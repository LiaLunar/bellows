// Start-up "does this virtual file exist" index (ported from the lab's
// startup_exists_index.inc).
//
// The game's file-system object (vtable 0x1D9C968) answers existence queries
// through slot +0x30 = 0x15CCE80, a thin wrapper around PHYSFS_exists. The
// modifier picture verifier asks it once per picture; every PHYSFS_exists walks
// the whole search path with several GetFileAttributesExW calls (~5 s of CPU
// during database loading in the lab).
//
// This module swaps that vtable slot (Anvil swap_pointer on the declared slot
// fs_exists_slot; the original's first bytes are declared too). A query
// is answered "exists" only when the path is found in a once-read listing of a
// *directory* entry of the PhysFS search path mounted at "/". Everything else
// (archives such as DLC zips, unusual spellings, symlinks, other mount points,
// a disabled index, after the game is ready) goes to the original function, so
// a "not found" is never invented. Every 256th positive answer is checked
// against the original ("verify": every one); a mismatch disables the index.
#include "exists_index_core.hpp"
#include "modules.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using ExistsFn = bool (*)(void*, const void*);
using GetSearchPathFn = char** (*)();
using FreeListFn = void (*)(void*);
using GetMountPointFn = const char* (*)(const char*);
using SymlinksPermittedFn = int (*)();
using GetWriteDirFn = const char* (*)();

using sb::exists::Kind;
using sb::exists::Listing;
using sb::exists::ReadListing;
using sb::exists::SplitVirtualPath;

struct Root {
    std::wstring path;  // directory, no trailing separator
};

struct State {
    SRWLOCK lock = SRWLOCK_INIT;
    std::vector<Root> roots;
    // key: root index + '|' + upper-cased relative directory ("" for the root)
    std::unordered_map<std::string, std::shared_ptr<const Listing>> listings;
    bool symlinksPermitted = false;
    volatile LONG64 positiveSequence = 0;
};

// The rest of the slot's chain (the original function unless another plugin
// swapped the slot too); Anvil rewrites it while the chain changes.
anvil::PointerSwap<bool(void*, const void*)> g_existsSlot;
State* g_state = nullptr;
GetSearchPathFn g_getSearchPath = nullptr;
FreeListFn g_freeList = nullptr;
GetMountPointFn g_getMountPoint = nullptr;
SymlinksPermittedFn g_symlinksPermitted = nullptr;
GetWriteDirFn g_getWriteDir = nullptr;
bool g_verifyAll = false;

}  // namespace

extern "C" {
// requests = queries seen, hits = answered "exists" from listings, misses = sent
// to the original after a listing miss, skips = sent to the original without a
// lookup, duplicateKeys = directory listings read, verifications/mismatches =
// checks against the original, mode = 1 when installed.
__declspec(dllexport) sb::Stats EU4SB_ExistsStats = SB_STATS_INIT;
}

namespace {

// MSVC std::string: inline buffer when capacity (+0x18) < 16, else pointer at +0.
bool ReadMsvcString(const void* object, const char*& text, std::size_t& length) {
    const auto* bytes = static_cast<const unsigned char*>(object);
    const std::size_t size = *reinterpret_cast<const std::size_t*>(bytes + 0x10);
    const std::size_t capacity = *reinterpret_cast<const std::size_t*>(bytes + 0x18);
    if (size > capacity || size > 4096) return false;
    text = capacity >= 16 ? *reinterpret_cast<const char* const*>(bytes) : reinterpret_cast<const char*>(bytes);
    length = size;
    return text != nullptr;
}

bool Utf8ToWide(const char* text, std::wstring& out) {
    const int length = static_cast<int>(std::strlen(text));
    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, length, nullptr, 0);
    if (required <= 0) return false;
    out.assign(static_cast<std::size_t>(required), L'\0');
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, length, out.data(), required) == required;
}

std::shared_ptr<const Listing> GetListing(State& state, std::size_t rootIndex, const std::vector<std::string>& parts,
                                          std::size_t depth) {
    std::string key = std::to_string(rootIndex) + "|";
    std::wstring directory = state.roots[rootIndex].path;
    for (std::size_t index = 0; index < depth; ++index) {
        key += parts[index];
        key += '/';
        directory += L'\\';
        directory.append(parts[index].begin(), parts[index].end());
    }
    AcquireSRWLockShared(&state.lock);
    auto found = state.listings.find(key);
    std::shared_ptr<const Listing> existing = found == state.listings.end() ? nullptr : found->second;
    ReleaseSRWLockShared(&state.lock);
    if (existing) return existing;
    std::shared_ptr<const Listing> fresh = ReadListing(directory, state.symlinksPermitted);
    sb::Increment(&EU4SB_ExistsStats.duplicateKeys);
    AcquireSRWLockExclusive(&state.lock);
    std::shared_ptr<const Listing> result;
    try {
        result = state.listings.emplace(std::move(key), fresh).first->second;
    } catch (...) {
        result = fresh;
    }
    ReleaseSRWLockExclusive(&state.lock);
    return result;
}

bool FoundUnderRoot(State& state, std::size_t rootIndex, const std::vector<std::string>& parts) {
    return sb::exists::Found(parts, [&](std::size_t depth) { return GetListing(state, rootIndex, parts, depth); });
}

// Directory entries of the PhysFS search path mounted at "/", except the write
// directory and anything inside it (the game changes files there while it
// starts, so a listing could be stale).
void LoadRoots(State& state) {
    char** list = g_getSearchPath();
    if (list == nullptr) return;
    std::wstring writeDir;
    if (const char* w = g_getWriteDir ? g_getWriteDir() : nullptr) Utf8ToWide(w, writeDir);
    while (!writeDir.empty() && (writeDir.back() == L'\\' || writeDir.back() == L'/')) writeDir.pop_back();
    for (char** entry = list; *entry != nullptr; ++entry) {
        const char* mountPoint = g_getMountPoint(*entry);
        if (mountPoint == nullptr || std::strcmp(mountPoint, "/") != 0) continue;
        std::wstring path;
        if (!Utf8ToWide(*entry, path)) continue;
        while (path.size() > 3 && (path.back() == L'\\' || path.back() == L'/')) path.pop_back();
        if (!writeDir.empty() && path.size() >= writeDir.size() &&
            _wcsnicmp(path.c_str(), writeDir.c_str(), writeDir.size()) == 0 &&
            (path.size() == writeDir.size() || path[writeDir.size()] == L'\\' || path[writeDir.size()] == L'/')) {
            continue;
        }
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            state.roots.push_back(Root{path});
        }
    }
    g_freeList(list);
}

INIT_ONCE g_initOnce = INIT_ONCE_STATIC_INIT;

BOOL CALLBACK InitializeState(PINIT_ONCE, PVOID, PVOID*) noexcept {
    try {
        auto* state = new State();
        state->symlinksPermitted = g_symlinksPermitted != nullptr && g_symlinksPermitted() != 0;
        LoadRoots(*state);
        g_state = state;
    } catch (...) {
        g_state = nullptr;
    }
    InterlockedExchange(&EU4SB_ExistsStats.initialized, 1);
    return TRUE;
}

bool ReadyReached() noexcept {
    if (InterlockedCompareExchange(&EU4SB_ExistsStats.readyDisabled, 0, 0) != 0) return true;
    if (sb::ReadyReached()) {
        InterlockedExchange(&EU4SB_ExistsStats.readyDisabled, 1);
        return true;
    }
    return false;
}

}  // namespace

extern "C" bool SB_FileExists(void* self, const void* name) {
    sb::Increment(&EU4SB_ExistsStats.requests);
    const DWORD priorError = GetLastError();
    try {
        const char* text = nullptr;
        std::size_t length = 0;
        std::vector<std::string> parts;
        if (ReadyReached() || InterlockedCompareExchange(&EU4SB_ExistsStats.cacheDisabled, 0, 0) != 0 ||
            name == nullptr || !ReadMsvcString(name, text, length) || !SplitVirtualPath(text, length, parts)) {
            sb::Increment(&EU4SB_ExistsStats.skips);
            SetLastError(priorError);
            return g_existsSlot.previous()(self, name);
        }
        InitOnceExecuteOnce(&g_initOnce, InitializeState, nullptr, nullptr);
        State* state = g_state;
        if (state == nullptr || state->roots.empty()) {
            sb::Increment(&EU4SB_ExistsStats.skips);
            SetLastError(priorError);
            return g_existsSlot.previous()(self, name);
        }
        bool found = false;
        for (std::size_t root = 0; root < state->roots.size() && !found; ++root) {
            found = FoundUnderRoot(*state, root, parts);
        }
        SetLastError(priorError);
        if (!found) {
            sb::Increment(&EU4SB_ExistsStats.misses);
            return g_existsSlot.previous()(self, name);
        }
        const LONG64 sequence = InterlockedIncrement64(&state->positiveSequence);
        if (g_verifyAll || (sequence & 255) == 0) {
            sb::Increment(&EU4SB_ExistsStats.verifications);
            if (!g_existsSlot.previous()(self, name)) {
                sb::Increment(&EU4SB_ExistsStats.mismatches);
                if (!g_verifyAll) InterlockedExchange(&EU4SB_ExistsStats.cacheDisabled, 1);
                SetLastError(priorError);
                return false;
            }
            SetLastError(priorError);
        }
        sb::Increment(&EU4SB_ExistsStats.hits);
        return true;
    } catch (...) {
        sb::Increment(&EU4SB_ExistsStats.skips);
        SetLastError(priorError);
        return g_existsSlot.previous()(self, name);
    }
}

namespace sb {

bool InstallExistsIndex(anvil::Host& host, Mode mode) {
    if (mode == Mode::Off) return false;
    g_getSearchPath = host.function(sym::physfs_get_search_path);
    g_freeList = host.function(sym::physfs_free_list);
    g_getMountPoint = host.function(sym::physfs_get_mount_point);
    g_symlinksPermitted = host.function(sym::physfs_symbolic_links_permitted);
    g_getWriteDir = host.function(sym::physfs_get_write_dir);
    g_verifyAll = mode == Mode::Verify;
    host.swap(g_existsSlot, sym::fs_exists_slot, &SB_FileExists);
    InterlockedExchange(&EU4SB_ExistsStats.mode, 1);
    return true;
}

void ReportExistsIndex() {
    LogStats("exists_index", EU4SB_ExistsStats);
    bench::SetCounters("exists_index", EU4SB_ExistsStats);
}

}  // namespace sb
