// never_throttle (off by default): while a single-player game runs at the fastest speed, sets the application's
// flag that skips the main loop's own waits, so a minimized game is no longer held to a frame every 100 ms (the
// reasoning in never_throttle_core.hpp).
//
// A hook on the daily tick notes the time of the last game day and the speed the game ran it at. After every frame
// of the main loop (anvil-game's frame event, on the main thread) the module decides whether the flag should be
// set and writes it only when that changes; it clears only a flag it set. Nothing in the game state is read or
// written apart from that one byte. A background thread writes the counters every 5 s to never_throttle.tsv in
// the data directory.
#include "never_throttle_core.hpp"
#include "modules.hpp"

#include <anvil/anvil_game.h>

#include <fstream>
#include <string>

extern "C" {
// requests = frames seen, hits = frames that ended with the module's flag set, misses = times the module set the
// flag, skips = times it cleared it, mode = 1 on.
__declspec(dllexport) sb::Stats EU4SB_NeverThrottleStats = SB_STATS_INIT;
}

namespace {

anvil::HookOf<sym::daily_tick> g_dayHook;
sym::Types::daily_tick::Pointer g_day = nullptr;
unsigned char* const* g_app = nullptr;  // the global that holds the application object
std::size_t g_flagOffset = 0;
std::size_t g_speedOffset = 0;
volatile LONG g_multiplayer = 0;
volatile LONG g_speed = -1;     // at the last game day
volatile LONG64 g_lastDay = 0;  // QPC ticks
sb::never_throttle::Owner g_owner;  // main thread only
LONG64 g_setAt = 0;                 // main thread only
volatile LONG64 g_setTicks = 0;     // time the module's flag was set, closed spells only
std::wstring* g_dir = nullptr;
constexpr DWORD kWriteEveryMs = 5000;

void DayDetour(void* state) {
    g_speed = sb::Read<std::int32_t>(state, g_speedOffset);
    g_lastDay = sb::Ticks();
    g_day(state);
}

void OnFrame() {
    InterlockedIncrement64(&EU4SB_NeverThrottleStats.requests);
    unsigned char* app = *g_app;
    if (app == nullptr) return;
    volatile std::uint8_t* flag = reinterpret_cast<volatile std::uint8_t*>(app + g_flagOffset);
    const LONG64 now = sb::Ticks();
    const double sinceDay = g_lastDay == 0 ? sb::never_throttle::kRunningMs : sb::TicksToMs(now - g_lastDay);
    const bool wanted = sb::never_throttle::Wanted(g_multiplayer != 0, g_speed, sinceDay);
    const int write = g_owner.Step(wanted, *flag);
    if (write == 1) {
        *flag = 1;
        g_setAt = now;
        InterlockedIncrement64(&EU4SB_NeverThrottleStats.misses);
    } else if (write == 0) {
        *flag = 0;
        InterlockedExchangeAdd64(&g_setTicks, now - g_setAt);
        InterlockedIncrement64(&EU4SB_NeverThrottleStats.skips);
    }
    if (g_owner.owned) InterlockedIncrement64(&EU4SB_NeverThrottleStats.hits);
}

void WriteFile() {
    const sb::Stats& s = EU4SB_NeverThrottleStats;
    try {
        const std::wstring path = *g_dir + L"\\never_throttle.tsv";
        const std::wstring temp = path + L".tmp";
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            out << "mode\tframes\tframes_set\tsets\tclears\tset_ms\n"
                << "on\t" << s.requests << "\t" << s.hits << "\t" << s.misses << "\t" << s.skips << "\t"
                << sb::TicksToMs(g_setTicks) << "\n";
        }
        MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
    } catch (...) {
    }
}

DWORD WINAPI WriterThread(void*) {
    LONG64 written = -1;
    for (;;) {
        Sleep(kWriteEveryMs);
        const LONG64 changes = EU4SB_NeverThrottleStats.misses + EU4SB_NeverThrottleStats.skips;
        if (changes != written) {
            written = changes;
            WriteFile();
        }
    }
}

}  // namespace

namespace sb {

void NoteNeverThrottleSession(bool multiplayer) noexcept { InterlockedExchange(&g_multiplayer, multiplayer ? 1 : 0); }

bool InstallNeverThrottle(anvil::Host& host, Mode mode) {
    if (mode == Mode::Off) return false;
    try {
        g_dir = new std::wstring(host.data_dir().wstring());
    } catch (...) {
        return false;
    }
    g_app = host.data(sym::app_global);
    g_flagOffset = static_cast<std::size_t>(host.value(sym::app_never_throttle, "never_throttle_offset"));
    g_speedOffset = static_cast<std::size_t>(host.value(sym::nested_frame_threshold, "speed_offset"));
    Undo undo;
    host.hook(g_dayHook, sym::daily_tick, &DayDetour);
    undo.add(g_dayHook);
    g_day = g_dayHook.next_pointer();
    host.subscribe(ANVIL_GAME_EVENT_FRAME, [] { OnFrame(); });
    undo.keep();
    InterlockedExchange(&EU4SB_NeverThrottleStats.mode, 1);
    InterlockedExchange(&EU4SB_NeverThrottleStats.initialized, 1);
    HANDLE thread = CreateThread(nullptr, 0, WriterThread, nullptr, 0, nullptr);
    if (thread != nullptr) {
        SetThreadDescription(thread, L"bellows never_throttle writer");
        CloseHandle(thread);
    }
    Log(ANVIL_LOG_INFO, "never_throttle: skipping the main loop's waits while a single-player game runs at the "
                        "fastest speed");
    return true;
}

void ReportNeverThrottle() {
    LogStats("never_throttle", EU4SB_NeverThrottleStats);
}

}  // namespace sb
