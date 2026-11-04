// pops_pacing (off by default): at the fastest speed the game's online-service pump POPS_RunCallbacks runs every
// 500 ms instead of twice a frame (the reasoning in pops_pacing_core.hpp).
//
// The import of POPS_RunCallbacks in eu4.exe is replaced. A hook on the daily tick notes the time of the last game
// day and the speed the game ran it at (read there, so the pump never reads the game's state). "on" lets a call
// wait while the game runs at the fastest speed and that context was pumped less than 500 ms ago; every other
// call, multiplayer and pauses go to the game's function as they came. "verify" calls the game's function every
// time and counts what "on" would have left out. A background thread writes the counters and the time in the
// pump every 5 s to pops_pacing.tsv in the data directory.
#include "pops_pacing_core.hpp"
#include "modules.hpp"

#include <fstream>
#include <string>

extern "C" {
// requests = calls of the pump, hits = calls left out ("on") or that would have been ("verify"), skips = calls
// that went to the game's pump at the fastest speed, mode = 1 on / 2 verify.
__declspec(dllexport) sb::Stats EU4SB_PopsPacingStats = SB_STATS_INIT;
}

namespace {

anvil::FunctionHook<void(void*)> g_pumpImport;
anvil::HookOf<sym::daily_tick> g_dayHook;
void (*g_pump)(void*) = nullptr;
sym::Types::daily_tick::Pointer g_day = nullptr;
bool g_verifyOnly = false;
std::size_t g_speedOffset = 0;
volatile LONG g_multiplayer = 0;
volatile LONG g_speed = -1;      // at the last game day
volatile LONG64 g_lastDay = 0;   // QPC ticks
volatile LONG64 g_pumpTicks = 0;
sb::pops::Contexts g_contexts;   // main thread only
std::wstring* g_dir = nullptr;
constexpr DWORD kWriteEveryMs = 5000;

void DayDetour(void* state) {
    g_speed = sb::Read<std::int32_t>(state, g_speedOffset);
    g_lastDay = sb::Ticks();
    g_day(state);
}

void PumpDetour(void* context) {
    InterlockedIncrement64(&EU4SB_PopsPacingStats.requests);
    const LONG64 now = sb::Ticks();
    const std::size_t slot = g_contexts.Slot(context);
    if (slot < sb::pops::kContexts) {
        const double clock = sb::TicksToMs(now);
        const bool wait = sb::pops::MayWait(g_multiplayer != 0, g_speed, sb::TicksToMs(now - g_lastDay),
                                            clock - g_contexts.last[slot]);
        if (wait) {
            InterlockedIncrement64(&EU4SB_PopsPacingStats.hits);
            if (!g_verifyOnly) return;
        } else {
            g_contexts.last[slot] = clock;
            if (g_speed == sb::pops::kFastest) InterlockedIncrement64(&EU4SB_PopsPacingStats.skips);
        }
    }
    g_pump(context);
    InterlockedExchangeAdd64(&g_pumpTicks, sb::Ticks() - now);
}

void WriteFile() {
    const sb::Stats& s = EU4SB_PopsPacingStats;
    try {
        const std::wstring path = *g_dir + L"\\pops_pacing.tsv";
        const std::wstring temp = path + L".tmp";
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            out << "mode\tcalls\tleft_out\tfastest_pumped\tpump_ms\n"
                << (g_verifyOnly ? "verify" : "on") << "\t" << s.requests << "\t" << s.hits << "\t" << s.skips << "\t"
                << sb::TicksToMs(g_pumpTicks) << "\n";
        }
        MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
    } catch (...) {
    }
}

DWORD WINAPI WriterThread(void*) {
    LONG64 written = -1;
    for (;;) {
        Sleep(kWriteEveryMs);
        if (EU4SB_PopsPacingStats.requests != written) {
            written = EU4SB_PopsPacingStats.requests;
            WriteFile();
        }
    }
}

}  // namespace

namespace sb {

void NotePopsSession(bool multiplayer) noexcept { InterlockedExchange(&g_multiplayer, multiplayer ? 1 : 0); }

bool InstallPopsPacing(anvil::Host& host, Mode mode) {
    if (mode == Mode::Off) return false;
    g_verifyOnly = mode == Mode::Verify;
    try {
        g_dir = new std::wstring(host.data_dir().wstring());
    } catch (...) {
        return false;
    }
    g_speedOffset = static_cast<std::size_t>(host.value(sym::nested_frame_threshold, "speed_offset"));
    Undo undo;
    host.hook(g_dayHook, sym::daily_tick, &DayDetour);
    undo.add(g_dayHook);
    g_day = g_dayHook.next_pointer();
    host.replace_import(g_pumpImport, "pops_api.dll", "POPS_RunCallbacks", &PumpDetour);
    undo.add(g_pumpImport);
    g_pump = g_pumpImport.next_pointer();
    undo.keep();
    InterlockedExchange(&EU4SB_PopsPacingStats.mode, g_verifyOnly ? 2 : 1);
    InterlockedExchange(&EU4SB_PopsPacingStats.initialized, 1);
    HANDLE thread = CreateThread(nullptr, 0, WriterThread, nullptr, 0, nullptr);
    if (thread != nullptr) {
        SetThreadDescription(thread, L"bellows pops_pacing writer");
        CloseHandle(thread);
    }
    Log(ANVIL_LOG_INFO, "pops_pacing: %s the online-service pump to every %.0f ms at the fastest speed",
        g_verifyOnly ? "counting what would pace" : "pacing", pops::kIntervalMs);
    return true;
}

void ReportPopsPacing() {
    LogStats("pops_pacing", EU4SB_PopsPacingStats);
}

}  // namespace sb
