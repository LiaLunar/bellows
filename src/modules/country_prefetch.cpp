// country_prefetch (prototype, off by default): while the game updates one country in the daily country loop, a
// helper thread prefetches the object and the row of diplomatic statuses of the next country (the reasoning in
// country_prefetch_core.hpp; lab docs: docs/anbennar-speed-2026-11-11.md).
//
// A hook on the entry of a country's daily update (0x2EE280) finds the country's place in the root's country
// array and hands the next entry to the helper. The helper reads two pointers of that country (the begin and end
// of its row, country + 0x1510 / + 0x1518) and issues PREFETCHT2 for every cache line of the country object and of
// the row. It never writes game memory and never calls the game, so the daily update reads the same values and the
// game computes the same answers; only where the lines come from changes. The helper's reads run under a
// structured exception handler, so a country freed while a save loads cannot take the game down.
//
// "on" prefetches on every day. "verify" prefetches only on every other day and times each daily update by the
// class of the day (country_prefetch_core.hpp: prefetch days, plain days, first days of a month), so one game
// gives both arms over the same years. A background thread writes the counters every 5 s to
// country_prefetch.tsv in the data directory.
#include "country_prefetch_core.hpp"
#include "modules.hpp"

#include <immintrin.h>

#include <fstream>
#include <string>

#pragma comment(lib, "Synchronization.lib")  // WaitOnAddress, WakeByAddressSingle

extern "C" {
// requests = daily updates seen, hits = countries handed to the helper, misses = updates of a country not in the
// array, skips = updates with no next country or on a day without prefetch, mode = 1 on / 2 verify.
__declspec(dllexport) sb::Stats EU4SB_CountryPrefetchStats = SB_STATS_INIT;
}

namespace {

namespace cp = sb::cprefetch;

// Offsets from the PDB layout (CGameState, CCountry), the same in the Steam and GOG builds of 1.37.5.
constexpr std::size_t kRootCountries = 0x1D08;     // CPdxArray<CCountry*>: data
constexpr std::size_t kRootCountryCount = 0x1D14;  // its count (int)
constexpr std::size_t kCountryRows = 0x1510;       // CCountryDiplomacy::_Statuses: begin, end at + 8
constexpr std::size_t kCountrySize = 0x9B78;
constexpr std::size_t kMaxRowBytes = 4u << 20;     // sanity bound for the row (~58,000 tags)

anvil::HookOf<sym::country_day> g_hook;
sym::Types::country_day::Pointer g_next = nullptr;
const unsigned char* const* g_rootGlobal = nullptr;
std::size_t g_rootDate = 0;
bool g_verifyOnly = false;
int g_last = -1;  // main thread only

// Helper hand-over: the country to prefetch and a sequence number the helper waits on.
void* volatile g_target = nullptr;
volatile LONG g_sequence = 0;
volatile LONG g_sleeping = 0;
volatile LONG64 g_lines = 0, g_helperTicks = 0, g_faults = 0;
volatile LONG64 g_calls[cp::kClasses] = {0, 0, 0}, g_ticks[cp::kClasses] = {0, 0, 0};
std::wstring* g_dir = nullptr;
constexpr DWORD kWriteEveryMs = 5000;
constexpr LONG64 kSpinMs = 2;  // the helper spins this long after a request before it sleeps

LONG64 PrefetchCountry(const unsigned char* country) noexcept {
    LONG64 lines = 0;
    __try {
        for (std::size_t off = 0; off < kCountrySize; off += 64, ++lines)
            _mm_prefetch(reinterpret_cast<const char*>(country + off), _MM_HINT_T2);
        const auto* begin = sb::Read<const unsigned char*>(country, kCountryRows);
        const auto* end = sb::Read<const unsigned char*>(country, kCountryRows + 8);
        if (begin != nullptr && end > begin && static_cast<std::size_t>(end - begin) <= kMaxRowBytes) {
            for (const unsigned char* p = begin; p < end; p += 64, ++lines)
                _mm_prefetch(reinterpret_cast<const char*>(p), _MM_HINT_T2);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        InterlockedIncrement64(&g_faults);
    }
    return lines;
}

DWORD WINAPI HelperThread(void*) {
    LONG seen = g_sequence;
    LONG64 lastWork = sb::Ticks();
    const double msPerTick = sb::TicksToMs(1);
    for (;;) {
        const LONG now = g_sequence;
        if (now != seen) {
            seen = now;
            const LONG64 start = sb::Ticks();
            const LONG64 lines = PrefetchCountry(static_cast<const unsigned char*>(g_target));
            lastWork = sb::Ticks();
            InterlockedExchangeAdd64(&g_lines, lines);
            InterlockedExchangeAdd64(&g_helperTicks, lastWork - start);
            continue;
        }
        if ((sb::Ticks() - lastWork) * msPerTick < kSpinMs) {
            _mm_pause();
            continue;
        }
        InterlockedExchange(&g_sleeping, 1);
        if (g_sequence == seen) WaitOnAddress(const_cast<LONG*>(&g_sequence), &seen, sizeof(seen), INFINITE);
        InterlockedExchange(&g_sleeping, 0);
        lastWork = sb::Ticks();
    }
}

void Handover(void* country) noexcept {
    g_target = country;
    InterlockedIncrement(&g_sequence);
    if (g_sleeping != 0) WakeByAddressSingle(const_cast<LONG*>(&g_sequence));
}

void Detour(void* country) {
    InterlockedIncrement64(&EU4SB_CountryPrefetchStats.requests);
    const unsigned char* root = *g_rootGlobal;
    const cp::DayClass day = root != nullptr ? cp::ClassOf(sb::Read<std::int32_t>(root, g_rootDate)) : cp::kPlainDay;
    // "verify": no prefetch on first days of a month, so their time is the game's alone.
    const bool prefetchToday = !g_verifyOnly || day == cp::kPrefetchDay;
    if (root != nullptr) {
        void* const* array = sb::Read<void* const*>(root, kRootCountries);
        const int count = sb::Read<std::int32_t>(root, kRootCountryCount);
        const int at = cp::Locate(array, count, g_last, country);
        if (at < 0) {
            InterlockedIncrement64(&EU4SB_CountryPrefetchStats.misses);
        } else {
            g_last = at;
            if (prefetchToday && at + 1 < count && array[at + 1] != nullptr) {
                Handover(array[at + 1]);
                InterlockedIncrement64(&EU4SB_CountryPrefetchStats.hits);
            } else {
                InterlockedIncrement64(&EU4SB_CountryPrefetchStats.skips);
            }
        }
    }
    const LONG64 start = sb::Ticks();
    g_next(country);
    InterlockedExchangeAdd64(&g_ticks[day], sb::Ticks() - start);
    InterlockedIncrement64(&g_calls[day]);
}

void WriteFile() {
    const sb::Stats& s = EU4SB_CountryPrefetchStats;
    try {
        const std::wstring path = *g_dir + L"\\country_prefetch.tsv";
        const std::wstring temp = path + L".tmp";
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            out << "mode\trequests\thanded\tnot_in_array\tnot_handed\tlines\thelper_ms\tfaults\t"
                   "calls_prefetch_days\tms_prefetch_days\tcalls_plain_days\tms_plain_days\tcalls_first_days\tms_first_days\n"
                << (g_verifyOnly ? "verify" : "on") << "\t" << s.requests << "\t" << s.hits << "\t" << s.misses << "\t"
                << s.skips << "\t" << g_lines << "\t" << sb::TicksToMs(g_helperTicks) << "\t" << g_faults;
            for (int k = 0; k < cp::kClasses; ++k) out << "\t" << g_calls[k] << "\t" << sb::TicksToMs(g_ticks[k]);
            out << "\n";
        }
        MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
    } catch (...) {
    }
}

DWORD WINAPI WriterThread(void*) {
    LONG64 written = -1;
    for (;;) {
        Sleep(kWriteEveryMs);
        if (EU4SB_CountryPrefetchStats.requests != written) {
            written = EU4SB_CountryPrefetchStats.requests;
            WriteFile();
        }
    }
}

}  // namespace

namespace sb {

bool InstallCountryPrefetch(anvil::Host& host, Mode mode) {
    if (mode == Mode::Off) return false;
    g_verifyOnly = mode == Mode::Verify;
    try {
        g_dir = new std::wstring(host.data_dir().wstring());
    } catch (...) {
        return false;
    }
    g_rootGlobal = host.data(sym::game_root);
    g_rootDate = static_cast<std::size_t>(host.value(sym::root_and_date, "date_offset"));
    host.hook(g_hook, sym::country_day, &Detour);
    g_next = g_hook.next_pointer();
    HANDLE helper = CreateThread(nullptr, 0, HelperThread, nullptr, 0, nullptr);
    if (helper != nullptr) {
        SetThreadDescription(helper, L"bellows country_prefetch helper");
        CloseHandle(helper);
    }
    InterlockedExchange(&EU4SB_CountryPrefetchStats.mode, g_verifyOnly ? 2 : 1);
    InterlockedExchange(&EU4SB_CountryPrefetchStats.initialized, 1);
    HANDLE thread = CreateThread(nullptr, 0, WriterThread, nullptr, 0, nullptr);
    if (thread != nullptr) {
        SetThreadDescription(thread, L"bellows country_prefetch writer");
        CloseHandle(thread);
    }
    Log(ANVIL_LOG_INFO, "country_prefetch: prefetching the next country of the daily loop %s",
        g_verifyOnly ? "on every other day, timing both kinds of day" : "on every day");
    return true;
}

void ReportCountryPrefetch() {
    LogStats("country_prefetch", EU4SB_CountryPrefetchStats);
}

}  // namespace sb
