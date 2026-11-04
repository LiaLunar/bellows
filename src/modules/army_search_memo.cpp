// army_search_memo (off by default): within one AI army search (0x1022B0), the zone-of-control flag of the
// army from 0xD40AE0 once instead of for every move (lab docs: docs/simulation-profile.md; the reasoning in
// army_search_memo_core.hpp).
//
// The module redirects the only call of 0x1022B0 (0xEBB5F in 0xEAB50, ai_army.cpp) to mark the search on
// this thread, and the call of 0xD40AE0(army, 0xE0, 0) in 0xD45250 (0xD45291). Inside a search the first
// call runs the game's function and its answer serves the later calls for the same army; outside a search
// (0xD45250 has two other callers) every call runs the game's function.
//
// "verify" runs the game's function for every call and compares it with the memo; "on" compares every
// 1024th answer from the memo the same way and goes back to the game's function for good after a mismatch.
// A background thread writes the counters and the time in the game's function every 5 s to
// army_search_memo.tsv in the data directory.
#include "army_search_memo_core.hpp"
#include "modules.hpp"

#include <fstream>
#include <string>

extern "C" {
// requests = calls inside a search, hits = calls answered from the memo, misses = calls that ran the game's
// function to fill the memo, skips = calls outside a search, verifications = memo answers compared with the
// game's function, mismatches = comparisons that differed, duplicateKeys = searches, cacheDisabled = 1
// after a mismatch in "on", mode = 1 on / 2 verify.
__declspec(dllexport) sb::Stats EU4SB_ArmySearchMemoStats = SB_STATS_INIT;
}

namespace {

using SearchFn = std::uint64_t (*)(void* scorer, int mode, void* army, std::uint64_t arg, float* score, std::uint8_t a6,
                                   std::uint8_t* a7, std::uint8_t* a8, std::uint8_t* a9, std::uint8_t a10,
                                   std::uint8_t a11, void* path);
using FlagFn = std::uint8_t (*)(void* army, std::uint32_t modifier, std::uint8_t viaLocation, std::int64_t arg);

anvil::FunctionHook<std::uint64_t(void*, int, void*, std::uint64_t, float*, std::uint8_t, std::uint8_t*, std::uint8_t*,
                                  std::uint8_t*, std::uint8_t, std::uint8_t, void*)>
    g_searchSite;
anvil::FunctionHook<std::uint8_t(void*, std::uint32_t, std::uint8_t, std::int64_t)> g_flagSite;
SearchFn g_search = nullptr;
FlagFn g_flag = nullptr;
bool g_verifyOnly = false;
volatile LONG64 g_checkSequence = 0;
volatile LONG64 g_gameTicks = 0, g_gameCalls = 0;
std::wstring* g_dir = nullptr;
constexpr DWORD kWriteEveryMs = 5000;

thread_local sb::SearchMemo t_memo;

std::uint64_t SearchDetour(void* scorer, int mode, void* army, std::uint64_t arg, float* score, std::uint8_t a6,
                           std::uint8_t* a7, std::uint8_t* a8, std::uint8_t* a9, std::uint8_t a10, std::uint8_t a11,
                           void* path) {
    InterlockedIncrement64(&EU4SB_ArmySearchMemoStats.duplicateKeys);
    const sb::SearchMemo saved = sb::EnterSearch(t_memo);
    const std::uint64_t result = g_search(scorer, mode, army, arg, score, a6, a7, a8, a9, a10, a11, path);
    sb::LeaveSearch(t_memo, saved);
    return result;
}

std::uint8_t TimedFlag(void* army, std::uint32_t modifier, std::uint8_t viaLocation, std::int64_t arg) {
    const LONG64 start = sb::Ticks();
    const std::uint8_t value = g_flag(army, modifier, viaLocation, arg);
    InterlockedExchangeAdd64(&g_gameTicks, sb::Ticks() - start);
    InterlockedIncrement64(&g_gameCalls);
    return value;
}

std::uint8_t FlagDetour(void* army, std::uint32_t modifier, std::uint8_t viaLocation, std::int64_t arg) {
    const sb::MemoAnswer answer = sb::Lookup(t_memo, army);
    if (answer == sb::MemoAnswer::Outside) {
        InterlockedIncrement64(&EU4SB_ArmySearchMemoStats.skips);
        return g_flag(army, modifier, viaLocation, arg);
    }
    InterlockedIncrement64(&EU4SB_ArmySearchMemoStats.requests);
    if (EU4SB_ArmySearchMemoStats.cacheDisabled != 0) return g_flag(army, modifier, viaLocation, arg);
    if (answer == sb::MemoAnswer::Miss) {
        InterlockedIncrement64(&EU4SB_ArmySearchMemoStats.misses);
        const std::uint8_t value = TimedFlag(army, modifier, viaLocation, arg);
        sb::Store(t_memo, army, value);
        return value;
    }
    if (g_verifyOnly || (InterlockedIncrement64(&g_checkSequence) & 1023) == 0) {
        const std::uint8_t value = TimedFlag(army, modifier, viaLocation, arg);
        InterlockedIncrement64(&EU4SB_ArmySearchMemoStats.verifications);
        if (value != t_memo.value) {
            InterlockedIncrement64(&EU4SB_ArmySearchMemoStats.mismatches);
            if (!g_verifyOnly && InterlockedExchange(&EU4SB_ArmySearchMemoStats.cacheDisabled, 1) == 0)
                sb::Log(ANVIL_LOG_WARNING, "army_search_memo: an answer differed from the game's; back to the game's function");
        }
        return value;
    }
    InterlockedIncrement64(&EU4SB_ArmySearchMemoStats.hits);
    return t_memo.value;
}

void WriteFile() {
    const sb::Stats& s = EU4SB_ArmySearchMemoStats;
    try {
        const std::wstring path = *g_dir + L"\\army_search_memo.tsv";
        const std::wstring temp = path + L".tmp";
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            out << "mode\tsearches\trequests\thits\tmisses\toutside\tverifications\tmismatches\tdisabled\tgame_calls\tgame_ms\n"
                << (g_verifyOnly ? "verify" : "on") << "\t" << s.duplicateKeys << "\t" << s.requests << "\t" << s.hits << "\t"
                << s.misses << "\t" << s.skips << "\t" << s.verifications << "\t" << s.mismatches << "\t" << s.cacheDisabled
                << "\t" << g_gameCalls << "\t" << g_gameTicks * sb::TicksToMs(1) << "\n";
        }
        MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
    } catch (...) {
    }
}

DWORD WINAPI WriterThread(void*) {
    LONG64 written = -1;
    for (;;) {
        Sleep(kWriteEveryMs);
        if (EU4SB_ArmySearchMemoStats.requests != written) {
            written = EU4SB_ArmySearchMemoStats.requests;
            WriteFile();
        }
    }
}

}  // namespace

namespace sb {

bool InstallArmySearchMemo(anvil::Host& host, Mode mode) {
    if (mode == Mode::Off) return false;
    g_verifyOnly = mode == Mode::Verify;
    try {
        g_dir = new std::wstring(host.data_dir().wstring());
    } catch (...) {
        return false;
    }
    host.redirect(g_searchSite, sym::army_search_call, &SearchDetour);
    g_search = g_searchSite.next_pointer();
    host.redirect(g_flagSite, sym::zoc_flag_call, &FlagDetour);
    g_flag = g_flagSite.next_pointer();
    InterlockedExchange(&EU4SB_ArmySearchMemoStats.mode, g_verifyOnly ? 2 : 1);
    InterlockedExchange(&EU4SB_ArmySearchMemoStats.initialized, 1);
    HANDLE thread = CreateThread(nullptr, 0, WriterThread, nullptr, 0, nullptr);
    if (thread != nullptr) {
        SetThreadDescription(thread, L"bellows army_search_memo writer");
        CloseHandle(thread);
    }
    Log(ANVIL_LOG_INFO, "army_search_memo: %s the army's zone-of-control flag once per AI army search",
        g_verifyOnly ? "verifying" : "computing");
    return true;
}

void ReportArmySearchMemo() {
    // No benchmark counters: the report is at its limit of 128 fields; the mode is there, the counters in the log.
    LogStats("army_search_memo", EU4SB_ArmySearchMemoStats);
}

}  // namespace sb
