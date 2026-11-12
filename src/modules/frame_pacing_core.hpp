#pragma once

// frame_pacing's choice of threshold for one check of 0x769EA0. Plain data; the
// module passes the registers and a probe of the input queues, a test passes numbers.

namespace sb {

// What waits for the game's next frame.
enum class Waiting { Nothing, Motion, Press };

// Shortest time between two frames drawn for a waiting press (a frame takes the press out of the queue).
constexpr double kPressGap = 0.010;

// The speed index at +0x1E08 of the fastest speed: the last entry of the 5-entry table at 0x1C33E18.
constexpr int kFastestIndex = 4;

// The module's interval for one speed index: frame_pacing_interval_ms at the fastest speed,
// frame_pacing_slow_interval_ms at speeds 1-4 (indexes 0-3), none (0) for any other index. 0 keeps the
// game's own threshold.
constexpr float IntervalFor(int index, float fastest, float slow) noexcept {
    return index == kFastestIndex ? fastest : (index >= 0 && index < kFastestIndex ? slow : 0.0f);
}

// The game's own threshold unless the module's interval is longer. With draw_on_input, while input
// waits: a key press, click or wheel turn gets a frame at once (at most every kPressGap, the only case
// where the module draws sooner than the game), mouse motion the game's own threshold. waiting() runs
// only when the module's interval is longer and at most once per check.
template <class InputWaiting>
float PacedThreshold(float game, float interval, double elapsed, bool draw_on_input, InputWaiting&& waiting) {
    if (!(interval > game)) return game;
    if (!draw_on_input) return interval;
    switch (waiting()) {
    case Waiting::Press:
        return elapsed >= kPressGap ? 0.0f : interval;
    case Waiting::Motion:
        return game;
    default:
        return interval;
    }
}

}  // namespace sb
