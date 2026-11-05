// develop_search_memo: the memo lives for one walk on one thread, keyed by the country and the kind, and a nested walk
// gets its own memo.
#include "../src/modules/develop_search_memo_core.hpp"

#include <cassert>
#include <cstdio>

int main() {
    int aiA = 0, aiB = 0;
    sb::DevelopMemo memo;

    // Outside a walk nothing is served.
    assert(sb::LookupDevelop(memo, &aiA, 0, 0) == sb::DevelopAnswer::Outside);

    // The first search of a kind misses, the later ones hit; the kinds do not share an answer.
    const sb::DevelopMemo outer = sb::EnterWalk(memo);
    assert(sb::LookupDevelop(memo, &aiA, 0, 0) == sb::DevelopAnswer::Miss);
    sb::StoreDevelop(memo, &aiA, 0, 0x100);
    assert(sb::LookupDevelop(memo, &aiA, 0, 0) == sb::DevelopAnswer::Hit && memo.best[0] == 0x100);
    assert(sb::LookupDevelop(memo, &aiA, 0, 1) == sb::DevelopAnswer::Miss);
    sb::StoreDevelop(memo, &aiA, 2, 0);  // no province is an answer too
    assert(sb::LookupDevelop(memo, &aiA, 0, 2) == sb::DevelopAnswer::Hit && memo.best[2] == 0);

    // What the memo cannot serve goes to the game.
    assert(sb::LookupDevelop(memo, &aiA, 7, 0) == sb::DevelopAnswer::Outside);
    assert(sb::LookupDevelop(memo, &aiA, 0, 3) == sb::DevelopAnswer::Outside);
    assert(sb::LookupDevelop(memo, &aiA, 0, -1) == sb::DevelopAnswer::Outside);

    // Another country starts over.
    assert(sb::LookupDevelop(memo, &aiB, 0, 0) == sb::DevelopAnswer::Miss);
    sb::StoreDevelop(memo, &aiB, 0, 0x200);
    assert(memo.depth == 1 && sb::LookupDevelop(memo, &aiB, 0, 0) == sb::DevelopAnswer::Hit);
    assert(sb::LookupDevelop(memo, &aiA, 0, 0) == sb::DevelopAnswer::Miss);
    assert(sb::LookupDevelop(memo, &aiB, 0, 2) == sb::DevelopAnswer::Miss);

    // A nested walk starts empty and gives the outer memo back on leaving.
    const sb::DevelopMemo inner = sb::EnterWalk(memo);
    assert(memo.depth == 2 && sb::LookupDevelop(memo, &aiB, 0, 0) == sb::DevelopAnswer::Miss);
    sb::LeaveWalk(memo, inner);
    assert(memo.depth == 1 && sb::LookupDevelop(memo, &aiB, 0, 0) == sb::DevelopAnswer::Hit);

    // Leaving the walk forgets everything; the next one starts empty.
    sb::LeaveWalk(memo, outer);
    assert(sb::LookupDevelop(memo, &aiB, 0, 0) == sb::DevelopAnswer::Outside);
    const sb::DevelopMemo next = sb::EnterWalk(memo);
    assert(sb::LookupDevelop(memo, &aiB, 0, 0) == sb::DevelopAnswer::Miss);
    sb::LeaveWalk(memo, next);

    std::puts("test_develop_search_memo: ok");
    return 0;
}
