// army_strength_memo (off by default): within one AI army search (0x1022B0), the army's strength against a
// province from 0x102140 once instead of once per open neighbour (lab docs:
// docs/simulation-hotspots-after-142.md; the reasoning in army_strength_memo_core.hpp).
//
// The module redirects two of the three calls of 0x102140 in 0x1022B0. The first, 0x10249E, runs once at the
// start of every search (no jump in the function passes it), so it starts a new memo on this thread. The
// third, 0x10303A, is the one in the loop over the neighbours: it is answered from the memo.
//
// "verify" runs the game's function for every repeated call and compares the answers bit for bit; "on"
// compares every 1024th repeated call the same way and goes back to the game's function for good after a
// mismatch. The counters live per thread and reach the shared ones once per search, so that the calls of
// seven threads do not fight over a cache line. A background thread writes them every 5 s to
// army_strength_memo.tsv in the data directory.
#include "army_strength_memo_core.hpp"
#include "modules.hpp"

#include <cstring>
#include <fstream>
#include <string>

extern "C" {
// requests = calls in the loop the memo could answer, hits = calls answered from the memo, misses = calls that
// ran the game's function to fill a slot, skips = calls the memo cannot answer, verifications = repeated calls
// compared with the game's function, mismatches = comparisons that differed, duplicateKeys = searches,
// cacheDisabled = 1 after a mismatch in "on", mode = 1 on / 2 verify.
__declspec(dllexport) sb::Stats EU4SB_ArmyStrengthMemoStats = SB_STATS_INIT;
}

namespace {

namespace strength = sb::strength;

using Signature = float(std::uint64_t, std::uint64_t, void*, std::uint8_t, int);

anvil::FunctionHook<Signature> g_firstSite, g_loopSite;
Signature* g_first = nullptr;
Signature* g_loop = nullptr;
bool g_verifyOnly = false;
volatile LONG64 g_repeatTicks = 0;
std::wstring* g_dir = nullptr;
constexpr DWORD kWriteEveryMs = 5000;
constexpr std::size_t kProvinceId = 0x20;

struct Tally {
    LONG64 searches = 0, requests = 0, hits = 0, misses = 0, skips = 0, verifications = 0, ticks = 0;
    std::uint32_t sequence = 0;
};

thread_local strength::Memo t_memo;
thread_local Tally t_tally;

void Add(volatile LONG64* sink, LONG64& value) noexcept {
    if (value != 0) InterlockedExchangeAdd64(sink, value);
    value = 0;
}

void Flush(Tally& t) noexcept {
    auto& s = EU4SB_ArmyStrengthMemoStats;
    Add(&s.duplicateKeys, t.searches);
    Add(&s.requests, t.requests);
    Add(&s.hits, t.hits);
    Add(&s.misses, t.misses);
    Add(&s.skips, t.skips);
    Add(&s.verifications, t.verifications);
    Add(&g_repeatTicks, t.ticks);
}

float FirstDetour(std::uint64_t flag, std::uint64_t army, void* province, std::uint8_t a4, int a5) {
    if (t_memo.slots == nullptr) {
        t_memo.slots = static_cast<strength::Slot*>(VirtualAlloc(nullptr, sizeof(strength::Slot) * strength::kSlots,
                                                                 MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    }
    strength::BeginSearch(t_memo, reinterpret_cast<const void*>(army));
    Tally& t = t_tally;
    ++t.searches;
    Flush(t);
    return g_first(flag, army, province, a4, a5);
}

float LoopDetour(std::uint64_t flag, std::uint64_t army, void* province, std::uint8_t a4, int a5) {
    std::int32_t id;
    std::memcpy(&id, static_cast<const char*>(province) + kProvinceId, sizeof id);
    const auto a5Byte = static_cast<std::uint8_t>(a5);
    Tally& t = t_tally;
    strength::Slot* slot = strength::SlotFor(t_memo, reinterpret_cast<const void*>(army), id, a4, a5Byte);
    if (slot == nullptr || EU4SB_ArmyStrengthMemoStats.cacheDisabled != 0) {
        ++t.skips;
        return g_loop(flag, army, province, a4, a5);
    }
    ++t.requests;
    const std::uint32_t stamp = strength::StampOf(t_memo, a4, a5Byte);
    if (slot->stamp != stamp) {
        ++t.misses;
        const float value = g_loop(flag, army, province, a4, a5);
        slot->value = value;
        slot->stamp = stamp;
        return value;
    }
    if (g_verifyOnly || (++t.sequence & 1023) == 0) {
        const LONG64 start = sb::Ticks();
        const float value = g_loop(flag, army, province, a4, a5);
        t.ticks += sb::Ticks() - start;
        ++t.verifications;
        if (!strength::SameAnswer(value, slot->value)) {
            InterlockedIncrement64(&EU4SB_ArmyStrengthMemoStats.mismatches);
            if (!g_verifyOnly && InterlockedExchange(&EU4SB_ArmyStrengthMemoStats.cacheDisabled, 1) == 0)
                sb::Log(ANVIL_LOG_WARNING, "army_strength_memo: an answer differed from the game's; back to the game's function");
        }
        return value;
    }
    ++t.hits;
    return slot->value;
}

void WriteFile() {
    const sb::Stats& s = EU4SB_ArmyStrengthMemoStats;
    try {
        const std::wstring path = *g_dir + L"\\army_strength_memo.tsv";
        const std::wstring temp = path + L".tmp";
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            out << "mode\tsearches\trequests\thits\tmisses\tskips\tverifications\tmismatches\tdisabled\trepeat_game_ms\n"
                << (g_verifyOnly ? "verify" : "on") << "\t" << s.duplicateKeys << "\t" << s.requests << "\t" << s.hits << "\t"
                << s.misses << "\t" << s.skips << "\t" << s.verifications << "\t" << s.mismatches << "\t" << s.cacheDisabled
                << "\t" << g_repeatTicks * sb::TicksToMs(1) << "\n";
        }
        MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
    } catch (...) {
    }
}

DWORD WINAPI WriterThread(void*) {
    LONG64 written = -1;
    for (;;) {
        Sleep(kWriteEveryMs);
        if (EU4SB_ArmyStrengthMemoStats.requests != written) {
            written = EU4SB_ArmyStrengthMemoStats.requests;
            WriteFile();
        }
    }
}

}  // namespace

namespace sb {

bool InstallArmyStrengthMemo(anvil::Host& host, Mode mode) {
    if (mode == Mode::Off) return false;
    g_verifyOnly = mode == Mode::Verify;
    try {
        g_dir = new std::wstring(host.data_dir().wstring());
    } catch (...) {
        return false;
    }
    Undo undo;
    host.redirect(g_firstSite, sym::army_strength_first_call, &FirstDetour);
    undo.add(g_firstSite);
    g_first = g_firstSite.next_pointer();
    host.redirect(g_loopSite, sym::army_strength_loop_call, &LoopDetour);
    undo.add(g_loopSite);
    g_loop = g_loopSite.next_pointer();
    undo.keep();
    InterlockedExchange(&EU4SB_ArmyStrengthMemoStats.mode, g_verifyOnly ? 2 : 1);
    InterlockedExchange(&EU4SB_ArmyStrengthMemoStats.initialized, 1);
    HANDLE thread = CreateThread(nullptr, 0, WriterThread, nullptr, 0, nullptr);
    if (thread != nullptr) {
        SetThreadDescription(thread, L"bellows army_strength_memo writer");
        CloseHandle(thread);
    }
    Log(ANVIL_LOG_INFO, "army_strength_memo: %s the army's strength once per province in an AI army search",
        g_verifyOnly ? "verifying" : "computing");
    return true;
}

void ReportArmyStrengthMemo() {
    // No benchmark counters: the report is at its limit of 128 fields; the mode is there, the counters in the log.
    LogStats("army_strength_memo", EU4SB_ArmyStrengthMemoStats);
}

}  // namespace sb
