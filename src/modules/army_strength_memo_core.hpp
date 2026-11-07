#pragma once
// army_strength_memo: the memo of one AI army search (lab docs: docs/simulation-hotspots-after-142.md).
//
// 0x1022B0 walks the province graph from an army outwards. It closes one province at a time and asks
// 0x102140 for the army's strength against each neighbour that is still open, so a province with five
// closed neighbours is asked about up to five times. The answer depends on the army, the province, two
// flag bytes and game state that the AI task only reads, so within one search the repeated calls can have
// the answer of the first.
//
// The memo has one slot per province id, 8 bytes each. A slot is valid for one search: its stamp holds the
// search number and the two flags, so starting a search costs nothing and a slot with other flags is a miss.
// The slots are memory the caller owns: the module takes them from VirtualAlloc, off the game's heap.
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sb::strength {

constexpr std::int32_t kSlots = 0x10000;          // province ids are 16 bits in the game
constexpr std::uint32_t kSearchLimit = 1u << 30;  // the stamp has two bits for the flags

struct Slot {
    std::uint32_t stamp;
    float value;
};

struct Memo {
    Slot* slots = nullptr;  // kSlots zeroed slots
    const void* army = nullptr;
    std::uint32_t search = 0;
};

inline void BeginSearch(Memo& memo, const void* army) noexcept {
    memo.army = army;
    if (++memo.search == kSearchLimit) {
        if (memo.slots != nullptr) std::memset(memo.slots, 0, sizeof(Slot) * static_cast<std::size_t>(kSlots));
        memo.search = 1;
    }
}

// The slot for this call, or nullptr when the memo cannot answer it: no search of this army is running on
// this thread, the id is out of range, or a flag is not a plain bool.
inline Slot* SlotFor(const Memo& memo, const void* army, std::int32_t province, std::uint8_t a4, std::uint8_t a5) noexcept {
    if (memo.slots == nullptr || memo.search == 0 || memo.army != army) return nullptr;
    if (province < 0 || province >= kSlots || a4 > 1 || a5 > 1) return nullptr;
    return memo.slots + province;
}

inline std::uint32_t StampOf(const Memo& memo, std::uint8_t a4, std::uint8_t a5) noexcept {
    return (memo.search << 2) | (static_cast<std::uint32_t>(a4) << 1) | a5;
}

// Bit for bit: the answer is a float the game compares and adds, and a memo must not round it.
inline bool SameAnswer(float a, float b) noexcept { return std::memcmp(&a, &b, sizeof a) == 0; }

}  // namespace sb::strength
