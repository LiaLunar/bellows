// develop_search_memo (off by default): within one AI walk over a country's provinces (0x1B5170), the search for the best
// province to develop (0xBDE10) once per kind of development instead of once per province (lab docs:
// docs/simulation-profile.md; the reasoning in develop_search_memo_core.hpp).
//
// The module redirects the only call of 0x1B5170 (0x1B4995 in 0x1B44B0) to mark the walk on this thread, and its
// call of 0xBDE10 (0x1B528B). 0xBDE10 has other callers; those go on to the game's function.
//
// "verify" runs the game's function for every call and compares it with the memo; "on" compares every
// 1024th answer from the memo the same way and goes back to the game's function for good after a mismatch.
// A background thread writes the counters and the time in the game's function every 5 s to
// develop_search_memo.tsv in the data directory.
#include "develop_search_memo_core.hpp"
#include "modules.hpp"

#include <fstream>
#include <string>

extern "C" {
// requests = searches inside a walk, hits = searches answered from the memo, misses = searches that ran the game's
// function to fill the memo, skips = searches the memo cannot serve, verifications = memo answers compared with
// the game's function, mismatches = comparisons that differed, duplicateKeys = walks, cacheDisabled = 1 after a
// mismatch in "on", mode = 1 on / 2 verify.
__declspec(dllexport) sb::Stats EU4SB_DevelopSearchMemoStats = SB_STATS_INIT;
}

namespace {

using WalkFn = void (*)(void* ai);
using BestFn = std::uint64_t (*)(void* ai, int* threshold, int kind);

anvil::FunctionHook<void(void*)> g_walkSite;
anvil::FunctionHook<std::uint64_t(void*, int*, int)> g_bestSite;
WalkFn g_walk = nullptr;
BestFn g_best = nullptr;
bool g_verifyOnly = false;
volatile LONG64 g_checkSequence = 0;
volatile LONG64 g_gameTicks = 0, g_gameCalls = 0;
std::wstring* g_dir = nullptr;
constexpr DWORD kWriteEveryMs = 5000;

thread_local sb::DevelopMemo t_memo;

void WalkDetour(void* ai) {
    InterlockedIncrement64(&EU4SB_DevelopSearchMemoStats.duplicateKeys);
    const sb::DevelopMemo saved = sb::EnterWalk(t_memo);
    g_walk(ai);
    sb::LeaveWalk(t_memo, saved);
}

std::uint64_t TimedBest(void* ai, int* threshold, int kind) {
    const LONG64 start = sb::Ticks();
    const std::uint64_t value = g_best(ai, threshold, kind);
    InterlockedExchangeAdd64(&g_gameTicks, sb::Ticks() - start);
    InterlockedIncrement64(&g_gameCalls);
    return value;
}

std::uint64_t BestDetour(void* ai, int* threshold, int kind) {
    const sb::DevelopAnswer answer = sb::LookupDevelop(t_memo, ai, *threshold, kind);
    if (answer == sb::DevelopAnswer::Outside) {
        InterlockedIncrement64(&EU4SB_DevelopSearchMemoStats.skips);
        return g_best(ai, threshold, kind);
    }
    InterlockedIncrement64(&EU4SB_DevelopSearchMemoStats.requests);
    if (EU4SB_DevelopSearchMemoStats.cacheDisabled != 0) return g_best(ai, threshold, kind);
    if (answer == sb::DevelopAnswer::Miss) {
        InterlockedIncrement64(&EU4SB_DevelopSearchMemoStats.misses);
        const std::uint64_t value = TimedBest(ai, threshold, kind);
        sb::StoreDevelop(t_memo, ai, kind, value);
        return value;
    }
    const std::uint64_t kept = t_memo.best[kind];
    if (g_verifyOnly || (InterlockedIncrement64(&g_checkSequence) & 1023) == 0) {
        const std::uint64_t value = TimedBest(ai, threshold, kind);
        InterlockedIncrement64(&EU4SB_DevelopSearchMemoStats.verifications);
        if (value != kept) {
            InterlockedIncrement64(&EU4SB_DevelopSearchMemoStats.mismatches);
            if (!g_verifyOnly && InterlockedExchange(&EU4SB_DevelopSearchMemoStats.cacheDisabled, 1) == 0)
                sb::Log(ANVIL_LOG_WARNING, "develop_search_memo: an answer differed from the game's; back to the game's function");
        }
        return value;
    }
    InterlockedIncrement64(&EU4SB_DevelopSearchMemoStats.hits);
    return kept;
}

void WriteFile() {
    const sb::Stats& s = EU4SB_DevelopSearchMemoStats;
    try {
        const std::wstring path = *g_dir + L"\\develop_search_memo.tsv";
        const std::wstring temp = path + L".tmp";
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            out << "mode\twalks\trequests\thits\tmisses\toutside\tverifications\tmismatches\tdisabled\tgame_calls\tgame_ms\n"
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
        if (EU4SB_DevelopSearchMemoStats.requests != written) {
            written = EU4SB_DevelopSearchMemoStats.requests;
            WriteFile();
        }
    }
}

}  // namespace

namespace sb {

bool InstallDevelopSearchMemo(anvil::Host& host, Mode mode) {
    if (mode == Mode::Off) return false;
    g_verifyOnly = mode == Mode::Verify;
    try {
        g_dir = new std::wstring(host.data_dir().wstring());
    } catch (...) {
        return false;
    }
    host.redirect(g_bestSite, sym::develop_best_call, &BestDetour);
    g_best = g_bestSite.next_pointer();
    host.redirect(g_walkSite, sym::develop_walk_call, &WalkDetour);
    g_walk = g_walkSite.next_pointer();
    InterlockedExchange(&EU4SB_DevelopSearchMemoStats.mode, g_verifyOnly ? 2 : 1);
    InterlockedExchange(&EU4SB_DevelopSearchMemoStats.initialized, 1);
    HANDLE thread = CreateThread(nullptr, 0, WriterThread, nullptr, 0, nullptr);
    if (thread != nullptr) {
        SetThreadDescription(thread, L"bellows develop_search_memo writer");
        CloseHandle(thread);
    }
    Log(ANVIL_LOG_INFO, "develop_search_memo: %s the best province to develop once per kind in one AI walk",
        g_verifyOnly ? "verifying" : "computing");
    return true;
}

void ReportDevelopSearchMemo() {
    // Not in the benchmark report, which is at its limit of 128 fields: the counters are in the log.
    LogStats("develop_search_memo", EU4SB_DevelopSearchMemoStats);
}

}  // namespace sb
