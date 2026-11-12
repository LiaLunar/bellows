#include "benchmark.hpp"

#include <iterator>
#include <string>
#include <vector>

namespace sb::bench {

namespace {

anvil::Benchmark g_benchmark;

constexpr const char* kModes[] = {"off", "on", "verify", "startup", "failed"};
constexpr const char* kOpenWarmupProfiles[] = {"learned", "base_and_mods", "all_list"};
constexpr const char* kRamPrefetchProfiles[] = {"none", "learned", "base"};

// The modules whose report is LogStats: its counters become `<module>.<counter>`;
// duplicateKeys (printed as "extra") gets a name of its own per module.
struct CounterModule {
    const char* name;
    const char* extra;  // nullptr: not used by the module
    const char* extra_label;
};
constexpr CounterModule kCounterModules[] = {
    {"mimalloc", nullptr, nullptr},
    {"verifypath_listing", "listings", "Directory listings read"},
    {"exists_index", "listings", "Directory listings read"},
    {"trigger_intern", "index_builds", "Index builds"},
    {"metascript_subst", "tokens", "Tokens substituted"},
    {"metascript_blocks", "blocks", "Blocks resolved"},
    {"attr_cache", "paths", "Paths remembered"},
    {"ram_prefetch", "prefetched", "Members prefetched"},
    {"token_dict_cache", "flushes", "Cache flushes"},
    {"modifier_merge", nullptr, nullptr},
};

struct Counter {
    const char* name;
    volatile LONG64 Stats::*member;
};
constexpr Counter kCounters[] = {
    {"requests", &Stats::requests}, {"hits", &Stats::hits},
    {"misses", &Stats::misses},     {"skips", &Stats::skips},
    {"verifications", &Stats::verifications}, {"mismatches", &Stats::mismatches},
};

// The report's 128 fields are nearly gone: a diagnostic, off for every player, gets no mode field.
constexpr const char* kDiagnostics[] = {"zip_profile",  "token_dict_profile", "range_profile", "tick_profile",
                                        "army_search_profile", "score_probe", "monthly_probe"};

bool IsDiagnostic(const char* module) {
    for (const char* name : kDiagnostics) {
        if (std::string(name) == module) return true;
    }
    return false;
}

const CounterModule* FindCounterModule(const char* module) {
    for (const auto& m : kCounterModules) {
        if (std::string(m.name) == module) return &m;
    }
    return nullptr;
}

constexpr AnvilBenchmarkUnit kCount = ANVIL_BENCHMARK_UNIT_COUNT;
constexpr AnvilBenchmarkUnit kMs = ANVIL_BENCHMARK_UNIT_MS;
constexpr AnvilBenchmarkUnit kMb = ANVIL_BENCHMARK_UNIT_MB;
using B = anvil::Benchmark;

// The fields the modules set in their own reports, beside their log lines.
const AnvilBenchmarkField kModuleFields[] = {
    B::enum_field("open_warmup.profile", kOpenWarmupProfiles, "Warm-up list"),
    B::int_field("open_warmup.entries", kCount, "List entries"),
    B::int_field("open_warmup.opened", kCount, "Files opened"),
    B::int_field("open_warmup.failed", kCount, "Failed opens"),
    B::int_field("open_warmup.not_tried", kCount, "Not tried (ready reached)"),
    B::int_field("open_warmup.threads", kCount, "Worker threads"),
    B::int_field("open_warmup.finish_time", kMs, "Warm-up finished after"),
    B::int_field("open_warmup.learn_recorded", kCount, "Game opens recorded"),
    B::int_field("open_warmup.learn_dropped", kCount, "Game opens not recorded (table full)"),
    B::flag_field("open_warmup.profile_saved", "Profile saved"),
    B::int_field("open_warmup.profile_entries", kCount, "Saved profile entries"),

    B::enum_field("ram_prefetch.profile", kRamPrefetchProfiles, "Prefetch profile"),
    B::int_field("ram_prefetch.members", kCount, "Profile members"),
    B::int_field("ram_prefetch.archives", kCount, "Profile archives"),
    B::number_field("ram_prefetch.prefetched_mb", kMb, "Prefetched"),
    B::number_field("ram_prefetch.served_mb", kMb, "Served from RAM"),
    B::int_field("ram_prefetch.failed", kCount, "Failed members"),
    B::number_field("ram_prefetch.decode_time", kMs, "Decode time (sum over workers)"),
    B::number_field("ram_prefetch.finish_time", kMs, "Workers finished after"),
    B::number_field("ram_prefetch.peak_mb", kMb, "Peak not yet taken"),
    B::int_field("ram_prefetch.budget_mb", kMb, "Memory budget"),
    B::int_field("ram_prefetch.learned", kCount, "Members learned"),

    B::int_field("trigger_intern.map_size", kCount, "Key map size"),
    B::int_field("trigger_intern.indexed", kCount, "Key structures indexed"),

    B::int_field("token_dict_cache.thread_caches", kCount, "Thread caches"),
    B::int_field("token_dict_cache.entries", kCount, "Cache entries"),

    B::int_field("token_dict_profile.tokenizer_lookups", kCount, "Tokenizer lookups"),
    B::int_field("token_dict_profile.tokenizer_hits", kCount, "Tokenizer hits"),
    B::int_field("token_dict_profile.get_or_add_lookups", kCount, "Get-or-add lookups"),

    B::int_field("range_profile.checks", kCount, "Range checks"),
    B::int_field("range_profile.distance_calls", kCount, "Distance calls inside them"),

    B::int_field("range_witness.checks", kCount, "Range checks"),
    B::int_field("range_witness.answered", kCount, "Answered from the witness"),
    B::int_field("range_witness.originals", kCount, "Sent to the game's check"),

    B::int_field("frame_pacing.interval_ms", kMs, "Interval at the fastest speed (0: the game's)"),
    B::int_field("frame_pacing.slow_interval_ms", kMs, "Interval at speeds 1-4 (0: the game's)"),
    B::flag_field("frame_pacing.draw_on_input", "The game's interval while input waits"),
};

}  // namespace

void Join(anvil::Host& host, std::span<const char* const> modules) {
    g_benchmark = anvil::Benchmark(host);
    if (!g_benchmark.available()) return;
    // Anvil copies the names during declare(); these only have to live until then.
    std::vector<std::string> names;
    names.reserve(modules.size() + std::size(kCounterModules) * (std::size(kCounters) + 1));
    std::vector<AnvilBenchmarkField> fields;
    for (const char* module : modules) {
        if (IsDiagnostic(module)) continue;
        names.push_back(std::string(module) + ".mode");
        fields.push_back(B::enum_field(nullptr, kModes, "Mode"));
    }
    for (const auto& m : kCounterModules) {
        for (const auto& c : kCounters) {
            names.push_back(std::string(m.name) + "." + c.name);
            fields.push_back(B::int_field(nullptr, kCount));
        }
        if (m.extra != nullptr) {
            names.push_back(std::string(m.name) + "." + m.extra);
            fields.push_back(B::int_field(nullptr, kCount, m.extra_label));
        }
    }
    for (std::size_t i = 0; i < names.size(); ++i) fields[i].name = names[i].c_str();
    fields.insert(fields.end(), std::begin(kModuleFields), std::end(kModuleFields));
    // A refused declaration is logged by Anvil; the plugin runs on regardless.
    g_benchmark.declare(fields);
}

const anvil::Benchmark& Get() { return g_benchmark; }

void SetMode(const char* module, const char* mode) {
    if (IsDiagnostic(module)) return;
    g_benchmark.set_enum((std::string(module) + ".mode").c_str(), mode);
}

void SetCounters(const char* module, const Stats& s) {
    if (!g_benchmark.available() || (s.mode == 0 && s.requests == 0)) return;  // as LogStats
    const CounterModule* m = FindCounterModule(module);
    if (m == nullptr) return;
    const std::string prefix = std::string(module) + ".";
    for (const auto& c : kCounters) g_benchmark.set((prefix + c.name).c_str(), s.*c.member);
    if (m->extra != nullptr) g_benchmark.set((prefix + m->extra).c_str(), s.duplicateKeys);
}

void NewModSet() { g_benchmark.learning("new_mod_set"); }

void Done() { g_benchmark.done(); }

}  // namespace sb::bench
