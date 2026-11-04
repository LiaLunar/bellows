// army_search_memo: the memo lives for one search on one thread, keyed by the army, and a nested search
// gets its own memo.
#include "../src/modules/army_search_memo_core.hpp"

#include <cassert>
#include <cstdio>

int main() {
    int armyA = 0, armyB = 0;
    sb::SearchMemo memo;

    // Outside a search nothing is remembered.
    assert(sb::Lookup(memo, &armyA) == sb::MemoAnswer::Outside);
    sb::Store(memo, &armyA, 1);
    assert(!memo.valid && sb::Lookup(memo, &armyA) == sb::MemoAnswer::Outside);

    // The first call of a search misses, the later ones for the same army hit.
    const sb::SearchMemo outer = sb::EnterSearch(memo);
    assert(sb::Lookup(memo, &armyA) == sb::MemoAnswer::Miss);
    sb::Store(memo, &armyA, 1);
    assert(sb::Lookup(memo, &armyA) == sb::MemoAnswer::Hit && memo.value == 1);
    // Another army misses and replaces it.
    assert(sb::Lookup(memo, &armyB) == sb::MemoAnswer::Miss);
    sb::Store(memo, &armyB, 0);
    assert(sb::Lookup(memo, &armyB) == sb::MemoAnswer::Hit && memo.value == 0);
    assert(sb::Lookup(memo, &armyA) == sb::MemoAnswer::Miss);
    sb::Store(memo, &armyA, 1);

    // A nested search starts empty and gives the outer memo back on leaving.
    const sb::SearchMemo inner = sb::EnterSearch(memo);
    assert(memo.depth == 2 && sb::Lookup(memo, &armyA) == sb::MemoAnswer::Miss);
    sb::Store(memo, &armyA, 0);
    sb::LeaveSearch(memo, inner);
    assert(memo.depth == 1 && sb::Lookup(memo, &armyA) == sb::MemoAnswer::Hit && memo.value == 1);

    // Leaving the outer search forgets everything; the next search starts empty.
    sb::LeaveSearch(memo, outer);
    assert(sb::Lookup(memo, &armyA) == sb::MemoAnswer::Outside);
    const sb::SearchMemo next = sb::EnterSearch(memo);
    assert(sb::Lookup(memo, &armyA) == sb::MemoAnswer::Miss);
    sb::LeaveSearch(memo, next);

    std::printf("army_search_memo: memo scoped to one search\n");
    return 0;
}
