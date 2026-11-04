// pops_pacing: when the online-service pump may wait. That the game runs without it is a lab test, not this one.
#include "modules/pops_pacing_core.hpp"

#include <cassert>
#include <cstdio>

using sb::pops::Contexts;
using sb::pops::kContexts;
using sb::pops::kFastest;
using sb::pops::MayWait;

int main() {
    // At the fastest speed in a running game, between two pumps of the context.
    assert(MayWait(false, kFastest, 40.0, 100.0));
    assert(MayWait(false, kFastest, 999.0, 499.0));
    // The interval passed: this call pumps.
    assert(!MayWait(false, kFastest, 40.0, 500.0));
    // Any other speed, a game that stopped ticking (a pause, a menu), multiplayer, no day yet (speed -1).
    assert(!MayWait(false, 3, 40.0, 100.0));
    assert(!MayWait(false, 0, 40.0, 100.0));
    assert(!MayWait(false, kFastest, 1000.0, 100.0));
    assert(!MayWait(true, kFastest, 40.0, 100.0));
    assert(!MayWait(false, -1, 40.0, 100.0));

    int a = 0, b = 0, c = 0, d = 0, e = 0;
    Contexts contexts;
    const std::size_t first = contexts.Slot(&a);
    assert(first == 0);
    assert(contexts.Slot(&b) == 1);
    assert(contexts.Slot(&a) == first);
    // A new context pumps at once: its last pump lies a whole interval back.
    assert(!MayWait(false, kFastest, 40.0, 0.0 - contexts.last[first]));
    assert(contexts.Slot(&c) == 2);
    assert(contexts.Slot(&d) == 3);
    assert(contexts.Slot(&e) == kContexts);  // goes to the game's pump

    std::puts("pops_pacing: ok");
    return 0;
}
