// port_table (off by default): the game's table of the nearest port without its repeated walks
// (lab docs: docs/simulation-profile.md, "Tabela portów"; the loop in port_table_core.hpp).
//
// "verify" never changes the table: the game's function runs, and Fill runs on a copy that must end with
// the same cells. "on" checks every 256th rebuild the same way and goes back to the game's function for
// good after a mismatch. A background thread writes the counters every 5 s to port_table.tsv in the data
// directory, because the game is usually closed without a clean exit.
#include "modules.hpp"
#include "port_table_core.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

extern "C" {
// requests = rebuilds, hits = rebuilds by Fill, skips = rebuilds left to the game's function,
// verifications = rebuilds compared with the game's function, mismatches = comparisons that differed,
// cacheDisabled = 1 after a mismatch in "on", mode = 1 on / 2 verify.
__declspec(dllexport) sb::Stats EU4SB_PortTableStats = SB_STATS_INIT;
}

namespace {

namespace port = sb::port;

using TableFn = void (*)(void* country);
using DistanceFn = void* (*)(std::int32_t* out, std::uint64_t country, const void* from, const void* to, void* blocked,
                             std::uint32_t land);
using RangeFn = std::int32_t* (*)(void* country, std::int32_t* out);

anvil::HookOf<sym::port_table> g_hook;
TableFn g_next = nullptr;
DistanceFn g_distance = nullptr;
RangeFn g_range = nullptr;
const unsigned char* const* g_root = nullptr;
bool g_verifyOnly = false;
volatile LONG64 g_checkSequence = 0;
volatile LONG64 g_gameTicks = 0, g_fastTicks = 0, g_collectTicks = 0, g_ports = 0, g_gameCalls = 0;
// Compared rebuilds that left the table as it was, and their time in the game's function.
volatile LONG64 g_unchanged = 0, g_unchangedTicks = 0;
std::wstring* g_dir = nullptr;
constexpr DWORD kWriteEveryMs = 5000;
constexpr LONG64 kCheckEvery = 256;

thread_local std::vector<port::Bytes>* t_ports = nullptr;

// The rebuild, on `table`. Throws only while collecting the list, before anything is written.
void Rebuild(const unsigned char* root, unsigned char* country, std::int16_t* table) {
    if (t_ports == nullptr) t_ports = new std::vector<port::Bytes>();
    t_ports->clear();
    const LONG64 start = sb::Ticks();
    port::CollectPorts(root, country, *t_ports);
    InterlockedExchangeAdd64(&g_collectTicks, sb::Ticks() - start);
    InterlockedExchangeAdd64(&g_ports, static_cast<LONG64>(t_ports->size()));
    const auto id = port::At<std::uint64_t>(country, port::kCountryId);
    port::Fill(
        root, country, *t_ports, table,
        [id](port::Bytes from, port::Bytes to) {
            std::int32_t distance = 0;
            g_distance(&distance, id, from, to, nullptr, 1);
            return distance;
        },
        [country] {
            std::int32_t range = 0;
            g_range(country, &range);
            return range;
        });
}

void Detour(void* countryPtr) {
    InterlockedIncrement64(&EU4SB_PortTableStats.requests);
    const unsigned char* root = *g_root;
    auto* country = static_cast<unsigned char*>(countryPtr);
    if (EU4SB_PortTableStats.cacheDisabled != 0 || root == nullptr || !port::Applies(root, country)) {
        InterlockedIncrement64(&EU4SB_PortTableStats.skips);
        return g_next(countryPtr);
    }
    auto* table = port::At<std::int16_t*>(country, port::kCountryTable);
    const std::size_t cells = (port::At<std::int16_t*>(country, port::kCountryTableEnd) - table);
    const bool check = g_verifyOnly || (InterlockedIncrement64(&g_checkSequence) & (kCheckEvery - 1)) == 0;
    if (!check) {
        const LONG64 start = sb::Ticks();
        try {
            Rebuild(root, country, table);
        } catch (...) {
            // Only the list allocates, and it does so before Fill writes anything.
            InterlockedIncrement64(&EU4SB_PortTableStats.skips);
            return g_next(countryPtr);
        }
        country[port::kCountryDirty] = 0;
        InterlockedExchangeAdd64(&g_fastTicks, sb::Ticks() - start);
        InterlockedIncrement64(&EU4SB_PortTableStats.hits);
        return;
    }
    std::vector<std::int16_t> copy, before;
    try {
        before.assign(table, table + cells);
        copy = before;
        const LONG64 start = sb::Ticks();
        Rebuild(root, country, copy.data());
        InterlockedExchangeAdd64(&g_fastTicks, sb::Ticks() - start);
    } catch (...) {
        InterlockedIncrement64(&EU4SB_PortTableStats.skips);
        return g_next(countryPtr);
    }
    const LONG64 start = sb::Ticks();
    g_next(countryPtr);
    const LONG64 gameTicks = sb::Ticks() - start;
    InterlockedExchangeAdd64(&g_gameTicks, gameTicks);
    InterlockedIncrement64(&g_gameCalls);
    if (std::equal(before.begin(), before.end(), table)) {
        InterlockedIncrement64(&g_unchanged);
        InterlockedExchangeAdd64(&g_unchangedTicks, gameTicks);
    }
    InterlockedIncrement64(&EU4SB_PortTableStats.verifications);
    if (!std::equal(copy.begin(), copy.end(), table)) {
        InterlockedIncrement64(&EU4SB_PortTableStats.mismatches);
        if (!g_verifyOnly && InterlockedExchange(&EU4SB_PortTableStats.cacheDisabled, 1) == 0) {
            sb::Log(ANVIL_LOG_WARNING, "port_table: a table differed from the game's; back to the game's function");
        }
    }
}

void WriteFile() {
    const sb::Stats& s = EU4SB_PortTableStats;
    try {
        const std::wstring path = *g_dir + L"\\port_table.tsv";
        const std::wstring temp = path + L".tmp";
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            const double ms = sb::TicksToMs(1);
            out << "mode\trequests\thits\tskips\tverifications\tmismatches\tdisabled\tgame_calls\tgame_ms\tfast_ms\t"
                   "collect_ms\tports\tunchanged\tunchanged_game_ms\n"
                << (g_verifyOnly ? "verify" : "on") << "\t" << s.requests << "\t" << s.hits << "\t" << s.skips << "\t"
                << s.verifications << "\t" << s.mismatches << "\t" << s.cacheDisabled << "\t" << g_gameCalls << "\t"
                << g_gameTicks * ms << "\t" << g_fastTicks * ms << "\t" << g_collectTicks * ms << "\t" << g_ports << "\t"
                << g_unchanged << "\t" << g_unchangedTicks * ms << "\n";
        }
        MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
    } catch (...) {
    }
}

DWORD WINAPI WriterThread(void*) {
    LONG64 written = -1;
    for (;;) {
        Sleep(kWriteEveryMs);
        if (EU4SB_PortTableStats.requests != written) {
            written = EU4SB_PortTableStats.requests;
            WriteFile();
        }
    }
}

}  // namespace

namespace sb {

bool InstallPortTable(anvil::Host& host, Mode mode) {
    if (mode == Mode::Off) return false;
    g_verifyOnly = mode == Mode::Verify;
    try {
        g_dir = new std::wstring(host.data_dir().wstring());
    } catch (...) {
        return false;
    }
    // The table calls 0x2D2520 with six arguments; the symbol declares the seven range_profile forwards.
    g_distance = reinterpret_cast<DistanceFn>(host.address(sym::province_distance));
    g_range = reinterpret_cast<RangeFn>(host.address(sym::port_range));
    g_root = host.data(sym::game_root);
    host.hook(g_hook, sym::port_table, &Detour);
    g_next = g_hook.next_pointer();
    InterlockedExchange(&EU4SB_PortTableStats.mode, g_verifyOnly ? 2 : 1);
    InterlockedExchange(&EU4SB_PortTableStats.initialized, 1);
    HANDLE thread = CreateThread(nullptr, 0, WriterThread, nullptr, 0, nullptr);
    if (thread != nullptr) {
        SetThreadDescription(thread, L"bellows port_table writer");
        CloseHandle(thread);
    }
    Log(ANVIL_LOG_INFO, "port_table: %s the nearest-port table from one list of ports",
        g_verifyOnly ? "verifying" : "rebuilding");
    return true;
}

void ReportPortTable() {
    LogStats("port_table", EU4SB_PortTableStats);  // no Benchmark counters: Anvil takes 128 fields, and they are full
}

}  // namespace sb
