#pragma once
// army_search_memo: the memo of one AI army search (lab docs: docs/simulation-profile.md, "AI army search:
// the zone-of-control flag").
//
// 0x1022B0 searches the province graph for one army on one thread. For each move it checks, 0x96F860 calls
// 0xD45250 (is the move blocked by a fort's zone of control?), which first asks 0xD40AE0(army, 0xE0, 0):
// does modifier 0xE0 apply to the army, from its leader, its country's cached modifiers or its regiments.
// That answer depends only on the army, and the AI task only reads game state while it runs (its decisions
// leave as commands), so within one search it is the same for every move. The memo keeps the last answer
// for the duration of the search; a search entered inside another one gets its own memo.
#include <cstdint>

namespace sb {

struct SearchMemo {
    int depth = 0;  // searches in progress on this thread
    const void* army = nullptr;
    std::uint8_t value = 0;
    bool valid = false;
    // The army's speed modifier 0xFB from 0xD40790, the same way.
    const void* speedArmy = nullptr;
    int speed = 0;
    bool speedValid = false;
};

// Entering a search: a fresh memo; the returned state is restored on leaving.
inline SearchMemo EnterSearch(SearchMemo& memo) {
    const SearchMemo saved = memo;
    memo = SearchMemo{};
    memo.depth = saved.depth + 1;
    return saved;
}

inline void LeaveSearch(SearchMemo& memo, const SearchMemo& saved) { memo = saved; }

enum class MemoAnswer { Outside, Hit, Miss };

inline MemoAnswer Lookup(const SearchMemo& memo, const void* army) {
    if (memo.depth == 0) return MemoAnswer::Outside;
    return memo.valid && memo.army == army ? MemoAnswer::Hit : MemoAnswer::Miss;
}

inline MemoAnswer LookupSpeed(const SearchMemo& memo, const void* army) {
    if (memo.depth == 0) return MemoAnswer::Outside;
    return memo.speedValid && memo.speedArmy == army ? MemoAnswer::Hit : MemoAnswer::Miss;
}

inline void StoreSpeed(SearchMemo& memo, const void* army, int speed) {
    if (memo.depth == 0) return;
    memo.speedArmy = army;
    memo.speed = speed;
    memo.speedValid = true;
}

inline void Store(SearchMemo& memo, const void* army, std::uint8_t value) {
    if (memo.depth == 0) return;
    memo.army = army;
    memo.value = value;
    memo.valid = true;
}

}  // namespace sb
