#pragma once

// modifier_merge (lab docs/simulation-profile.md, "Tick miesięczny"): 0x8EA420 adds the (id, value)
// entries of one modifier list to another. A list keeps its entries as int32 pairs at +0x58, the
// capacity at +0x60, the count at +0x64, and a byte at +0x10 that marks a dense list, where the entry
// of each static id 0..0x285 sits at index id. For every source entry the game
//   - reads the value (from the source's slot `id` when the source is dense and id <= 0x286),
//   - skips id 0x286,
//   - into a dense destination adds a static id in place (and writes the id into that slot),
//   - otherwise scans the whole destination for the first entry with that id and adds to it, or
//     appends the entry, doubling the capacity when it is full,
// and at the end records the source in a vector at +0x38. The scan makes one call O(n * m): the
// monthly ranking of the countries counts every fleet's cannons, which adds the country's dense list
// (about 700 entries) to the fleet's sparse list.
//
// MergeFast does the same with an index of the destination's ids, so each lookup is O(1), and gives the
// same entries in the same order, the same values, count and capacity, and the same sequence of
// capacity changes. When an input is outside what the index covers it returns false before changing
// anything, and the caller runs the game's function. MergeReference is the game's loop written out,
// for the tests and the verify mode. Plain data, no Windows calls.

#include <cstdint>
#include <vector>

namespace sb {

constexpr std::size_t kModDense = 0x10;
constexpr std::size_t kModEntries = 0x58;
constexpr std::size_t kModCapacity = 0x60;
constexpr std::size_t kModCount = 0x64;
constexpr std::int32_t kModStaticEnd = 0x286;  // static ids are 0..0x285; id 0x286 is skipped
constexpr std::int32_t kModMaxId = 1 << 16;    // ids the index covers

// The source as the game reads it: the count once at the start, the entries as they are.
struct ModSource {
    bool dense = false;
    const std::int32_t* entries = nullptr;
    std::int32_t count = 0;
};

// Index of a destination's ids for one call: slot of the first entry with each id, valid when the
// stamp equals the current generation. One per thread.
struct ModIndex {
    std::vector<std::uint32_t> stamp = std::vector<std::uint32_t>(kModMaxId, 0);
    std::vector<std::int32_t> slot = std::vector<std::int32_t>(kModMaxId, 0);
    std::uint32_t generation = 0;

    void Next() {
        if (++generation == 0) {
            std::fill(stamp.begin(), stamp.end(), 0u);
            generation = 1;
        }
    }
};

inline std::int32_t Add32(std::int32_t a, std::int32_t b) noexcept {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}

inline std::int32_t SourceValue(const ModSource& src, std::int32_t i, std::int32_t id) noexcept {
    if (src.dense && id <= kModStaticEnd) return src.entries[2 * static_cast<std::int64_t>(id) + 1];
    return src.entries[2 * static_cast<std::int64_t>(i) + 1];
}

// Appends (id, value) to the destination, growing it the way the game does. L has
// entries(), count(), capacity(), set_count(n) and grow(new_capacity), which moves the entries.
template <class L> void Append(L& dest, std::int32_t id, std::int32_t value) {
    const std::int32_t count = dest.count();
    if (count + 1 > dest.capacity()) {
        const std::int32_t doubled = dest.capacity() * 2;
        dest.grow(count + 1 > doubled ? count + 1 : doubled);
    }
    std::int32_t* e = dest.entries();
    e[2 * static_cast<std::int64_t>(count)] = id;
    e[2 * static_cast<std::int64_t>(count) + 1] = value;
    dest.set_count(count + 1);
}

// The game's loop (without recording the source). The source must not be the destination.
template <class L> void MergeReference(L& dest, const ModSource& src) {
    for (std::int32_t i = 0; i < src.count; ++i) {
        const std::int32_t id = src.entries[2 * static_cast<std::int64_t>(i)];
        const std::int32_t value = SourceValue(src, i, id);
        if (id == kModStaticEnd) continue;
        if (dest.dense() && id < kModStaticEnd) {
            std::int32_t* e = dest.entries();
            e[2 * static_cast<std::int64_t>(id)] = id;
            e[2 * static_cast<std::int64_t>(id) + 1] = Add32(e[2 * static_cast<std::int64_t>(id) + 1], value);
            continue;
        }
        std::int32_t* e = dest.entries();
        const std::int32_t count = dest.count();
        std::int32_t j = 0;
        while (j < count && e[2 * static_cast<std::int64_t>(j)] != id) ++j;
        if (j < count) {
            e[2 * static_cast<std::int64_t>(j) + 1] = Add32(e[2 * static_cast<std::int64_t>(j) + 1], value);
        } else {
            Append(dest, id, value);
        }
    }
}

// Whether MergeFast covers this call: every source id in [0, kModMaxId) (a dense destination then
// never gets an in-place write at a negative slot), and in a dense destination no slot below 0x286
// holding an id the scans could look for (above 0x286; 0x286 itself is never looked for). Reads only.
template <class L> bool MergeFastApplies(const L& dest, const ModSource& src) {
    if (src.entries == nullptr || src.count <= 0) return false;
    for (std::int32_t i = 0; i < src.count; ++i) {
        const std::int32_t id = src.entries[2 * static_cast<std::int64_t>(i)];
        if (id < 0 || id >= kModMaxId) return false;
    }
    if (dest.dense()) {
        const std::int32_t* e = dest.entries();
        const std::int32_t end = dest.count() < kModStaticEnd ? dest.count() : kModStaticEnd;
        for (std::int32_t j = 0; j < end; ++j) {
            if (e[2 * static_cast<std::int64_t>(j)] > kModStaticEnd) return false;
        }
    }
    return true;
}

// The first source id outside [0, kModMaxId), for the module's counters.
inline bool FirstUncoveredId(const ModSource& src, std::int32_t* id) {
    for (std::int32_t i = 0; i < src.count; ++i) {
        const std::int32_t v = src.entries[2 * static_cast<std::int64_t>(i)];
        if (v < 0 || v >= kModMaxId) return *id = v, true;
    }
    return false;
}

// The first id above 0x286 in a dense destination's slots below 0x286, for the module's counters.
template <class L> bool FirstDynamicStaticSlot(const L& dest, std::int32_t* id) {
    const std::int32_t* e = dest.entries();
    const std::int32_t end = dest.count() < kModStaticEnd ? dest.count() : kModStaticEnd;
    for (std::int32_t j = 0; j < end; ++j) {
        if (e[2 * static_cast<std::int64_t>(j)] > kModStaticEnd) return *id = e[2 * static_cast<std::int64_t>(j)], true;
    }
    return false;
}

// Upper bound of the entries the game's scans would compare (each scan looks at the whole destination,
// which grows by at most one per scanned entry): the caller compares it with a threshold, below which
// the game's own loop is as fast.
template <class L> std::int64_t ScanWork(const L& dest, const ModSource& src) {
    std::int64_t scanned = 0;
    for (std::int32_t i = 0; i < src.count; ++i) {
        const std::int32_t id = src.entries[2 * static_cast<std::int64_t>(i)];
        if (id != kModStaticEnd && (!dest.dense() || id >= kModStaticEnd)) ++scanned;
    }
    return scanned * dest.count() + scanned * (scanned - 1) / 2;
}

// MergeReference with an index. Call only when MergeFastApplies.
template <class L> void MergeFast(L& dest, const ModSource& src, ModIndex& index) {
    index.Next();
    const std::uint32_t gen = index.generation;
    {
        const std::int32_t* e = dest.entries();
        const std::int32_t count = dest.count();
        for (std::int32_t j = 0; j < count; ++j) {
            const std::int32_t id = e[2 * static_cast<std::int64_t>(j)];
            if (id < 0 || id >= kModMaxId || index.stamp[id] == gen) continue;  // only the first entry counts
            index.stamp[id] = gen;
            index.slot[id] = j;
        }
    }
    for (std::int32_t i = 0; i < src.count; ++i) {
        const std::int32_t id = src.entries[2 * static_cast<std::int64_t>(i)];
        const std::int32_t value = SourceValue(src, i, id);
        if (id == kModStaticEnd) continue;
        std::int32_t* e = dest.entries();
        if (dest.dense() && id < kModStaticEnd) {
            // Slots below 0x286 hold static ids or 0x286 (MergeFastApplies), which the scans never look for.
            e[2 * static_cast<std::int64_t>(id)] = id;
            e[2 * static_cast<std::int64_t>(id) + 1] = Add32(e[2 * static_cast<std::int64_t>(id) + 1], value);
            continue;
        }
        if (index.stamp[id] == gen) {
            const std::int64_t j = index.slot[id];
            e[2 * j + 1] = Add32(e[2 * j + 1], value);
            continue;
        }
        const std::int32_t at = dest.count();
        Append(dest, id, value);
        index.stamp[id] = gen;
        index.slot[id] = at;
    }
}

// A destination in a std::vector, with the game's capacity rule; for the tests and the verify mode.
struct VectorModList {
    bool is_dense = false;
    std::vector<std::int32_t> data;  // 2 * capacity ints
    std::int32_t n = 0;
    std::vector<std::int32_t> growths;  // capacities in the order grow() set them

    bool dense() const { return is_dense; }
    std::int32_t* entries() { return data.data(); }
    const std::int32_t* entries() const { return data.data(); }
    std::int32_t count() const { return n; }
    std::int32_t capacity() const { return static_cast<std::int32_t>(data.size() / 2); }
    void set_count(std::int32_t c) { n = c; }
    void grow(std::int32_t cap) {
        data.resize(2 * static_cast<std::size_t>(cap), 0);
        growths.push_back(cap);
    }
};

}  // namespace sb
