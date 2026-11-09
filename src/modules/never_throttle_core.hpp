#pragma once

// never_throttle: when the game's main loop may skip its own waits, without the game (lab docs:
// docs/never-throttle-minimized.md and docs/anbennartools-gaps.md, §1; bellows#159).
//
// The frame function of the main loop (0x14C25A0) waits in SDL_Delay when the graphics device is not usable,
// which is the case while the window is minimized: 100 ms a frame. A flag of the application object (+0x32B,
// set by the game only for the console command `timer start`) skips that wait and the 50 ms one of a game in the
// background. At the fastest speed a minimized game then advances as fast as the remaining work allows; with the
// window visible both waits are 0 ms already, so the flag changes nothing there.
//
// The module sets the flag only while a single-player game runs at the fastest speed, and clears it as soon as
// that stops: a pause, a menu, another speed, multiplayer. It never clears a flag it did not set.

#include <cstdint>

namespace sb::never_throttle {

inline constexpr std::int32_t kFastest = 4;   // the game's speed index at the fastest speed (speed 5)
inline constexpr double kRunningMs = 1000.0;  // no game day for this long: paused or in a menu

// True while the flag should be set: a single-player game that ran a day at the fastest speed less than
// kRunningMs ago. speed is -1 before the first day.
inline bool Wanted(bool multiplayer, std::int32_t speed, double sinceDayMs) noexcept {
    return !multiplayer && speed == kFastest && sinceDayMs < kRunningMs;
}

// What to write to the flag once a frame. Step returns 1 or 0 to write, or -1 to leave the flag as it is.
struct Owner {
    bool owned = false;  // the flag is 1 because the module wrote it

    int Step(bool wanted, std::uint8_t current) noexcept {
        if (wanted) {
            if (current != 0) return -1;  // already set: by the module, or by the game's `timer start`
            owned = true;                 // 0: not yet set, or cleared since (`timer stop`)
            return 1;
        }
        if (!owned) return -1;  // never clear what the game set itself
        owned = false;
        return current != 0 ? 0 : -1;
    }
};

}  // namespace sb::never_throttle
