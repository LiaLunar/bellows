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

int main() {
    TestDateText();
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
