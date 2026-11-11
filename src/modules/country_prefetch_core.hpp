#pragma once

// country_prefetch: the pure parts, unit-tested (tests/test_country_prefetch.cpp).
//
// The daily country loop (0x75B0D0) calls the daily update of a country (0x2EE280) for every entry of the game
// root's country array (root + 0x1D08, count root + 0x1D14), one after the other on the main thread. Each update
// reads, among much else, the country's whole row of diplomatic statuses (CDiplomacyStatus, 72 B per tag, from
// country + 0x1510): CCountry::DailyUpdateSerialCheckDiplomacyChange tests one bit of every row. On a mod set with
// ~1,900 tags that row is ~134 KB per country and ~125 MB per day over the living countries, far more than the
// last-level cache, so every country starts from memory. The module tells a helper thread which country comes
// next; the helper only issues prefetch instructions for that country's object and row while the game updates the
// current one. Prefetching writes nothing, so the game computes exactly what it computes without the module.

#include <cstdint>

namespace sb::cprefetch {

// EU4 dates: int32 hours since 1 January of year -5000 (CGameState::_CurrentDate), 365-day years.
constexpr std::int32_t kHoursPerDay = 24;
constexpr std::int32_t kDaysPerYear = 365;

// Day of the month, 1-31, of a raw date.
constexpr int DayOfMonth(std::int32_t raw) noexcept {
    constexpr int kMonth[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (raw < 0) return 0;
    int day = static_cast<int>((raw / kHoursPerDay) % kDaysPerYear);
    for (int m = 0; m < 12; ++m) {
        if (day < kMonth[m]) return day + 1;
        day -= kMonth[m];
    }
    return 0;
}

// The class of a game day for the "verify" comparison: 0 and 1 alternate by the day number (0: prefetch on, 1:
// off), 2 is the first day of a month (no prefetch in "verify"). After the monthly tick almost every country refreshes its knowledge and
// modifiers on that day, which makes it about ten times longer than the others on Anbennar; it is kept apart so
// that the uneven count of first days in the two halves does not decide the comparison.
enum DayClass : int { kPrefetchDay = 0, kPlainDay = 1, kFirstDay = 2, kClasses = 3 };

constexpr DayClass ClassOf(std::int32_t raw) noexcept {
    if (DayOfMonth(raw) == 1) return kFirstDay;
    return ((raw / kHoursPerDay) & 1) == 0 ? kPrefetchDay : kPlainDay;
}

// Position of `country` in the game's array, starting the search after `last` (the loop goes in order, so the
// next entry is almost always the answer). Returns -1 when the country is not in the array (a call from
// elsewhere than the daily loop).
inline int Locate(void* const* array, int count, int last, const void* country) noexcept {
    if (array == nullptr || count <= 0) return -1;
    const int guess = last + 1;
    if (guess >= 0 && guess < count && array[guess] == country) return guess;
    if (array[0] == country) return 0;
    for (int i = 0; i < count; ++i)
        if (array[i] == country) return i;
    return -1;
}

}  // namespace sb::cprefetch
