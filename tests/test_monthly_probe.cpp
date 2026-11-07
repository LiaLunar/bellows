// ShapeOfMerge and ScanSteps (monthly_probe counters): which source entries of a modifier merge take the
// linear scan of the destination, and the bound on the entries those scans compare.
#include "../src/modules/monthly_probe_core.hpp"

#include <cassert>
#include <cstdio>

static void TestDateText() {
    char text[32];
    sb::DateText(43'800'000 + (1821 * 365 + 31 + 27) * 24, text, sizeof text);  // 28 February 1821
    assert(std::strcmp(text, "1821.2.28") == 0);
    sb::DateText(43'800'000 + 1444 * 365 * 24 + 24, text, sizeof text);
    assert(std::strcmp(text, "1444.1.2") == 0);
}

// Countries laid out as the tick's double loop reads them (WalkPerTagLists). The game's loop is
// replayed as written, over every pair, and the single pass must reach the same entries.
struct FakeList {
    unsigned char bytes[sb::kTagListStride] = {};
};
struct FakeCountry {
    unsigned char bytes[sb::kTagListCount + 4] = {};
};

static void SetTag(FakeCountry& c, std::int16_t index) {
    const std::uint64_t raw = static_cast<std::uint64_t>(static_cast<std::uint16_t>(index)) << 32;
    std::memcpy(c.bytes + sb::kCountryTagIndex, &raw, sizeof raw);
}

static void SetLists(FakeCountry& c, FakeList* lists, std::int32_t count) {
    std::memcpy(c.bytes + sb::kTagLists, &lists, sizeof lists);
    std::memcpy(c.bytes + sb::kTagListCount, &count, sizeof count);
}

static void SetList(FakeList& l, unsigned char** items, std::int32_t n) {
    std::memcpy(l.bytes + sb::kTagListData, &items, sizeof items);
    std::memcpy(l.bytes + sb::kTagListLength, &n, sizeof n);
}

// The game's loop, as in the tick: entries reached per pair, with repeats.
static std::int64_t GameLoop(const unsigned char* const* countries, std::int64_t count) {
    std::int64_t reached = 0;
    for (std::int64_t i = 0; i < count; ++i) {
        for (std::int64_t k = 0; k < count; ++k) {
            const auto index =
                static_cast<std::int16_t>(sb::ReadAt<std::uint64_t>(countries[k], sb::kCountryTagIndex) >> 32);
            if (index < 0 || index >= sb::ReadAt<std::int32_t>(countries[i], sb::kTagListCount)) continue;
            const auto* list = sb::ReadAt<const unsigned char*>(countries[i], sb::kTagLists) + index * sb::kTagListStride;
            const std::int32_t n = sb::ReadAt<std::int32_t>(list, sb::kTagListLength);
            auto* const* items = sb::ReadAt<unsigned char* const*>(list, sb::kTagListData);
            for (std::int32_t e = 0; e < n; ++e) {
                items[e][sb::kEntryCopy] = items[e][sb::kEntrySource];
                ++reached;
            }
        }
    }
    return reached;
}

static void TestTagLists() {
    static unsigned char present[sb::kTagIndices];
    unsigned char entry[4][0x28] = {};
    entry[0][sb::kEntrySource] = 1;
    entry[2][sb::kEntrySource] = 1;
    entry[3][sb::kEntryCopy] = 1;  // copy differs from the source until the game's loop runs
    unsigned char* a0[] = {entry[0], entry[1]};
    unsigned char* a2[] = {entry[2]};
    unsigned char* b1[] = {entry[3]};
    // Country A (tag 0) has lists for tags 0..3; tag 3 belongs to no country, so its list stays unread.
    FakeList listsA[4], listsB[2];
    SetList(listsA[0], a0, 2);
    SetList(listsA[2], a2, 1);
    unsigned char* orphan[] = {entry[3]};
    SetList(listsA[3], orphan, 1);
    SetList(listsB[1], b1, 1);
    FakeCountry a, b, c, dead;
    SetTag(a, 0);
    SetTag(b, 1);
    SetTag(c, 2);
    SetTag(dead, -1);
    SetLists(a, listsA, 4);
    SetLists(b, listsB, 2);
    SetLists(c, nullptr, 0);
    SetLists(dead, nullptr, 0);
    const unsigned char* countries[] = {a.bytes, b.bytes, c.bytes, dead.bytes};

    sb::TagListWalk before = sb::WalkPerTagLists(countries, 4, present);
    assert(before.countries == 4 && before.pairs == 16);
    assert(before.lists == 3 + 2);  // A: tags 0..2, B: tags 0..1; C has no lists; tag -1 and tag 3 skipped
    assert(before.entries == 4);    // entry[3] counted once, through B (A's list for tag 3 is not read)
    assert(before.mismatches == 3);

    assert(GameLoop(countries, 4) == 4);
    sb::TagListWalk after = sb::WalkPerTagLists(countries, 4, present);
    assert(after.entries == before.entries && after.mismatches == 0);
}

int main() {
    TestDateText();
    TestTagLists();
    // Sparse destination: every id but 0x286 is scanned for.
    const std::int32_t src[] = {5, 10, 0x286, 0, 0x300, 7};
    sb::MergeShape s = sb::ShapeOfMerge(false, 40, false, src, 3);
    assert(s.scanned == 2 && s.src_count == 3 && s.dest_count == 40);
    assert(sb::ScanSteps(s) == 2 * 40 + 1);

    // Dense destination: static ids go in place, only 0x300 is scanned for.
    s = sb::ShapeOfMerge(true, 0x290, false, src, 3);
    assert(s.scanned == 1 && s.dest_dense && !s.src_dense);
    assert(sb::ScanSteps(s) == 0x290);

    // Nothing scanned, empty source.
    s = sb::ShapeOfMerge(true, 0x290, true, src, 0);
    assert(s.scanned == 0 && sb::ScanSteps(s) == 0);

    assert(sb::Bucket(0) == 0 && sb::Bucket(1) == 0 && sb::Bucket(2) == 1 && sb::Bucket(1023) == 9);
    assert(sb::Bucket(1LL << 40) == 15);
    std::printf("monthly_probe ok\n");
    return 0;
}
