#pragma once

// history_add: the duplicate check of the game's dated list, without the game (lab docs:
// docs/autosave-cost.md, "Co zostaje w Continue").
//
// 0x7E7CD0 adds an entry to a list ordered by date: it first walks the whole list from its head (+0x20,
// nodes { entry, previous, next }) to see whether the entry is in it already, then finds its place from the
// tail, usually at once. A list of n entries costs n * n / 2 node reads. 0xDA66A0, which reads one entry of such
// a list, adds the entry to its owner's list and to one list of all such entries. With the lab's 10 mods that
// is 38,000 adds and 154 million node reads on a save of 1821, 35,000 adds on a save of 1719, and on vanilla 4,300
// adds. An entry made while reading it cannot be in either list yet.
//
// The second add has a second way in: 0xDA66A0 given a key that is not a date (0xDA6A0A) adds the owner's last
// entry, which can be an older one the list of all entries holds already, and the game's walk is what stops it from
// going in twice. So that add may skip the walk only for the entry the first add just took (Noted).

#include <cstddef>
#include <cstdint>

namespace sb::history {

inline constexpr std::size_t kListHead = 0x20;
inline constexpr std::size_t kNodeEntry = 0x00;
inline constexpr std::size_t kNodeNext = 0x10;

// What the game's walk finds: is `entry` in the list that starts at `head`? `walked` counts the nodes.
inline bool Contains(const void* head, const void* entry, std::uint64_t& walked) noexcept {
    for (auto* node = static_cast<const unsigned char*>(head); node != nullptr;
         node = *reinterpret_cast<const unsigned char* const*>(node + kNodeNext)) {
        ++walked;
        if (*reinterpret_cast<const void* const*>(node + kNodeEntry) == entry) return true;
    }
    return false;
}

// Which entry the next add may take without the walk, per thread. Plain data: the module's hooks feed it.
struct Noted {
    std::uintptr_t read = 0;   // the entry the first add of 0xDA66A0 took, until the second add
    std::uintptr_t fresh = 0;  // the entry the next call of 0x7E7CD0 may skip the walk for

    // The add to the owner's list, reached only with an entry just allocated.
    void OwnerAdd(std::uintptr_t entry) noexcept {
        read = entry;
        fresh = entry;
    }
    // The add to the list of all entries: `entry` is new only if the owner's add took it just now.
    void GlobalAdd(std::uintptr_t entry) noexcept {
        fresh = entry == read ? entry : 0;
        read = 0;
    }
    // At the walk; true when this call of 0x7E7CD0 adds the noted entry.
    bool TakeWalk(std::uintptr_t entry) noexcept {
        const std::uintptr_t noted = fresh;
        fresh = 0;
        return noted != 0 && noted == entry;
    }
};

}  // namespace sb::history
