// province_modifier_view (on by default since 1.4): 0x2FFC60 without its copy of the province's modifier list (lab docs:
// docs/simulation-profile.md, "RefreshKnowledge"; the reasoning in province_modifier_view_core.hpp).
//
// 0x2FFC60 copies a province's whole modifier list (hundreds of entries, reallocating from 8 by doubling,
// plus its vector of sources) only so that 0x3003B0 and 0x300720 can read one value from it. The daily
// knowledge refresh of a country (0x93FE30 > 0x93FB80) calls it for every province the country owns. The
// hook calls the game's 0x3003B0 itself with a sparse view of the province's entries instead of the copy,
// which gives the same value while the value objects of modifiers 0x266 and 0x267 are unset (0x300720 hands
// the list to them otherwise); when either is set, and for calls without a province, the game's function runs.
//
// "verify" runs both and returns the game's value; "on" compares every 1024th call the same way and goes
// back to the game's function for good after a mismatch. A background thread writes the counters every 5 s
// to province_modifier_view.tsv in the data directory.
#include "modules.hpp"
#include "province_modifier_view_core.hpp"

#include <fstream>
#include <string>

extern "C" {
// requests = calls, hits = calls answered through the view, skips = calls without a province, misses = calls
// while a value object of 0x266 / 0x267 is set, verifications = calls compared with the game's function,
// mismatches = comparisons that differed, cacheDisabled = 1 after a mismatch in "on", mode = 1 on / 2 verify.
__declspec(dllexport) sb::Stats EU4SB_ProvinceModifierViewStats = SB_STATS_INIT;
}

namespace {

using ValueFn = std::int32_t* (*)(void* country, std::int32_t* out, void* modifiers, void* culture);

anvil::HookOf<sym::province_culture_value> g_hook;
ValueFn g_next = nullptr;
ValueFn g_value = nullptr;  // 0x3003B0
const unsigned char* const* g_registry = nullptr;
std::size_t g_provinceModifiers = 0, g_post266 = 0, g_post267 = 0;
bool g_verifyOnly = false;
volatile LONG64 g_checkSequence = 0;
volatile LONG64 g_gameTicks = 0, g_fastTicks = 0;
std::wstring* g_dir = nullptr;
constexpr DWORD kWriteEveryMs = 5000;

bool ValueObjectsUnset() {
    const unsigned char* registry = *g_registry;
    return registry != nullptr && sb::Read<const void*>(registry, g_post266) == nullptr &&
           sb::Read<const void*>(registry, g_post267) == nullptr;
}

std::int32_t* ThroughView(void* country, std::int32_t* out, void* province, void* culture) {
    const auto* list = static_cast<const unsigned char*>(province) + g_provinceModifiers;
    sb::ModListView view;
    sb::PointView(view, sb::Read<const std::int32_t*>(list, sb::kModEntries), sb::Read<std::int32_t>(list, sb::kModCount));
    return g_value(country, out, view.bytes, culture);
}

std::int32_t* Detour(void* country, std::int32_t* out, void* province, void* culture) {
    InterlockedIncrement64(&EU4SB_ProvinceModifierViewStats.requests);
    if (province == nullptr) {
        InterlockedIncrement64(&EU4SB_ProvinceModifierViewStats.skips);
        return g_next(country, out, province, culture);
    }
    if (EU4SB_ProvinceModifierViewStats.cacheDisabled != 0 || !ValueObjectsUnset()) {
        InterlockedIncrement64(&EU4SB_ProvinceModifierViewStats.misses);
        return g_next(country, out, province, culture);
    }
    if (g_verifyOnly || (InterlockedIncrement64(&g_checkSequence) & 1023) == 0) {
        std::int32_t fast = 0;
        ThroughView(country, &fast, province, culture);
        const LONG64 start = sb::Ticks();
        std::int32_t* result = g_next(country, out, province, culture);
        InterlockedExchangeAdd64(&g_gameTicks, sb::Ticks() - start);
        InterlockedIncrement64(&EU4SB_ProvinceModifierViewStats.verifications);
        if (*out != fast) {
            InterlockedIncrement64(&EU4SB_ProvinceModifierViewStats.mismatches);
            if (!g_verifyOnly && InterlockedExchange(&EU4SB_ProvinceModifierViewStats.cacheDisabled, 1) == 0)
                sb::Log(ANVIL_LOG_WARNING, "province_modifier_view: a value differed from the game's; back to the game's function");
        }
        return result;
    }
    const LONG64 start = sb::Ticks();
    std::int32_t* result = ThroughView(country, out, province, culture);
    InterlockedExchangeAdd64(&g_fastTicks, sb::Ticks() - start);
    InterlockedIncrement64(&EU4SB_ProvinceModifierViewStats.hits);
    return result;
}

void WriteFile() {
    const sb::Stats& s = EU4SB_ProvinceModifierViewStats;
    try {
        const std::wstring path = *g_dir + L"\\province_modifier_view.tsv";
        const std::wstring temp = path + L".tmp";
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            const double ms = sb::TicksToMs(1);
            out << "mode\trequests\thits\tskips\tvalue_objects_set\tverifications\tmismatches\tdisabled\tgame_ms\tview_ms\n"
                << (g_verifyOnly ? "verify" : "on") << "\t" << s.requests << "\t" << s.hits << "\t" << s.skips << "\t"
                << s.misses << "\t" << s.verifications << "\t" << s.mismatches << "\t" << s.cacheDisabled << "\t"
                << g_gameTicks * ms << "\t" << g_fastTicks * ms << "\n";
        }
        MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
    } catch (...) {
    }
}

DWORD WINAPI WriterThread(void*) {
    LONG64 written = -1;
    for (;;) {
        Sleep(kWriteEveryMs);
        if (EU4SB_ProvinceModifierViewStats.requests != written) {
            written = EU4SB_ProvinceModifierViewStats.requests;
            WriteFile();
        }
    }
}

}  // namespace

namespace sb {

bool InstallProvinceModifierView(anvil::Host& host, Mode mode) {
    if (mode == Mode::Off) return false;
    g_verifyOnly = mode == Mode::Verify;
    try {
        g_dir = new std::wstring(host.data_dir().wstring());
    } catch (...) {
        return false;
    }
    g_value = host.function(sym::culture_modifier_value);
    g_registry = host.data(sym::modifier_registry);
    g_provinceModifiers = static_cast<std::size_t>(host.value(sym::province_culture_value_list, "province_modifiers"));
    g_post266 = static_cast<std::size_t>(host.value(sym::modifier_value_post_266, "post_266"));
    g_post267 = static_cast<std::size_t>(host.value(sym::modifier_value_post_267, "post_267"));
    host.hook(g_hook, sym::province_culture_value, &Detour);
    g_next = g_hook.next_pointer();
    InterlockedExchange(&EU4SB_ProvinceModifierViewStats.mode, g_verifyOnly ? 2 : 1);
    InterlockedExchange(&EU4SB_ProvinceModifierViewStats.initialized, 1);
    HANDLE thread = CreateThread(nullptr, 0, WriterThread, nullptr, 0, nullptr);
    if (thread != nullptr) {
        SetThreadDescription(thread, L"bellows province_modifier_view writer");
        CloseHandle(thread);
    }
    Log(ANVIL_LOG_INFO, "province_modifier_view: %s the province modifier value without the list copy",
        g_verifyOnly ? "verifying" : "reading");
    return true;
}

void ReportProvinceModifierView() {
    // No benchmark counters: the report is at its limit of 128 fields; the mode is there, the counters in the log.
    LogStats("province_modifier_view", EU4SB_ProvinceModifierViewStats);
}

}  // namespace sb
