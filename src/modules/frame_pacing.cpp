// frame_pacing: at the fastest speed, frames drawn from inside the daily tick come less often
// (lab docs/simulation-profile.md, "Klatki rysowane w trakcie ticku").
//
// 0x769EA0 is called about 40 times between the stages of the daily tick. It draws a frame
// (0x14C25A0 with outer = 0) when the time since the last one exceeds a threshold:
// base [0x245B070] (1.0 unless the player's settings object says otherwise) times a table
// indexed by the game speed at +0x1E08 (0.02 for indexes 0-3, 0.05 for 4). In the lab these
// frames took about 16% of the main thread's time at speed 5. A mid hook after the clock read
// replaces the threshold in xmm6 for index 4 only; the game still decides, with its own clock
// and its own last-frame time, whether to draw. The simulation does not depend on these
// frames: they only draw and handle input sooner. A second mid hook on the drawing branch
// counts the frames per speed index.
//
// frame_pacing_interval_ms and frame_pacing_input change while the game runs (restart = false, the
// overlay): the thread that changed them stores the new values, and the next check reads them.
//
// With frame_pacing_input = "draw" (the default) frames are skipped only while no input waits: a
// waiting key press, click or wheel turn gets a frame at the next check (it takes the input out of the
// queue), mouse motion the game's own threshold. Input waits in the main thread's Windows queue
// (GetQueueStatus; SDL reads it only when a frame pumps events) or in SDL's own queue (SDL_PeepEvents
// with no buffer counts the events; another plugin's SDL_PushEvent puts them there).
#include "../settings.hpp"
#include "frame_pacing_core.hpp"
#include "modules.hpp"

#include <atomic>
#include <fstream>
#include <string>

namespace {

constexpr std::int32_t kFastest = 4;  // the last entry of the 5-entry table at 0x1C33E18
constexpr int kIndexes = 6;           // 0-4, and 5 for anything else

constexpr int kPeekEvent = 1;                  // SDL_PEEKEVENT
constexpr std::uint32_t kFirstKey = 0x300;     // SDL_KEYDOWN, up to the text events
constexpr std::uint32_t kLastKey = 0x3FF;
constexpr std::uint32_t kMouseMotion = 0x400;  // SDL_MOUSEMOTION
constexpr std::uint32_t kFirstButton = 0x401;  // SDL_MOUSEBUTTONDOWN, SDL_MOUSEBUTTONUP, SDL_MOUSEWHEEL
constexpr std::uint32_t kLastButton = 0x403;
constexpr double kProbeGap = 0.002;            // seconds: the queues are probed at most this often

std::atomic<std::int64_t> g_intervalMs{100};  // 0: keep the game's threshold, count only
std::atomic<float> g_interval{0.1f};          // seconds, the unit of the game's clock in 0x769EA0
std::atomic<bool> g_drawOnInput{true};
std::size_t g_speedOffset = 0, g_lastFrameOffset = 0;
sym::Types::sdl_peep_events::Pointer g_peepEvents = nullptr;
double g_lastProbe = -1.0;  // main thread only: the clock at the last probe and what it found
sb::Waiting g_lastWaiting = sb::Waiting::Nothing;
anvil::Change g_compare, g_render;
volatile LONG64 g_calls[kIndexes] = {}, g_frames[kIndexes] = {};
volatile LONG64 g_replaced = 0, g_inputProbes = 0, g_inputPress = 0, g_inputMotion = 0;
volatile LONG64 g_changes = 0;  // values changed while the game runs
volatile LONG g_dirty = 0;
std::wstring* g_dir = nullptr;

int IndexOf(const anvil::Cpu& cpu) noexcept {
    const auto index = sb::Read<std::int32_t>(reinterpret_cast<const void*>(cpu.rsi), g_speedOffset);
    return index >= 0 && index < kIndexes - 1 ? index : kIndexes - 1;
}

// Called on the main thread, which owns the game's window and its message queue.
int SdlCount(std::uint32_t first, std::uint32_t last) noexcept {
    return g_peepEvents(nullptr, 0, kPeekEvent, first, last);
}

sb::Waiting InputWaiting() noexcept {
    const DWORD queued = HIWORD(GetQueueStatus(QS_KEY | QS_MOUSEMOVE | QS_MOUSEBUTTON));
    if ((queued & (QS_KEY | QS_MOUSEBUTTON)) != 0) return sb::Waiting::Press;
    // One count over keys, motion and buttons; only when something waits, a second one tells motion apart.
    const int sdl = SdlCount(kFirstKey, kLastButton);
    if (sdl > 0) return SdlCount(kMouseMotion, kMouseMotion) < sdl ? sb::Waiting::Press : sb::Waiting::Motion;
    return (queued & QS_MOUSEMOVE) != 0 ? sb::Waiting::Motion : sb::Waiting::Nothing;
}

// At most one probe every kProbeGap on the game's clock; checks in between reuse its answer.
sb::Waiting ProbeInput(double now) noexcept {
    if (now - g_lastProbe < kProbeGap && now >= g_lastProbe) return g_lastWaiting;
    g_lastProbe = now;
    InterlockedIncrement64(&g_inputProbes);
    return g_lastWaiting = InputWaiting();
}

void WriteFile() {
    try {
        const std::wstring path = *g_dir + L"\\frame_pacing.tsv";
        const std::wstring temp = path + L".tmp";
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            out << "key\tvalue\n"
                << "interval_ms\t" << g_intervalMs.load() << "\n"
                << "draw_on_input\t" << (g_drawOnInput.load() ? 1 : 0) << "\n"
                << "changes\t" << g_changes << "\n"
                << "replaced\t" << g_replaced << "\n"
                << "input_probes\t" << g_inputProbes << "\n"
                << "input_press\t" << g_inputPress << "\n"
                << "input_motion\t" << g_inputMotion << "\n";
            for (int i = 0; i < kIndexes; ++i) {
                out << "calls_" << i << "\t" << g_calls[i] << "\n"
                    << "frames_" << i << "\t" << g_frames[i] << "\n";
            }
        }
        MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
    } catch (...) {
    }
}

DWORD WINAPI WriterThread(void*) {
    for (;;) {
        Sleep(5000);
        if (InterlockedExchange(&g_dirty, 0) != 0) WriteFile();
    }
}

}  // namespace

extern "C" {
// requests = threshold checks, hits = thresholds replaced, skips = frames drawn, mode = 1 on.
__declspec(dllexport) sb::Stats EU4SB_FramePacingStats = SB_STATS_INIT;
}

namespace sb {

void SetFramePacingInterval(std::int64_t milliseconds, bool draw_on_input) {
    g_interval.store(static_cast<float>(milliseconds) / 1000.0f, std::memory_order_relaxed);
    g_drawOnInput.store(draw_on_input, std::memory_order_relaxed);
    g_intervalMs.store(milliseconds, std::memory_order_relaxed);
}

void ChangeFramePacing(std::int64_t milliseconds, bool draw_on_input) {
    SetFramePacingInterval(milliseconds, draw_on_input);
    if (EU4SB_FramePacingStats.initialized == 0) return;  // frame_pacing is off: the values wait for its next start
    InterlockedIncrement64(&g_changes);
    InterlockedExchange(&g_dirty, 1);
    Log(ANVIL_LOG_INFO, "frame_pacing: changed while the game runs: interval %lld ms%s",
        static_cast<long long>(milliseconds),
        milliseconds <= 0 ? " (the game's own, counting only)"
                          : (draw_on_input ? ", the game's interval while input waits" : ", also while input waits"));
}

bool InstallFramePacing(anvil::Host& host, Mode mode) {
    if (mode == Mode::Off) return false;
    try {
        g_dir = new std::wstring(host.data_dir().wstring());
    } catch (...) {
        return false;
    }
    g_speedOffset = static_cast<std::size_t>(host.value(sym::nested_frame_threshold, "speed_offset"));
    g_lastFrameOffset = static_cast<std::size_t>(host.value(sym::nested_frame_compare, "last_frame_offset"));
    g_peepEvents = host.function(sym::sdl_peep_events);
    Undo undo;
    g_compare = host.mid_hook(sym::nested_frame_compare, [](anvil::Cpu& cpu) {
        const int index = IndexOf(cpu);
        InterlockedIncrement64(&g_calls[index]);
        InterlockedIncrement64(&EU4SB_FramePacingStats.requests);
        // Longer than the game's own threshold, except a frame at once for a waiting press.
        if (index != kFastest || g_intervalMs.load(std::memory_order_relaxed) <= 0) return;
        const float game = cpu.xmm[6].f32[0];
        // xmm0 = the clock now, [rsi + last_frame_offset] = the time of the last frame (seconds, double).
        const double now = cpu.xmm[0].f64[0];
        const double elapsed = now - sb::Read<double>(reinterpret_cast<const void*>(cpu.rsi), g_lastFrameOffset);
        sb::Waiting waiting = sb::Waiting::Nothing;
        const float threshold = PacedThreshold(game, g_interval.load(std::memory_order_relaxed), elapsed,
                                               g_drawOnInput.load(std::memory_order_relaxed),
                                               [&] { return waiting = ProbeInput(now); });
        if (waiting == sb::Waiting::Press && threshold == 0.0f) InterlockedIncrement64(&g_inputPress);
        if (waiting == sb::Waiting::Motion) InterlockedIncrement64(&g_inputMotion);
        if (threshold == game) return;
        cpu.xmm[6].f32[0] = threshold;
        InterlockedIncrement64(&g_replaced);
        InterlockedIncrement64(&EU4SB_FramePacingStats.hits);
    });
    undo.add(g_compare);
    g_render = host.mid_hook(sym::nested_frame_render, [](anvil::Cpu& cpu) {
        InterlockedIncrement64(&g_frames[IndexOf(cpu)]);
        InterlockedIncrement64(&EU4SB_FramePacingStats.skips);
        InterlockedExchange(&g_dirty, 1);
    });
    undo.add(g_render);
    undo.keep();
    HANDLE thread = CreateThread(nullptr, 0, WriterThread, nullptr, 0, nullptr);
    if (thread != nullptr) {
        SetThreadDescription(thread, L"bellows frame_pacing writer");
        CloseHandle(thread);
    }
    InterlockedExchange(&EU4SB_FramePacingStats.mode, 1);
    InterlockedExchange(&EU4SB_FramePacingStats.initialized, 1);
    if (g_intervalMs.load() > 0) {
        Log(ANVIL_LOG_INFO, "frame_pacing: frames inside the daily tick at most every %lld ms at the fastest speed%s; "
                            "counters in frame_pacing.tsv every 5 s",
            static_cast<long long>(g_intervalMs.load()),
            g_drawOnInput.load() ? ", at the game's interval while input waits" : "");
    } else {
        Log(ANVIL_LOG_INFO, "frame_pacing: the game's own interval, counting only; counters in frame_pacing.tsv every 5 s");
    }
    return true;
}

void ReportFramePacing() {
    if (EU4SB_FramePacingStats.initialized == 0) return;
    bench::Get().set("frame_pacing.interval_ms", g_intervalMs.load());
    bench::Get().set("frame_pacing.draw_on_input", g_drawOnInput.load());
}

}  // namespace sb
