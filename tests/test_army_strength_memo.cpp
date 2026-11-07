// army_strength_memo: a slot answers only within one search of one army, with the same flags.
#include "../src/modules/army_strength_memo_core.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

namespace strength = sb::strength;

int main() {
    std::vector<strength::Slot> slots(strength::kSlots, strength::Slot{0, 0.0f});
    strength::Memo memo;
    int armyA = 0, armyB = 0;

    // Without a search, or without memory, there is no slot.
    assert(strength::SlotFor(memo, &armyA, 5, 0, 0) == nullptr);
    memo.slots = slots.data();
    assert(strength::SlotFor(memo, &armyA, 5, 0, 0) == nullptr);

    strength::BeginSearch(memo, &armyA);
    strength::Slot* slot = strength::SlotFor(memo, &armyA, 5, 1, 0);
    assert(slot == slots.data() + 5);
    const std::uint32_t stamp = strength::StampOf(memo, 1, 0);
    assert(slot->stamp != stamp);  // the first call of a search misses
    slot->stamp = stamp;
    slot->value = 2.5f;
    assert(strength::SlotFor(memo, &armyA, 5, 1, 0)->stamp == stamp);

    // Other flags are another question: the same slot, another stamp.
    assert(strength::StampOf(memo, 0, 0) != stamp);
    assert(strength::StampOf(memo, 1, 1) != stamp);
    assert(strength::StampOf(memo, 0, 1) != stamp);

    // Another army does not reach the memo, ids and flags out of range neither.
    assert(strength::SlotFor(memo, &armyB, 5, 1, 0) == nullptr);
    assert(strength::SlotFor(memo, &armyA, -1, 1, 0) == nullptr);
    assert(strength::SlotFor(memo, &armyA, strength::kSlots, 1, 0) == nullptr);
    assert(strength::SlotFor(memo, &armyA, strength::kSlots - 1, 1, 0) != nullptr);
    assert(strength::SlotFor(memo, &armyA, 5, 2, 0) == nullptr);
    assert(strength::SlotFor(memo, &armyA, 5, 0, 2) == nullptr);

    // The next search, also of the same army, finds nothing.
    strength::BeginSearch(memo, &armyA);
    assert(strength::SlotFor(memo, &armyA, 5, 1, 0)->stamp != strength::StampOf(memo, 1, 0));

    // When the search number runs out the slots start again empty, and no old stamp comes back.
    memo.search = strength::kSearchLimit - 2;
    strength::BeginSearch(memo, &armyB);
    slots[7].stamp = strength::StampOf(memo, 0, 0);
    strength::BeginSearch(memo, &armyB);
    assert(memo.search == 1 && slots[7].stamp == 0 && strength::StampOf(memo, 0, 0) != 0);

    // Answers compare by their bits.
    assert(strength::SameAnswer(1.5f, 1.5f));
    assert(!strength::SameAnswer(0.0f, -0.0f));
    assert(strength::SameAnswer(std::nanf(""), std::nanf("")));

    std::puts("army_strength_memo ok");
    return 0;
}
