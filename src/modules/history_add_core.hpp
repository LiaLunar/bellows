#pragma once

// history_add: the duplicate check of the game's dated list, without the game (lab docs:
// docs/autosave-cost.md, "Co zostaje w Continue").
//
// 0x7E7CD0 adds an entry to a list ordered by date: it first walks the whole list from its head (+0x20,
// nodes { entry, previous, next }) to see whether the entry is in it already, then finds its place from the
// tail, usually at once. A list of n entries costs n * n / 2 node reads. 0xDA66A0, which reads one entry of such
// a list, adds the entry to its owner's list and to one list of all such entries. With the lab's 10 mods that
// is 38,000 adds and 154 million node reads on a save of 1821, 35,000 adds on a save of 1719, and on vanilla 4,300
// adds. The entry was made while reading it, so neither list can hold it yet.

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

}  // namespace sb::history
