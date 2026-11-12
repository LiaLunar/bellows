// PacedThreshold (frame_pacing): the module's longer interval while nothing waits, the game's own when
// it is longer or mouse motion waits, a frame at once (no sooner than kPressGap) for a waiting press,
// and no probe when the module does not lengthen the threshold or ignores input.
#include "../src/modules/frame_pacing_core.hpp"

#include <cassert>
#include <cstdio>

int main() {
    int probes = 0;
    auto with = [&](sb::Waiting w) {
        return [&probes, w] {
            ++probes;
            return w;
        };
    };

    // Count only (interval 0) or an interval no longer than the game's: the game's own, no probe.
    assert(sb::PacedThreshold(0.05f, 0.0f, 0.2, true, with(sb::Waiting::Press)) == 0.05f);
    assert(sb::PacedThreshold(0.05f, 0.05f, 0.2, true, with(sb::Waiting::Press)) == 0.05f);
    assert(sb::PacedThreshold(0.05f, 0.03f, 0.2, true, with(sb::Waiting::Press)) == 0.05f);
    assert(probes == 0);

    // Nothing waits: the interval. Motion: the game's own. A press: at once, after the gap.
    assert(sb::PacedThreshold(0.05f, 0.1f, 0.06, true, with(sb::Waiting::Nothing)) == 0.1f);
    assert(sb::PacedThreshold(0.05f, 0.1f, 0.02, true, with(sb::Waiting::Motion)) == 0.05f);
    assert(sb::PacedThreshold(0.05f, 0.1f, 0.02, true, with(sb::Waiting::Press)) == 0.0f);
    assert(sb::PacedThreshold(0.05f, 0.1f, sb::kPressGap, true, with(sb::Waiting::Press)) == 0.0f);
    assert(sb::PacedThreshold(0.05f, 0.1f, 0.005, true, with(sb::Waiting::Press)) == 0.1f);
    assert(probes == 5);

    // frame_pacing_input = "wait": the interval, input not probed.
    probes = 0;
    assert(sb::PacedThreshold(0.05f, 0.1f, 0.06, false, with(sb::Waiting::Press)) == 0.1f);
    assert(probes == 0);

    // IntervalFor: index 4 the fastest speed's interval, 0-3 the interval of speeds 1-4, anything else none.
    assert(sb::IntervalFor(4, 0.1f, 0.0f) == 0.1f);
    assert(sb::IntervalFor(0, 0.1f, 0.0f) == 0.0f);
    assert(sb::IntervalFor(3, 0.1f, 0.0f) == 0.0f);
    assert(sb::IntervalFor(0, 0.1f, 0.16f) == 0.16f);
    assert(sb::IntervalFor(3, 0.1f, 0.16f) == 0.16f);
    assert(sb::IntervalFor(4, 0.0f, 0.16f) == 0.0f);
    assert(sb::IntervalFor(5, 0.1f, 0.16f) == 0.0f);
    assert(sb::IntervalFor(-1, 0.1f, 0.16f) == 0.0f);
    // At speeds 1-4 the game's own threshold is 20 ms: 160 ms lengthens it, 10 ms keeps the game's.
    assert(sb::PacedThreshold(0.02f, sb::IntervalFor(2, 0.1f, 0.16f), 0.05, true, with(sb::Waiting::Nothing)) == 0.16f);
    assert(sb::PacedThreshold(0.02f, sb::IntervalFor(2, 0.1f, 0.01f), 0.05, true, with(sb::Waiting::Nothing)) == 0.02f);

    std::printf("frame_pacing ok\n");
    return 0;
}
