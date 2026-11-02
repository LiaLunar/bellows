// Start-up answers for the stat calls of PhysFS verifyPath (ported from the
// lab's startup_verifypath_listing.inc).
//
// PhysFS verifyPath runs whenever symbolic links are not permitted (always in
// this game). For every searched directory archive it stats each prefix of the
// path ("gfx", "gfx/interface", "gfx/interface/x.dds") through the archiver's
// stat slot (call [r9+68h]) only to learn: did the stat succeed, is the entry a
// symlink (filetype 2), and on failure is the error PHYSFS_ERR_NOT_FOUND (0xb).
//
// The call and the following `mov ebp,eax` are replaced as one block (Anvil
// replace_block on the declared symbol verifypath_stat_call): the callback
// calls SB_VerifyPathStat with the call's arguments and writes its result to
// eax and ebp. For the directory archiver it answers from
// once-read directory listings exactly as the platform stat would for
// verifyPath's purposes: existing directory/junction -> rc 1, filetype 1;
// existing file -> rc 1, filetype 0; missing name or directory -> rc 0 with
// PHYSFS_setErrorCode(0xb). Symbolic links, other reparse points, unusual
// spellings, non-directory archives, unreadable listings, a disabled module
// and anything after the game is ready go to the original stat. Every 256th
// answer is compared with the original; a disagreement disables the module
// ("verify" mode compares every answer). Assumes game and mod files do not
// change while the game starts.
#include "modules.hpp"
#include "verifypath_listing_core.hpp"

#include <memory>
#include <string>
#include <unordered_map>

namespace {

using sb::verifypath::Answer;
using sb::verifypath::Kind;
using sb::verifypath::Listing;
using sb::verifypath::ReadListing;

constexpr int kPhysfsNotFound = 0xB;
constexpr std::size_t kArchiverStat = 0x68;  // the stat slot of a PhysFS archiver function table

using StatFn = int (*)(void*, const char*, void*);
using SetErrorFn = void (*)(int);
using GetErrorFn = int (*)();
using GetWriteDirFn = const char* (*)();

struct PhysfsStat {
    std::int64_t filesize;
    std::int64_t modtime;
    std::int64_t createtime;
    std::int64_t accesstime;
    std::int32_t filetype;
    std::int32_t readonly;
};
static_assert(sizeof(PhysfsStat) == 0x28, "PHYSFS_Stat layout drift");

uintptr_t g_dirArchiver = 0;
SetErrorFn g_setError = nullptr;
GetErrorFn g_getError = nullptr;
GetWriteDirFn g_getWriteDir = nullptr;
SRWLOCK g_lock = SRWLOCK_INIT;
std::unordered_map<std::wstring, std::shared_ptr<const Listing>>* g_listings = nullptr;
volatile LONG64 g_answerSequence = 0;
bool g_verifyAll = false;

}  // namespace

extern "C" {
// requests = calls seen, hits = answered "exists", misses = answered NOT_FOUND,
// skips = passed to the original, duplicateKeys = listings read,
// verifications/mismatches = checks against the original, mode = 1 patched,
// cacheDisabled = disabled after a mismatch.
__declspec(dllexport) sb::Stats EU4SB_VerifyPathStats = SB_STATS_INIT;
}

namespace {

std::shared_ptr<const Listing> GetListing(const std::wstring& directory) {
    AcquireSRWLockShared(&g_lock);
    std::shared_ptr<const Listing> existing;
    auto found = g_listings->find(directory);
    if (found != g_listings->end()) existing = found->second;
    ReleaseSRWLockShared(&g_lock);
    if (existing) return existing;
    std::shared_ptr<const Listing> fresh = ReadListing(directory);
    sb::Increment(&EU4SB_VerifyPathStats.duplicateKeys);
    AcquireSRWLockExclusive(&g_lock);
    std::shared_ptr<const Listing> result;
    try {
        result = g_listings->emplace(directory, fresh).first->second;
    } catch (...) {
        result = fresh;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return result;
}

// The write directory unknown (no symbol): every root goes to the original stat.
bool IsWriteRoot(const char* root) {
    if (g_getWriteDir == nullptr) return true;
    return sb::verifypath::IsWriteRoot(root, g_getWriteDir());  // PhysFS state lock is recursive; verifyPath already holds it
}

Answer Decide(const char* root, const char* name, Kind& kind) {
    if (root == nullptr || name == nullptr || *name == '\0') return Answer::Original;
    if (IsWriteRoot(root)) return Answer::Original;
    return sb::verifypath::DecideInRoot(root, name, kind, GetListing);
}

bool Disabled() noexcept {
    if (InterlockedCompareExchange(&EU4SB_VerifyPathStats.cacheDisabled, 0, 0) != 0) return true;
    if (InterlockedCompareExchange(&EU4SB_VerifyPathStats.readyDisabled, 0, 0) != 0) return true;
    if (sb::ReadyReached()) {
        InterlockedExchange(&EU4SB_VerifyPathStats.readyDisabled, 1);
        return true;
    }
    return false;
}

}  // namespace

extern "C" int SB_VerifyPathStat(void* opaque, const char* name, void* statOut, const void* funcs) {
    sb::Increment(&EU4SB_VerifyPathStats.requests);
    const auto original = *reinterpret_cast<const StatFn*>(static_cast<const unsigned char*>(funcs) + kArchiverStat);
    try {
        Kind kind = Kind::Unknown;
        const Answer answer = (Disabled() || reinterpret_cast<uintptr_t>(funcs) != g_dirArchiver || g_listings == nullptr)
                                  ? Answer::Original
                                  : Decide(static_cast<const char*>(opaque), name, kind);
        if (answer == Answer::Original) {
            sb::Increment(&EU4SB_VerifyPathStats.skips);
            return original(opaque, name, statOut);
        }
        const LONG64 sequence = InterlockedIncrement64(&g_answerSequence);
        if (g_verifyAll || (sequence & 255) == 0) {
            sb::Increment(&EU4SB_VerifyPathStats.verifications);
            PhysfsStat actual{};
            const int rc = original(opaque, name, &actual);
            const int error = rc ? 0 : g_getError();  // clears the code; set again below
            const bool same = answer == Answer::Exists ? (rc == 1 && actual.filetype != 2)
                                                       : (rc == 0 && error == kPhysfsNotFound);
            if (!same || g_verifyAll) {
                if (!same) {
                    const LONG64 count = InterlockedIncrement64(&EU4SB_VerifyPathStats.mismatches);
                    if (!g_verifyAll) InterlockedExchange(&EU4SB_VerifyPathStats.cacheDisabled, 1);
                    if (count <= 10) {
                        sb::Log(ANVIL_LOG_WARNING,
                                "verifypath mismatch: root '%s' name '%s': listing says %s, original rc %d type %d error %d",
                                static_cast<const char*>(opaque), name,
                                answer == Answer::Exists ? (kind == Kind::Directory ? "directory" : "file") : "not found",
                                rc, actual.filetype, error);
                    }
                }
                std::memcpy(statOut, &actual, sizeof(actual));
                if (!rc) g_setError(error);
                return rc;
            }
        }
        if (answer == Answer::NotFound) {
            sb::Increment(&EU4SB_VerifyPathStats.misses);
            g_setError(kPhysfsNotFound);
            return 0;
        }
        PhysfsStat result{};
        result.filesize = kind == Kind::Directory ? 0 : -1;  // not read by verifyPath
        result.modtime = result.createtime = result.accesstime = -1;
        result.filetype = kind == Kind::Directory ? 1 : 0;
        std::memcpy(statOut, &result, sizeof(result));
        sb::Increment(&EU4SB_VerifyPathStats.hits);
        return 1;
    } catch (...) {
        sb::Increment(&EU4SB_VerifyPathStats.skips);
        return original(opaque, name, statOut);
    }
}

namespace sb {

bool InstallVerifyPathListing(anvil::Host& host, Mode mode) {
    if (mode == Mode::Off) return false;
    g_setError = host.function(sym::physfs_set_error_code);
    g_getError = host.function(sym::physfs_get_last_error_code);
    g_getWriteDir = host.function(sym::physfs_get_write_dir);
    try {
        g_listings = new std::unordered_map<std::wstring, std::shared_ptr<const Listing>>();
    } catch (...) {
        return false;
    }
    // Anvil verified that the table's stat slot points to the DIR archiver's stat.
    g_dirArchiver = reinterpret_cast<uintptr_t>(host.address(sym::dir_archiver_stat)) - kArchiverStat;
    g_verifyAll = mode == Mode::Verify;

    // The block is `call [r9+68h]` (stat(opaque = rcx, name = rdx, stat = r8),
    // r9 = the archiver's function table) and `mov ebp,eax`. The callback runs
    // instead and the game resumes after the block; the call's volatile
    // registers are free at that point, as after any call.
    host.replace_block(sym::verifypath_stat_call, [](anvil::Cpu& cpu) {
            const int rc = SB_VerifyPathStat(reinterpret_cast<void*>(cpu.rcx), reinterpret_cast<const char*>(cpu.rdx),
                                             reinterpret_cast<void*>(cpu.r8), reinterpret_cast<const void*>(cpu.r9));
            cpu.rax = static_cast<std::uint32_t>(rc);
            cpu.rbp = static_cast<std::uint32_t>(rc);  // mov ebp,eax zero-extends
        }).release();  // kept for the process
    InterlockedExchange(&EU4SB_VerifyPathStats.mode, 1);
    InterlockedExchange(&EU4SB_VerifyPathStats.initialized, 1);
    return true;
}

void ReportVerifyPathListing() {
    LogStats("verifypath_listing", EU4SB_VerifyPathStats);
    bench::SetCounters("verifypath_listing", EU4SB_VerifyPathStats);
}

}  // namespace sb
