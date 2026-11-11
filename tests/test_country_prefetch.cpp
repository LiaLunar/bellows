// country_prefetch: day classes and the place of a country in the loop. That prefetching leaves the game's answers
// as they are follows from the module never writing game memory; the time it saves is a lab measurement.
#include "modules/country_prefetch_core.hpp"

#include <cassert>
#include <cstdio>

using namespace sb::cprefetch;

constexpr std::int32_t Raw(int year, int dayOfYear) {  // dayOfYear 0 = 1 January
    return ((year + 5000) * kDaysPerYear + dayOfYear) * kHoursPerDay;
}

int main() {
    // 1 January 1446 as the game stores it (lab: run_until reached_date_raw 56466960).
    static_assert(Raw(1446, 0) == 56466960);
    static_assert(DayOfMonth(56466960) == 1);
    static_assert(DayOfMonth(Raw(1445, 30)) == 31);   // 31 January
    static_assert(DayOfMonth(Raw(1445, 31)) == 1);    // 1 February
    static_assert(DayOfMonth(Raw(1445, 58)) == 28);   // 28 February, no leap years
    static_assert(DayOfMonth(Raw(1445, 59)) == 1);    // 1 March
    static_assert(DayOfMonth(Raw(1445, 364)) == 31);  // 31 December
    static_assert(DayOfMonth(Raw(1445, 0) + 23) == 1);  // hours within the day
    static_assert(DayOfMonth(-1) == 0);

    // The 1st and the 2nd of a month apart, the others alternate.
    assert(ClassOf(Raw(1445, 31)) == kFirstDay && ClassOf(Raw(1445, 32)) == kFirstDay);
    assert(ClassOf(Raw(1445, 33)) != kFirstDay && ClassOf(Raw(1445, 30)) != kFirstDay);
    const DayClass a = ClassOf(Raw(1445, 40)), b = ClassOf(Raw(1445, 41)), c = ClassOf(Raw(1445, 42));
    assert(a != kFirstDay && b != kFirstDay && a != b && a == c);
    int counts[kClasses] = {0, 0, 0};
    for (int d = 0; d < kDaysPerYear; ++d) ++counts[ClassOf(Raw(1445, d))];
    assert(counts[kFirstDay] == 24);
    assert(counts[kPrefetchDay] + counts[kPlainDay] == 341);
    assert(counts[kPrefetchDay] - counts[kPlainDay] <= 1 && counts[kPlainDay] - counts[kPrefetchDay] <= 1);

    // Locate: in order, from the start, out of order, absent.
    int x[5];
    void* array[5] = {&x[0], &x[1], &x[2], &x[3], &x[4]};
    assert(Locate(array, 5, -1, &x[0]) == 0);
    assert(Locate(array, 5, 0, &x[1]) == 1);
    assert(Locate(array, 5, 4, &x[0]) == 0);  // the next day starts again
    assert(Locate(array, 5, 1, &x[3]) == 3);  // a skipped entry
    int other = 0;
    assert(Locate(array, 5, 2, &other) == -1);
    assert(Locate(nullptr, 0, -1, &x[0]) == -1);
    assert(Locate(array, 0, -1, &x[0]) == -1);

    std::puts("country_prefetch: ok");
    return 0;
}
