// Diagnostic (off by default): how often a country's knowledge refresh (0x93FE30) leaves the country as it
// was, by call site and reason (lab docs: docs/simulation-hotspots-after-142.md).
//
// The refresh rebuilds a country's view of the world: lists of other countries, a flag byte per country in
// its diplomatic rows, caches of its provinces, its manpower and more. It runs from the daily country
// update when the country's dirty byte is set, and from commands and effects (an event option, for one).
// If many calls end with the country exactly as it was, a module could skip them once a witness shows that
// the inputs have not changed (as range_witness does for the trade range). This probe measures the upper
// bound of that: around each call it copies the country object (0x9B78 bytes) and hashes the elements of
// its containers (offsets in knowledge_probe_layout.hpp), and after the call it compares. A call that
// changes neither is "unchanged". Writes outside the country object and its containers (other countries,
// provinces, maps and sets of the country) are not seen, so "unchanged" is an upper bound.
//
// The hook only observes (inline, through Anvil): it calls the original and counts. Nothing the game
// computes is changed. The game is usually closed without a clean exit, so a background thread writes the
// counters every 5 s to knowledge_probe*.tsv in the data directory:
//   knowledge_probe.tsv            totals and one row per call site and reason
//   knowledge_probe_words.tsv      8-byte words of the object a call changed, with the number of calls
//   knowledge_probe_containers.tsv containers a call changed
//   knowledge_probe_patterns.tsv   the sets of changes, most frequent first
#include "modules.hpp"
#include "knowledge_probe_core.hpp"

#include <intrin.h>

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

extern "C" {
// requests = calls, hits = unchanged calls, misses = calls the probe could not read, mode = 1 on.
__declspec(dllexport) sb::Stats EU4SB_KnowledgeProbeStats = SB_STATS_INIT;
}

namespace {

namespace kn = sb::knowledge;
using RefreshFn = void (*)(void* country, char* reason);

constexpr DWORD kWriteEveryMs = 5000;
constexpr std::size_t kCountryId = 0x20;  // tag index in bits 32-47
constexpr int kTags = 8192;
constexpr int kSites = 256;
constexpr int kPatterns = 1024;

anvil::HookOf<sym::country_day_93fe30> g_hook;
RefreshFn g_next = nullptr;  // the rest of the hook's chain
std::uintptr_t g_imageBase = 0;
volatile LONG g_dirty = 0;
std::wstring* g_dir = nullptr;
SRWLOCK g_lock = SRWLOCK_INIT;

struct Totals {
    LONG64 calls = 0, unchanged = 0, unreadable = 0, ticks = 0, unchanged_ticks = 0, same_containers = 0;
} g_total;

struct Site {
    std::uint32_t rva = 0;
    const char* reason = nullptr;
    char text[64] = {};
    LONG64 calls = 0, unchanged = 0, ticks = 0, unchanged_ticks = 0, same_containers = 0;
};
Site g_sites[kSites];
int g_siteCount = 0;
LONG64 g_otherSites = 0;

struct Pattern {
    std::uint64_t key = 0;
    LONG64 calls = 0, ticks = 0;
    kn::Change example;
};
Pattern g_patterns[kPatterns];
int g_patternCount = 0;

LONG64 g_wordChanged[kn::kWords] = {};
LONG64 g_containerChanged[kn::kContainerCount] = {};
std::uint64_t g_lastContainers[kTags] = {};
bool g_hasLast[kTags] = {};

struct Scratch {
    unsigned char before[kn::kCountrySize];
    std::uint64_t hash[kn::kContainerCount];
    bool ok[kn::kContainerCount];
};
thread_local Scratch* t_scratch = nullptr;

Scratch* GetScratch() noexcept {
    if (t_scratch == nullptr) {
        // Off the game's heap: one per thread that refreshes, for the process.
        t_scratch = static_cast<Scratch*>(VirtualAlloc(nullptr, sizeof(Scratch), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    }
    return t_scratch;
}

// SEH, so no objects with destructors in these two.
bool Before(const unsigned char* country, Scratch* s) noexcept {
    __try {
        std::memcpy(s->before, country, kn::kCountrySize);
        for (std::size_t i = 0; i < kn::kContainerCount; ++i) s->ok[i] = kn::HashContainer(country, kn::kContainers[i], s->hash[i]);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool After(const unsigned char* country, const Scratch* s, kn::Change* change, std::uint64_t* containers) noexcept {
    __try {
        kn::DiffWords(s->before, country, *change);
        std::uint64_t all = 0;
        for (std::size_t i = 0; i < kn::kContainerCount; ++i) {
            std::uint64_t h = 0;
            const bool ok = kn::HashContainer(country, kn::kContainers[i], h);
            all = kn::Mix(all, ok ? h : 0);
            if (ok != s->ok[i] || (ok && h != s->hash[i])) {
                change->AddContainer(static_cast<std::uint16_t>(i));
                InterlockedIncrement64(&g_containerChanged[i]);
            }
        }
        *containers = all;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void CopyReason(const char* reason, char* text, std::size_t n) noexcept {
    __try {
        std::size_t i = 0;
        for (; reason != nullptr && i + 1 < n && reason[i] != 0; ++i) text[i] = (reason[i] >= 32 && reason[i] < 127 && reason[i] != '\t') ? reason[i] : '?';
        text[i] = 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        text[0] = 0;
    }
}

Site* FindSite(std::uint32_t rva, const char* reason) noexcept {
    for (int i = 0; i < g_siteCount; ++i) {
        if (g_sites[i].rva == rva && g_sites[i].reason == reason) return &g_sites[i];
    }
    if (g_siteCount == kSites) return nullptr;
    Site& s = g_sites[g_siteCount++];
    s.rva = rva;
    s.reason = reason;
    CopyReason(reason, s.text, sizeof s.text);
    return &s;
}

void Record(std::uint32_t rva, const char* reason, LONG64 ticks, bool read, const kn::Change& change, int tag,
            std::uint64_t containers) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    Site* site = FindSite(rva, reason);
    if (site == nullptr) ++g_otherSites;
    ++g_total.calls;
    g_total.ticks += ticks;
    if (site != nullptr) {
        ++site->calls;
        site->ticks += ticks;
    }
    if (!read) {
        ++g_total.unreadable;
        InterlockedIncrement64(&EU4SB_KnowledgeProbeStats.misses);
    } else {
        const bool unchanged = change.None();
        if (unchanged) {
            ++g_total.unchanged;
            g_total.unchanged_ticks += ticks;
            InterlockedIncrement64(&EU4SB_KnowledgeProbeStats.hits);
            if (site != nullptr) {
                ++site->unchanged;
                site->unchanged_ticks += ticks;
            }
        }
        if (tag >= 0 && tag < kTags) {
            if (g_hasLast[tag] && g_lastContainers[tag] == containers) {
                ++g_total.same_containers;
                if (site != nullptr) ++site->same_containers;
            }
            g_lastContainers[tag] = containers;
            g_hasLast[tag] = true;
        }
        // The pattern table: open addressing on the change's key.
        int slot = static_cast<int>(change.key % kPatterns);
        for (int probe = 0; probe < kPatterns; ++probe, slot = (slot + 1) % kPatterns) {
            Pattern& p = g_patterns[slot];
            if (p.calls == 0) {
                p.key = change.key;
                p.example = change;
                ++g_patternCount;
            }
            if (p.key == change.key) {
                ++p.calls;
                p.ticks += ticks;
                break;
            }
            if (g_patternCount >= kPatterns) break;
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    InterlockedIncrement64(&EU4SB_KnowledgeProbeStats.requests);
    InterlockedExchange(&g_dirty, 1);
}

void Detour(void* country, char* reason) {
    const auto rva = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - g_imageBase);
    const auto* self = static_cast<const unsigned char*>(country);
    Scratch* s = GetScratch();
    const bool before = s != nullptr && country != nullptr && Before(self, s);
    const LONG64 start = sb::Ticks();
    g_next(country, reason);
    const LONG64 ticks = sb::Ticks() - start;
    kn::Change change;
    std::uint64_t containers = 0;
    const bool read = before && After(self, s, &change, &containers);
    if (read) {
        // Per word, with atomic adds outside the lock (containers: in After); the writer thread reads them as they are.
        for (std::size_t i = 0; i < kn::kWords; ++i) {
            if (std::memcmp(s->before + i * 8, self + i * 8, 8) != 0) InterlockedIncrement64(&g_wordChanged[i]);
        }
    }
    const int tag = read ? static_cast<std::int16_t>(sb::Read<std::uint64_t>(s->before, kCountryId) >> 32) : -1;
    Record(rva, reason, ticks, read, change, tag, containers);
}

std::string List(const kn::Change& c) {
    std::string out;
    char buf[32];
    for (int i = 0; i < std::min(c.words, kn::Change::kKept); ++i) {
        std::snprintf(buf, sizeof buf, "%s0x%X", out.empty() ? "" : " ", c.word[i] * 8);
        out += buf;
    }
    if (c.words > kn::Change::kKept) out += " ...";
    out += "\t";
    bool first = true;
    for (int i = 0; i < std::min(c.containers, kn::Change::kKept); ++i) {
        std::snprintf(buf, sizeof buf, "%s0x%X", first ? "" : " ", kn::kContainers[c.container[i]].offset);
        out += buf;
        first = false;
    }
    if (c.containers > kn::Change::kKept) out += " ...";
    return out;
}

void WriteFile() {
    const double msPerTick = sb::TicksToMs(1);
    try {
        const std::wstring base = *g_dir + L"\\knowledge_probe";
        auto write = [](const std::wstring& path, const std::string& text) {
            const std::wstring temp = path + L".tmp";
            {
                std::ofstream out(temp, std::ios::binary | std::ios::trunc);
                out << text;
            }
            MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
        };
        std::string main, words, containers, patterns;
        char line[512];
        std::vector<Pattern> pats;
        AcquireSRWLockShared(&g_lock);
        main = "site_rva\treason\tcalls\tunchanged\ttotal_ms\tunchanged_ms\tsame_containers_as_last\n";
        std::snprintf(line, sizeof line, "all\t\t%lld\t%lld\t%.3f\t%.3f\t%lld\n", g_total.calls, g_total.unchanged,
                      g_total.ticks * msPerTick, g_total.unchanged_ticks * msPerTick, g_total.same_containers);
        main += line;
        std::snprintf(line, sizeof line, "unreadable\t\t%lld\t\t\t\t\nother_sites\t\t%lld\t\t\t\t\n", g_total.unreadable, g_otherSites);
        main += line;
        for (int i = 0; i < g_siteCount; ++i) {
            const Site& s = g_sites[i];
            std::snprintf(line, sizeof line, "0x%X\t%s\t%lld\t%lld\t%.3f\t%.3f\t%lld\n", s.rva, s.text, s.calls, s.unchanged,
                          s.ticks * msPerTick, s.unchanged_ticks * msPerTick, s.same_containers);
            main += line;
        }
        for (const Pattern& p : g_patterns) {
            if (p.calls != 0) pats.push_back(p);
        }
        ReleaseSRWLockShared(&g_lock);
        words = "offset\tchanged_calls\n";
        for (std::size_t i = 0; i < kn::kWords; ++i) {
            if (g_wordChanged[i] == 0) continue;
            std::snprintf(line, sizeof line, "0x%zX\t%lld\n", i * 8, g_wordChanged[i]);
            words += line;
        }
        containers = "offset\tshape\tchanged_calls\n";
        for (std::size_t i = 0; i < kn::kContainerCount; ++i) {
            if (g_containerChanged[i] == 0) continue;
            std::snprintf(line, sizeof line, "0x%X\t%d\t%lld\n", kn::kContainers[i].offset, kn::kContainers[i].shape, g_containerChanged[i]);
            containers += line;
        }
        std::sort(pats.begin(), pats.end(), [](const Pattern& a, const Pattern& b) { return a.calls > b.calls; });
        patterns = "calls\ttotal_ms\twords_changed\tcontainers_changed\tfirst_words\tfirst_containers\n";
        for (std::size_t i = 0; i < pats.size() && i < 200; ++i) {
            const Pattern& p = pats[i];
            std::snprintf(line, sizeof line, "%lld\t%.3f\t%d\t%d\t", p.calls, p.ticks * msPerTick, p.example.words, p.example.containers);
            patterns += line;
            patterns += List(p.example);
            patterns += "\n";
        }
        write(base + L".tsv", main);
        write(base + L"_words.tsv", words);
        write(base + L"_containers.tsv", containers);
        write(base + L"_patterns.tsv", patterns);
    } catch (...) {
    }
}

DWORD WINAPI WriterThread(void*) {
    for (;;) {
        Sleep(kWriteEveryMs);
        if (InterlockedExchange(&g_dirty, 0) != 0) WriteFile();
    }
}

}  // namespace

namespace sb {

bool InstallKnowledgeProbe(anvil::Host& host, Mode mode) {
    if (mode == Mode::Off) return false;
    try {
        g_dir = new std::wstring(host.data_dir().wstring());
    } catch (...) {
        return false;
    }
    g_imageBase = host.game().image_base;
    host.hook(g_hook, sym::country_day_93fe30, &Detour);
    g_next = g_hook.next_pointer();
    HANDLE thread = CreateThread(nullptr, 0, WriterThread, nullptr, 0, nullptr);
    if (thread != nullptr) {
        SetThreadDescription(thread, L"bellows knowledge_probe writer");
        CloseHandle(thread);
    }
    InterlockedExchange(&EU4SB_KnowledgeProbeStats.mode, 1);
    InterlockedExchange(&EU4SB_KnowledgeProbeStats.initialized, 1);
    Log(ANVIL_LOG_INFO, "knowledge_probe: observing the country knowledge refresh; counters in knowledge_probe*.tsv every %u s",
        kWriteEveryMs / 1000);
    return true;
}

}  // namespace sb
