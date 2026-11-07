#pragma once

// monthly_probe helpers (lab docs/simulation-profile.md, "Tick miesięczny"): plain data, no Windows
// calls, so a test can feed them numbers.
//
// 0x8EA420 adds the (id, value) entries of one modifier list to another. A list keeps its entries as
// int pairs at +0x58, their count at +0x64, and a byte at +0x10 that marks a dense list: there the
// entry of each static id 0..0x285 sits at index id. Into a dense list a static id is added in place;
// every other id (and every id into a sparse list) is looked up by a linear scan of the whole list and
// appended when missing. Id 0x286 is skipped.

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace sb {

constexpr std::size_t kModDense = 0x10;
constexpr std::size_t kModEntries = 0x58;
constexpr std::size_t kModCount = 0x64;
constexpr std::int32_t kModStaticEnd = 0x286;  // static ids are 0..0x285; 0x286 is skipped

struct MergeShape {
    bool dest_dense = false;
    bool src_dense = false;
    std::int32_t dest_count = 0;
    std::int32_t src_count = 0;
    std::int32_t scanned = 0;  // source entries that take the linear scan of the destination
};

// The shape of one 0x8EA420(dest, src) call, read before the call. `entries` is the source's pair
// array (id, value), `src_count` entries long.
inline MergeShape ShapeOfMerge(bool dest_dense, std::int32_t dest_count, bool src_dense, const std::int32_t* entries,
                               std::int32_t src_count) noexcept {
    MergeShape s{dest_dense, src_dense, dest_count, src_count, 0};
    for (std::int32_t i = 0; i < src_count; ++i) {
        const std::int32_t id = entries[2 * i];
        if (id == kModStaticEnd) continue;
        if (!dest_dense || id >= kModStaticEnd) ++s.scanned;
    }
    return s;
}

// Upper bound of the entries the linear scans of one call compare (each scan looks at the whole
// destination, which grows by at most one per scanned entry).
inline std::int64_t ScanSteps(const MergeShape& s) noexcept {
    const std::int64_t n = s.dest_count, k = s.scanned;
    return k * n + k * (k - 1) / 2;
}

// One monthly tick, in timer ticks. The parts are serial on the main thread, so their sum and the
// total differ only by what the tick does around them.
struct MonthRow {
    std::int32_t date = 0;  // the game's raw date on entry
    std::int64_t total = 0, country = 0, province = 0, units = 0, port = 0, merge = 0;
    std::int64_t mergeSteps = 0;
    std::int32_t countries = 0;
    std::int32_t topCountry = -1;  // tag index of the slowest country update
    std::int64_t topCountryTicks = 0;
    std::int64_t tagLists = 0, tagListsWalk = 0;  // the double loop, and a read-only single pass after it
    std::int64_t tagListEntries = 0, tagListMismatches = 0;
};

// The month row's date: the game's date is hours since year -5000, in years of 365 days.
inline void DateText(std::int32_t raw, char* out, std::size_t size) noexcept {
    static constexpr int kMonths[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const std::int64_t day = (static_cast<std::int64_t>(raw) - 43'800'000) / 24;
    int rest = static_cast<int>(((day % 365) + 365) % 365), month = 0;
    const std::int64_t year = (day - rest) / 365;
    while (month < 11 && rest >= kMonths[month]) rest -= kMonths[month++];
    std::snprintf(out, size, "%lld.%d.%d", static_cast<long long>(year), month + 1, rest + 1);
}

// The monthly tick's double loop over countries (0x75DD79..0x75DE0F on Steam, inline in the tick):
// for every country A and every country B it takes A's list for B's tag index from a per-tag array
// of lists at A+0x1800 (data pointer; count at +0x180C, 0x18 bytes a list: data +0x8, count +0x14)
// and copies the byte at +0x24 of every entry to its +0x25. The pairs (A, B) cost countries²
// look-ups even when the lists are empty. The same entries are reached once per tag index that some
// country has: WalkPerTagLists walks them that way and only reads, so it tells what a single pass
// would cost and, run after the game's loop, whether every entry it reaches holds +0x25 == +0x24.
constexpr std::size_t kCountryTagIndex = 0x20;  // the tag index is bits 32-47 of this qword
constexpr std::size_t kTagLists = 0x1800;
constexpr std::size_t kTagListCount = 0x180C;
constexpr std::size_t kTagListStride = 0x18;
constexpr std::size_t kTagListData = 0x8;
constexpr std::size_t kTagListLength = 0x14;
constexpr std::size_t kEntrySource = 0x24;
constexpr std::size_t kEntryCopy = 0x25;
constexpr int kTagIndices = 0x8000;  // the tag index is an int16

struct TagListWalk {
    std::int64_t countries = 0;
    std::int64_t pairs = 0;       // (A, B) look-ups the game's loop makes
    std::int64_t lists = 0;       // lists a single pass looks at
    std::int64_t entries = 0;     // entries a single pass reaches
    std::int64_t mismatches = 0;  // entries with +0x25 != +0x24
};

template <class T>
inline T ReadAt(const unsigned char* base, std::size_t offset) noexcept {
    T v;
    std::memcpy(&v, base + offset, sizeof v);
    return v;
}

// `countries` is the game's array of country pointers, `count` long. `present` is scratch space of
// kTagIndices bytes.
inline TagListWalk WalkPerTagLists(const unsigned char* const* countries, std::int64_t count,
                                   unsigned char* present) noexcept {
    TagListWalk w;
    w.countries = count;
    w.pairs = count * count;
    std::memset(present, 0, kTagIndices);
    int top = 0;
    for (std::int64_t i = 0; i < count; ++i) {
        const auto index = static_cast<std::int16_t>(ReadAt<std::uint64_t>(countries[i], kCountryTagIndex) >> 32);
        if (index < 0) continue;
        present[index] = 1;
        if (index + 1 > top) top = index + 1;
    }
    for (std::int64_t i = 0; i < count; ++i) {
        const unsigned char* a = countries[i];
        const std::int32_t lists = ReadAt<std::int32_t>(a, kTagListCount);
        const auto* data = ReadAt<const unsigned char*>(a, kTagLists);
        const std::int32_t end = lists < top ? lists : top;
        for (std::int32_t j = 0; j < end; ++j) {
            if (!present[j]) continue;
            ++w.lists;
            const unsigned char* list = data + static_cast<std::size_t>(j) * kTagListStride;
            const std::int32_t n = ReadAt<std::int32_t>(list, kTagListLength);
            if (n == 0) continue;
            const auto* const* items = ReadAt<const unsigned char* const*>(list, kTagListData);
            for (std::int32_t k = 0; k < n; ++k) {
                ++w.entries;
                if (items[k][kEntryCopy] != items[k][kEntrySource]) ++w.mismatches;
            }
        }
    }
    return w;
}

// log2 bucket 0..15 of a count (0 and 1 in bucket 0).
inline int Bucket(std::int64_t v) noexcept {
    int b = 0;
    while (v > 1 && b < 15) {
        v >>= 1;
        ++b;
    }
    return b;
}

}  // namespace sb
