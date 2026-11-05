// history_add: the walk it compares with, on stand-ins with the game's node layout, and which adds may skip it. That
// the game adds the same list is a lab test ("verify"), not this one.
#include "modules/history_add_core.hpp"

#include <cassert>
#include <cstdio>

namespace {

struct Node {
    const void* entry;
    Node* previous;
    Node* next;
};
static_assert(offsetof(Node, entry) == sb::history::kNodeEntry);
static_assert(offsetof(Node, next) == sb::history::kNodeNext);

}  // namespace

int main() {
    int a = 0, b = 0, c = 0;
    Node last{&c, nullptr, nullptr};
    Node first{&a, nullptr, &last};
    last.previous = &first;

    std::uint64_t walked = 0;
    assert(sb::history::Contains(&first, &a, walked) && walked == 1);
    walked = 0;
    assert(sb::history::Contains(&first, &c, walked) && walked == 2);
    walked = 0;
    assert(!sb::history::Contains(&first, &b, walked) && walked == 2);
    walked = 0;
    assert(!sb::history::Contains(nullptr, &a, walked) && walked == 0);

    // A dated entry: both adds of the new entry skip the walk.
    sb::history::Noted noted;
    noted.OwnerAdd(0x10);
    assert(noted.TakeWalk(0x10));
    noted.GlobalAdd(0x10);
    assert(noted.TakeWalk(0x10));
    // A key that is no date: the second add alone, with the owner's last entry, keeps the game's walk.
    noted.GlobalAdd(0x10);
    assert(!noted.TakeWalk(0x10));
    noted.GlobalAdd(0x20);
    assert(!noted.TakeWalk(0x20));
    // Another add between the note and the walk leaves that walk to the game, and the note is gone.
    noted.OwnerAdd(0x30);
    assert(!noted.TakeWalk(0x40));
    assert(!noted.TakeWalk(0x30));
    // Nothing noted: an entry of 0 is no entry.
    assert(!noted.TakeWalk(0));

    std::puts("history_add: ok");
    return 0;
}
