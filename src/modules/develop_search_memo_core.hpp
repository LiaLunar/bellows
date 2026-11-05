#pragma once
// develop_search_memo: the memo of one scan of a country's provinces for development (lab docs:
// docs/simulation-profile.md, "AI: szukanie prowincji do rozwoju").
//
// 0x1B5170 walks every province of an AI country and, for each one that passes a check, draws a number, takes it
// modulo 3 as a kind of development, and calls 0xBDE10, which goes through all the provinces of the country and of
// its subjects for the best one of that kind. The AI task only reads game state while it runs (its decisions
// leave as commands), and that search draws no number, so with n provinces the walk does n searches for
// at most three different answers. The memo keeps them for one walk on one thread; the walk still draws
// its numbers, so the random sequence of the game does not change.
#include <cstdint>

namespace sb {

struct DevelopMemo {
    static constexpr int kKinds = 3;
    int depth = 0;  // walks in progress on this thread
    const void* ai = nullptr;
    std::uint64_t best[kKinds] = {};
    bool valid[kKinds] = {};
};

// Entering a walk: a fresh memo; the returned state is restored on leaving.
inline DevelopMemo EnterWalk(DevelopMemo& memo) {
    const DevelopMemo saved = memo;
    memo = DevelopMemo{};
    memo.depth = saved.depth + 1;
    return saved;
}

inline void LeaveWalk(DevelopMemo& memo, const DevelopMemo& saved) { memo = saved; }

enum class DevelopAnswer { Outside, Hit, Miss };

// A search the memo cannot serve (outside a walk, a threshold other than the walk's zero, an unknown kind) is Outside.
inline DevelopAnswer LookupDevelop(const DevelopMemo& memo, const void* ai, int threshold, int kind) {
    if (memo.depth == 0 || threshold != 0 || kind < 0 || kind >= DevelopMemo::kKinds) return DevelopAnswer::Outside;
    return memo.ai == ai && memo.valid[kind] ? DevelopAnswer::Hit : DevelopAnswer::Miss;
}

// Another country on the same thread starts the memo over.
inline void StoreDevelop(DevelopMemo& memo, const void* ai, int kind, std::uint64_t best) {
    if (memo.ai != ai) {
        const int depth = memo.depth;
        memo = DevelopMemo{};
        memo.depth = depth;
        memo.ai = ai;
    }
    memo.best[kind] = best;
    memo.valid[kind] = true;
}

}  // namespace sb
