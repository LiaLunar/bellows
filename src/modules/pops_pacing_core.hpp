#pragma once

// pops_pacing: when the game's online-service pump may wait, without the game (lab docs:
// docs/simulation-profile.md, "POPS_RunCallbacks").
//
// The main loop calls POPS_RunCallbacks (pops_api.dll, Paradox's online services) twice a frame:
// from 0x14C5810 and from CPopsWrapper::Update 0x14ADA90. When no socket is ready, the library
// ends that call in Sleep(1) on the game's main thread. At the fastest speed a frame follows each
// game day, so two sleeps a frame are a few percent of the year, spent waiting for nothing. The pump
// only delivers what the service sent; nothing in the simulation waits for it.

#include <cstddef>
#include <cstdint>

namespace sb::pops {

inline constexpr std::int32_t kFastest = 4;    // the game's speed index at the fastest speed (speed 5)
inline constexpr double kIntervalMs = 500.0;   // one pump per context this often while the game runs at it
inline constexpr double kRunningMs = 1000.0;   // no game day for this long: paused or in a menu, so pump every call
inline constexpr std::size_t kContexts = 4;    // the game has two; any other goes to the game's pump

// True when this call of the pump can wait. Hosted or joined multiplayer keeps the game's rate, and so does
// anything but the fastest speed in a running game.
inline bool MayWait(bool multiplayer, std::int32_t speed, double sinceDayMs, double sincePumpMs) noexcept {
    return !multiplayer && speed == kFastest && sinceDayMs < kRunningMs && sincePumpMs < kIntervalMs;
}

// The last pump of each context, in the module's clock (ms).
struct Contexts {
    const void* context[kContexts] = {};
    double last[kContexts] = {};

    // The slot of `which`, taking a free one; kContexts when all are taken.
    std::size_t Slot(const void* which) noexcept {
        for (std::size_t i = 0; i < kContexts; ++i) {
            if (context[i] == which) return i;
            if (context[i] == nullptr) {
                context[i] = which;
                last[i] = -kIntervalMs;
                return i;
            }
        }
        return kContexts;
    }
};

}  // namespace sb::pops
