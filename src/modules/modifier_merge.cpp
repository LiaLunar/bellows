// modifier_merge (on by default): the game's modifier-list merge 0x8EA420 with an index instead of a linear scan per
// entry (lab docs: docs/simulation-profile.md, "Tick miesięczny"; the loop in modifier_merge_core.hpp).
//
// The hook takes calls whose scans would compare many entries (the monthly ranking of the countries counts
// every fleet's cannons, which adds the country's dense list of about 700 modifiers to the fleet's list)
// and runs MergeFast on the game's list, growing it with the game's operator new and the list's own release
// call exactly where the game would, then records the source in the list's vector of sources the way 0x8EA420 ends. Every other call,
// and any input outside what the index covers, goes to the game's function.
//
// "verify" never changes a list: the game's function runs, and MergeFast runs on a copy, which must end
// with the same entries, count and capacity. "on" checks every 1024th fast call the same way (the game's
// function on the real list, MergeFast on the copy) and goes back to the game's function for good after
// a mismatch. A background thread writes the counters every 5 s to modifier_merge.tsv in the data
// directory, because the game is usually closed without a clean exit.
#include "modules.hpp"
#include "modifier_merge_core.hpp"

#include <cstring>
#include <fstream>
#include <string>

extern "C" {
// requests = calls, hits = calls merged by MergeFast, skips = calls below the threshold or not covered,
// verifications = calls compared with the game's function, mismatches = comparisons that differed,
// cacheDisabled = 1 after a mismatch in "on", mode = 1 on / 2 verify.
__declspec(dllexport) sb::Stats EU4SB_ModifierMergeStats = SB_STATS_INIT;
}

namespace {

using MergeFn = void* (*)(void* dest, void* src);
using NewFn = void* (*)(std::size_t size);
using EmplaceFn = void* (*)(void* vector, void* where, void** value);

constexpr std::int64_t kMinScanWork = 4096;  // compared entries below which the game's loop is as fast
constexpr std::size_t kModSourcesEnd = 0x40;  // std::vector<list*> of merged sources at +0x38
constexpr std::size_t kModSourcesCap = 0x48;
constexpr std::size_t kModRelease = 0x50;     // object whose vtable slot 2 releases the entry buffer

anvil::HookOf<sym::modifier_merge> g_hook;
MergeFn g_next = nullptr;
NewFn g_new = nullptr;
EmplaceFn g_emplace = nullptr;
bool g_verifyOnly = false;
volatile LONG64 g_checkSequence = 0;
thread_local sb::ModIndex* t_index = nullptr;
// Time of the covered calls: in the game's function (verify, checks) and in MergeFast on the real list.
volatile LONG64 g_gameTicks = 0, g_fastTicks = 0;
// Calls MergeFastApplies refused, and the first refused source id and destination slot id seen.
volatile LONG64 g_refusedSource = 0, g_refusedDense = 0, g_refusedEmpty = 0;
volatile LONG g_firstSourceId = 0, g_firstSlotId = 0;
std::wstring* g_dir = nullptr;
constexpr DWORD kWriteEveryMs = 5000;

// A destination list in game memory.
struct GameModList {
    unsigned char* base;

    bool dense() const { return sb::Read<std::uint8_t>(base, sb::kModDense) != 0; }
    std::int32_t* entries() const { return sb::Read<std::int32_t*>(base, sb::kModEntries); }
    std::int32_t count() const { return sb::Read<std::int32_t>(base, sb::kModCount); }
    std::int32_t capacity() const { return sb::Read<std::int32_t>(base, sb::kModCapacity); }
    void set_count(std::int32_t c) const { std::memcpy(base + sb::kModCount, &c, sizeof c); }
    // As 0x8EA420: allocate, copy the entries, release the old buffer through the list, then store.
    void grow(std::int32_t cap) const {
        // 8 * the zero-extended capacity, as the game computes it (it cannot overflow 64 bits).
        auto* fresh = static_cast<std::int32_t*>(g_new(static_cast<std::uint64_t>(static_cast<std::uint32_t>(cap)) * 8));
        const std::int32_t n = count();
        if (n > 0) std::memcpy(fresh, entries(), static_cast<std::size_t>(n) * 8);
        unsigned char* holder = base + kModRelease;
        const auto* vtable = sb::Read<void* const*>(holder, 0);
        reinterpret_cast<void (*)(void*)>(vtable[2])(holder);
        std::memcpy(base + sb::kModEntries, &fresh, sizeof fresh);
        std::memcpy(base + sb::kModCapacity, &cap, sizeof cap);
    }
};

sb::ModIndex& Index() {
    if (t_index == nullptr) t_index = new sb::ModIndex;  // one per thread, for the process
    return *t_index;
}

// The end of 0x8EA420: push the source into the list's vector of merged sources.
void RecordSource(unsigned char* dest, void* src) {
    auto* end = sb::Read<void**>(dest, kModSourcesEnd);
    if (end != sb::Read<void**>(dest, kModSourcesCap)) {
        *end = src;
        ++end;
        std::memcpy(dest + kModSourcesEnd, &end, sizeof end);
    } else {
        void* value = src;
        g_emplace(dest + 0x38, end, &value);
    }
}

sb::VectorModList Copy(const GameModList& list) {
    sb::VectorModList copy;
    copy.is_dense = list.dense();
    copy.n = list.count();
    copy.data.assign(2 * static_cast<std::size_t>(list.capacity()), 0);
    if (list.capacity() > 0) std::memcpy(copy.data.data(), list.entries(), copy.data.size() * 4);
    return copy;
}

bool SameAs(const GameModList& list, const sb::VectorModList& copy) {
    if (list.count() != copy.n || list.capacity() != copy.capacity()) return false;
    return copy.n == 0 || std::memcmp(list.entries(), copy.data.data(), static_cast<std::size_t>(copy.n) * 8) == 0;
}

void* Detour(void* destPtr, void* srcPtr) {
    InterlockedIncrement64(&EU4SB_ModifierMergeStats.requests);
    auto* dest = static_cast<unsigned char*>(destPtr);
    const auto* srcBase = static_cast<const unsigned char*>(srcPtr);
    if (destPtr == srcPtr || EU4SB_ModifierMergeStats.cacheDisabled != 0) return g_next(destPtr, srcPtr);
    const sb::ModSource src{sb::Read<std::uint8_t>(srcBase, sb::kModDense) != 0,
                            sb::Read<const std::int32_t*>(srcBase, sb::kModEntries),
                            sb::Read<std::int32_t>(srcBase, sb::kModCount)};
    GameModList list{dest};
    if (sb::ScanWork(list, src) < kMinScanWork) {
        InterlockedIncrement64(&EU4SB_ModifierMergeStats.skips);
        return g_next(destPtr, srcPtr);
    }
    if (!sb::MergeFastApplies(list, src)) {
        InterlockedIncrement64(&EU4SB_ModifierMergeStats.misses);
        std::int32_t badSource = 0, badSlot = 0;
        if (src.entries == nullptr || src.count <= 0) {
            InterlockedIncrement64(&g_refusedEmpty);
        } else if (sb::FirstUncoveredId(src, &badSource)) {
            InterlockedIncrement64(&g_refusedSource);
            InterlockedCompareExchange(&g_firstSourceId, badSource, 0);
        } else {
            InterlockedIncrement64(&g_refusedDense);
            if (sb::FirstDynamicStaticSlot(list, &badSlot)) InterlockedCompareExchange(&g_firstSlotId, badSlot, 0);
        }
        return g_next(destPtr, srcPtr);
    }
    // Only the index and the copy allocate on our side; both come before anything changes, so a failure
    // there leaves the call to the game. The game's own calls (its function, operator new) stay outside.
    sb::ModIndex* index = nullptr;
    sb::VectorModList copy;
    const bool check = g_verifyOnly || (InterlockedIncrement64(&g_checkSequence) & 1023) == 0;
    try {
        index = &Index();
        if (check) {
            copy = Copy(list);
            sb::MergeFast(copy, src, *index);
        }
    } catch (...) {
        InterlockedIncrement64(&EU4SB_ModifierMergeStats.skips);
        return g_next(destPtr, srcPtr);
    }
    if (check) {
        const LONG64 start = sb::Ticks();
        void* result = g_next(destPtr, srcPtr);
        InterlockedExchangeAdd64(&g_gameTicks, sb::Ticks() - start);
        InterlockedIncrement64(&EU4SB_ModifierMergeStats.verifications);
        if (!SameAs(list, copy)) {
            InterlockedIncrement64(&EU4SB_ModifierMergeStats.mismatches);
            if (!g_verifyOnly && InterlockedExchange(&EU4SB_ModifierMergeStats.cacheDisabled, 1) == 0)
                sb::Log(ANVIL_LOG_WARNING, "modifier_merge: a merge differed from the game's; back to the game's function");
        }
        return result;
    }
    const LONG64 start = sb::Ticks();
    sb::MergeFast(list, src, *index);
    RecordSource(dest, srcPtr);
    InterlockedExchangeAdd64(&g_fastTicks, sb::Ticks() - start);
    InterlockedIncrement64(&EU4SB_ModifierMergeStats.hits);
    return destPtr;
}

void WriteFile() {
    const sb::Stats& s = EU4SB_ModifierMergeStats;
    try {
        const std::wstring path = *g_dir + L"\\modifier_merge.tsv";
        const std::wstring temp = path + L".tmp";
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            const double ms = sb::TicksToMs(1);
            out << "mode\trequests\thits\tskips\tverifications\tmismatches\tdisabled\tgame_ms\tfast_ms\tnot_covered"
                   "\trefused_source\trefused_dense\trefused_empty\tfirst_source_id\tfirst_slot_id\n"
                << (g_verifyOnly ? "verify" : "on") << "\t" << s.requests << "\t" << s.hits << "\t" << s.skips << "\t"
                << s.verifications << "\t" << s.mismatches << "\t" << s.cacheDisabled << "\t" << g_gameTicks * ms << "\t"
                << g_fastTicks * ms << "\t" << s.misses << "\t" << g_refusedSource << "\t" << g_refusedDense << "\t"
                << g_refusedEmpty << "\t" << g_firstSourceId << "\t" << g_firstSlotId << "\n";
        }
        MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
    } catch (...) {
    }
}

DWORD WINAPI WriterThread(void*) {
    LONG64 written = -1;
    for (;;) {
        Sleep(kWriteEveryMs);
        if (EU4SB_ModifierMergeStats.requests != written) {
            written = EU4SB_ModifierMergeStats.requests;
            WriteFile();
        }
    }
}

}  // namespace

namespace sb {

bool InstallModifierMerge(anvil::Host& host, Mode mode) {
    if (mode == Mode::Off) return false;
    g_verifyOnly = mode == Mode::Verify;
    try {
        g_dir = new std::wstring(host.data_dir().wstring());
    } catch (...) {
        return false;
    }
    g_new = host.function(sym::game_operator_new);
    g_emplace = host.function(sym::pointer_vector_emplace);
    host.hook(g_hook, sym::modifier_merge, &Detour);
    g_next = g_hook.next_pointer();
    InterlockedExchange(&EU4SB_ModifierMergeStats.mode, g_verifyOnly ? 2 : 1);
    InterlockedExchange(&EU4SB_ModifierMergeStats.initialized, 1);
    HANDLE thread = CreateThread(nullptr, 0, WriterThread, nullptr, 0, nullptr);
    if (thread != nullptr) {
        SetThreadDescription(thread, L"bellows modifier_merge writer");
        CloseHandle(thread);
    }
    Log(ANVIL_LOG_INFO, "modifier_merge: %s large modifier merges with an index", g_verifyOnly ? "verifying" : "speeding up");
    return true;
}

void ReportModifierMerge() {
    LogStats("modifier_merge", EU4SB_ModifierMergeStats);
    bench::SetCounters("modifier_merge", EU4SB_ModifierMergeStats);
}

}  // namespace sb
