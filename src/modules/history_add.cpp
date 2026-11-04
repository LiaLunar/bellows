// history_add (on by default since 1.2): adding an entry the game just read to a dated list, without walking the
// whole list first to see whether that new entry is in it (the reasoning in history_add_core.hpp).
//
// Mid hooks on the two adds in 0xDA66A0 (to the owner's list, then to the list of all such entries) note the
// entry on this thread. A mid hook at the start of 0x7E7CD0, before the walk, jumps past it when the call adds
// that entry. Nothing else about the add changes: the game finds the
// entry's place and links it as it always does. "verify" lets the game walk and walks the list as well, comparing
// the answers; "on" does that for every 1024th entry and goes back to the game's walk for good after a mismatch.
// A background thread writes the counters every 5 s to history_add.tsv in the data directory.
#include "history_add_core.hpp"
#include "modules.hpp"

#include <fstream>
#include <string>

extern "C" {
// requests = adds of a noted entry, hits = walks skipped, verifications = walks compared, mismatches = entries
// the walk found, duplicateKeys = nodes walked in the comparisons, cacheDisabled = 1 after a mismatch,
// mode = 1 on / 2 verify.
__declspec(dllexport) sb::Stats EU4SB_HistoryAddStats = SB_STATS_INIT;
}

namespace {

anvil::Change g_addCall, g_globalCall, g_walk;
std::uintptr_t g_afterWalk = 0;
bool g_verifyOnly = false;
volatile LONG64 g_sequence = 0;
std::wstring* g_dir = nullptr;
constexpr DWORD kWriteEveryMs = 5000;

thread_local std::uintptr_t t_fresh = 0;

// rcx = the list, rdx = the entry; the block's own `mov rdi,rdx` and `mov rsi,rcx` are the code the jump leaves out.
void AtWalk(anvil::Cpu& cpu) noexcept {
    const std::uintptr_t fresh = t_fresh;
    t_fresh = 0;
    if (cpu.rdx != fresh || fresh == 0) return;
    sb::Stats& s = EU4SB_HistoryAddStats;
    InterlockedIncrement64(&s.requests);
    if (s.cacheDisabled != 0) return;
    if (g_verifyOnly || (InterlockedIncrement64(&g_sequence) & 1023) == 0) {
        std::uint64_t walked = 0;
        const void* head = sb::Read<const void*>(reinterpret_cast<const void*>(cpu.rcx), sb::history::kListHead);
        const bool found = sb::history::Contains(head, reinterpret_cast<const void*>(fresh), walked);
        InterlockedIncrement64(&s.verifications);
        InterlockedExchangeAdd64(&s.duplicateKeys, static_cast<LONG64>(walked));
        if (found) {
            InterlockedIncrement64(&s.mismatches);
            if (!g_verifyOnly && InterlockedExchange(&s.cacheDisabled, 1) == 0)
                sb::Log(ANVIL_LOG_WARNING, "history_add: a new entry was in the list already; back to the game's walk");
            return;
        }
        if (g_verifyOnly) return;
    }
    InterlockedIncrement64(&s.hits);
    cpu.rdi = cpu.rdx;
    cpu.rsi = cpu.rcx;
    cpu.rip = g_afterWalk;
}

void WriteFile() {
    const sb::Stats& s = EU4SB_HistoryAddStats;
    try {
        const std::wstring path = *g_dir + L"\\history_add.tsv";
        const std::wstring temp = path + L".tmp";
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            out << "mode\tadds\tskipped\tcompared\tmismatches\tnodes_walked\tdisabled\n"
                << (g_verifyOnly ? "verify" : "on") << "\t" << s.requests << "\t" << s.hits << "\t" << s.verifications
                << "\t" << s.mismatches << "\t" << s.duplicateKeys << "\t" << s.cacheDisabled << "\n";
        }
        MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
    } catch (...) {
    }
}

DWORD WINAPI WriterThread(void*) {
    LONG64 written = -1;
    for (;;) {
        Sleep(kWriteEveryMs);
        if (EU4SB_HistoryAddStats.requests != written) {
            written = EU4SB_HistoryAddStats.requests;
            WriteFile();
        }
    }
}

}  // namespace

namespace sb {

bool InstallHistoryAdd(anvil::Host& host, Mode mode) {
    if (mode == Mode::Off) return false;
    g_verifyOnly = mode == Mode::Verify;
    try {
        g_dir = new std::wstring(host.data_dir().wstring());
    } catch (...) {
        return false;
    }
    g_afterWalk = reinterpret_cast<std::uintptr_t>(host.address(sym::history_add_after_walk));
    Undo undo;
    g_walk = host.mid_hook(sym::history_add_walk, &AtWalk);
    undo.add(g_walk);
    g_addCall = host.mid_hook(sym::history_add_call, [](anvil::Cpu& cpu) { t_fresh = cpu.rdx; });
    undo.add(g_addCall);
    g_globalCall = host.mid_hook(sym::history_add_global_call, [](anvil::Cpu& cpu) { t_fresh = cpu.rdx; });
    undo.add(g_globalCall);
    undo.keep();
    InterlockedExchange(&EU4SB_HistoryAddStats.mode, g_verifyOnly ? 2 : 1);
    InterlockedExchange(&EU4SB_HistoryAddStats.initialized, 1);
    HANDLE thread = CreateThread(nullptr, 0, WriterThread, nullptr, 0, nullptr);
    if (thread != nullptr) {
        SetThreadDescription(thread, L"bellows history_add writer");
        CloseHandle(thread);
    }
    Log(ANVIL_LOG_INFO, "history_add: %s the walk over a dated list for an entry just read",
        g_verifyOnly ? "verifying" : "skipping");
    return true;
}

void ReportHistoryAdd() {
    LogStats("history_add", EU4SB_HistoryAddStats);
}

}  // namespace sb
