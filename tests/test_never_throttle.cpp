// never_throttle: when the flag is wanted, and that the module only clears a flag it set. The effect on a minimized
// game is a lab measurement, not this test.
#include "modules/never_throttle_core.hpp"

#include <cassert>
#include <cstdio>

using sb::never_throttle::kFastest;
using sb::never_throttle::kRunningMs;
using sb::never_throttle::Owner;
using sb::never_throttle::Wanted;

int main() {
    // A single-player game that ran a day at the fastest speed a moment ago.
    assert(Wanted(false, kFastest, 0.0));
    assert(Wanted(false, kFastest, kRunningMs - 1.0));
    // A pause or a menu (no day for kRunningMs), another speed, no day yet, multiplayer.
    assert(!Wanted(false, kFastest, kRunningMs));
    assert(!Wanted(false, 3, 10.0));
    assert(!Wanted(false, 0, 10.0));
    assert(!Wanted(false, -1, 10.0));
    assert(!Wanted(true, kFastest, 10.0));

    {
        // Set once, left alone while wanted, cleared once when no longer wanted.
        Owner o;
        assert(o.Step(true, 0) == 1 && o.owned);
        assert(o.Step(true, 1) == -1 && o.owned);
        assert(o.Step(false, 1) == 0 && !o.owned);
        assert(o.Step(false, 0) == -1);
    }
    {
        // The game set the flag itself (`timer start`): the module neither takes it over nor clears it.
        Owner o;
        assert(o.Step(true, 1) == -1 && !o.owned);
        assert(o.Step(false, 1) == -1);
    }
    {
        // Cleared behind the module's back (`timer stop`) while wanted: set again; once not wanted, nothing to clear.
        Owner o;
        assert(o.Step(true, 0) == 1);
        assert(o.Step(true, 0) == 1 && o.owned);
        assert(o.Step(false, 0) == -1 && !o.owned);
    }

    std::puts("never_throttle: ok");
    return 0;
}
