// bellows.dll's anvil_plugin_load against a stand-in API 2 host: the settings
// it reads, the changes each module requests (none reaches a game: the image
// is this test's own executable and nothing is enabled), the per-module undo
// when Anvil refuses a change, the config.toml migration, the ready event and
// the start-up benchmark fields (a stand-in anvil.benchmark that checks every
// call as Anvil 1.4 does; "old" is an Anvil without the service).
//   test_plugin_load <bellows.dll> <plugin folder> ok|refuse|old
#include <anvil/anvil_benchmark.h>
#include <anvil/anvil_game.h>
#include <anvil/anvil_plugin.h>

#include "settings.gen.h"
#include "symbols.gen.h"

#include <windows.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Change {
    std::string what;
    bool live = true;
};

std::mutex g_mutex;
std::vector<Change> g_changes;  // handle = index + 1
std::vector<std::string> g_log;
std::string g_scenario;
std::wstring g_pluginDir, g_dataDir;
AnvilEventCallback g_ready = nullptr;
void* g_readyContext = nullptr;
AnvilEventCallback g_session = nullptr;
void* g_sessionContext = nullptr;
AnvilEventCallback g_frame = nullptr;
void* g_frameContext = nullptr;
unsigned char g_symbolMemory[256] = {};  // what address_of returns for every symbol

void NeverCalled() { std::abort(); }  // what `next` and `previous` lead to: nothing runs the changes

bool Refused(const std::string& what) {
    return g_scenario == "refuse" && (what == "redirect_call " + std::to_string(SYM_TRIGGER_INTERN_CALL_B) ||
                                      what == "replace_import HeapAlloc");
}

AnvilResult Request(const std::string& what, void** next, AnvilChange** out) {
    std::lock_guard lock(g_mutex);
    if (Refused(what)) return ANVIL_ERROR_CONFLICT;
    g_changes.push_back({what});
    if (next != nullptr) *next = reinterpret_cast<void*>(&NeverCalled);
    if (out != nullptr) *out = reinterpret_cast<AnvilChange*>(g_changes.size());
    return ANVIL_OK;
}

void Log(AnvilPlugin*, AnvilLogLevel, const char* message) {
    std::lock_guard lock(g_mutex);
    g_log.emplace_back(message);
    std::printf("log: %s\n", message);
}

const wchar_t* PluginDir(AnvilPlugin*) { return g_pluginDir.c_str(); }
const wchar_t* DataDir(AnvilPlugin*) { return g_dataDir.c_str(); }

AnvilResult HookFunction(AnvilPlugin*, AnvilSymbol fn, void*, void** next, AnvilChange** out) {
    return Request("hook_function " + std::to_string(fn), next, out);
}
AnvilResult MidHook(AnvilPlugin*, AnvilSymbol site, AnvilCpuCallback, void*, AnvilChange** out) {
    return Request("mid_hook " + std::to_string(site), nullptr, out);
}
AnvilResult ReplaceBlock(AnvilPlugin*, AnvilSymbol block, AnvilCpuCallback, void*, AnvilChange** out) {
    return Request("replace_block " + std::to_string(block), nullptr, out);
}
AnvilResult RedirectCall(AnvilPlugin*, AnvilSymbol site, void*, void** next, AnvilChange** out) {
    return Request("redirect_call " + std::to_string(site), next, out);
}
AnvilResult PatchBytes(AnvilPlugin*, AnvilSymbol site, const void*, size_t, AnvilChange** out) {
    return Request("patch_bytes " + std::to_string(site), nullptr, out);
}
AnvilResult SwapPointer(AnvilPlugin*, AnvilSymbol slot, void*, void** previous, AnvilChange** out) {
    return Request("swap_pointer " + std::to_string(slot), previous, out);
}
AnvilResult ReplaceImport(AnvilPlugin*, const char* module, const char* function, void*, void** next,
                          AnvilChange** out) {
    assert(std::string(module) == "KERNEL32.dll");
    return Request(std::string("replace_import ") + function, next, out);
}
AnvilResult Unsupported() { return ANVIL_ERROR_UNSUPPORTED; }
AnvilResult HookFunctionAt(AnvilPlugin*, void*, const void*, size_t, void*, void**, AnvilChange**) { return Unsupported(); }
AnvilResult MidHookAt(AnvilPlugin*, void*, const void*, size_t, AnvilCpuCallback, void*, AnvilChange**) {
    return Unsupported();
}
AnvilResult Remove(AnvilPlugin*, AnvilChange* change) {
    std::lock_guard lock(g_mutex);
    const auto index = reinterpret_cast<std::uintptr_t>(change);
    if (index == 0 || index > g_changes.size() || !g_changes[index - 1].live) return ANVIL_ERROR_NOT_FOUND;
    g_changes[index - 1].live = false;
    return ANVIL_OK;
}
AnvilResult WrongPhase(AnvilPlugin*) { return ANVIL_ERROR_WRONG_PHASE; }
void NoGroup(AnvilPlugin*) {}
void* AddressOf(AnvilPlugin*, AnvilSymbol) { return g_symbolMemory; }
AnvilResult SymbolValue(AnvilPlugin*, AnvilSymbol, const char*, uint64_t* out) {
    *out = 0x40;  // an RVA inside this executable's headers: readable
    return ANVIL_OK;
}
int SettingBool(AnvilPlugin*, AnvilSetting) { return 0; }
int64_t SettingInt(AnvilPlugin*, AnvilSetting setting) { return setting == CFG_RAM_PREFETCH_BUDGET_MB ? 1024 : 0; }
double SettingFloat(AnvilPlugin*, AnvilSetting) { return 0.0; }
const char* SettingString(AnvilPlugin*, AnvilSetting setting) {
    switch (setting) {
    case CFG_MIMALLOC: return "startup";  // a mode only mimalloc has, also as a benchmark field value
    case CFG_OPEN_WARMUP:
    case CFG_RAM_PREFETCH: return "off";  // their threads would read the game folder
    case CFG_TOKEN_DICT_PROFILE: return "off";  // its ready report walks the (here fake) keyword tree
    case CFG_ATTR_CACHE: return "verify";
    default: return "on";
    }
}
AnvilResult SubscribeSetting(AnvilPlugin*, AnvilSetting, AnvilSettingCallback, void*) { return ANVIL_ERROR_WRONG_KIND; }
AnvilResult RegisterService(AnvilPlugin*, const char*, uint32_t, const void*) { return ANVIL_OK; }
// --- the stand-in anvil.benchmark ---------------------------------------------
struct BenchField {
    uint32_t kind = 0;
    std::vector<std::string> options;
};
bool g_loading = false;  // inside anvil_plugin_load
std::map<std::string, BenchField> g_fields;
std::map<std::string, std::string> g_values;  // field -> value as text
std::vector<std::string> g_benchErrors;       // calls Anvil would refuse
int g_declares = 0, g_dones = 0;
std::string g_learning;
bool g_openCost = false;

// `group.field` or `field`, identifiers of [a-z0-9_], at most 64 characters.
bool BenchName(const char* name) {
    if (name == nullptr) return false;
    const std::string text = name;
    if (text.empty() || text.size() > ANVIL_BENCHMARK_MAX_NAME || text.front() == '.' || text.back() == '.') return false;
    int dots = 0;
    for (char c : text) {
        if (c == '.') {
            ++dots;
        } else if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
            return false;
        }
    }
    return dots <= 1;
}

AnvilResult BenchRefuse(const std::string& why) {  // under g_mutex
    g_benchErrors.push_back(why);
    std::printf("benchmark refused: %s\n", why.c_str());
    return ANVIL_ERROR_INVALID_ARGUMENT;
}

int BenchActive() { return 1; }
AnvilResult BenchDeclare(AnvilPlugin*, const AnvilBenchmarkField* fields, uint32_t count) {
    std::lock_guard lock(g_mutex);
    ++g_declares;
    if (!g_loading) return BenchRefuse("declare outside anvil_plugin_load");
    if (g_fields.size() + count > ANVIL_BENCHMARK_MAX_FIELDS) return BenchRefuse("more than 128 fields");
    std::map<std::string, BenchField> added;  // all or none
    for (uint32_t i = 0; i < count; ++i) {
        const AnvilBenchmarkField& f = fields[i];
        const std::string name = f.name != nullptr ? f.name : "";
        if (!BenchName(f.name)) return BenchRefuse("bad name '" + name + "'");
        if (g_fields.contains(name) || added.contains(name)) return BenchRefuse("declared twice: " + name);
        if (f.kind < ANVIL_BENCHMARK_INT || f.kind > ANVIL_BENCHMARK_FLAG) return BenchRefuse("bad kind: " + name);
        if (f.unit > ANVIL_BENCHMARK_UNIT_PERCENT) return BenchRefuse("bad unit: " + name);
        if ((f.kind == ANVIL_BENCHMARK_ENUM || f.kind == ANVIL_BENCHMARK_FLAG) && f.unit != ANVIL_BENCHMARK_UNIT_NONE) {
            return BenchRefuse("a unit on an enum or a flag: " + name);
        }
        if (f.label != nullptr && std::strlen(f.label) > ANVIL_BENCHMARK_MAX_LABEL) return BenchRefuse("long label: " + name);
        BenchField field{f.kind, {}};
        if (f.kind == ANVIL_BENCHMARK_ENUM) {
            if (f.options == nullptr || f.option_count == 0 || f.option_count > ANVIL_BENCHMARK_MAX_OPTIONS) {
                return BenchRefuse("bad option list: " + name);
            }
            for (uint32_t k = 0; k < f.option_count; ++k) {
                if (!BenchName(f.options[k]) || std::strchr(f.options[k], '.') != nullptr) {
                    return BenchRefuse("bad option of " + name);
                }
                field.options.emplace_back(f.options[k]);
            }
        } else if (f.options != nullptr || f.option_count != 0) {
            return BenchRefuse("options on a field that is not an enum: " + name);
        }
        added.emplace(name, std::move(field));
    }
    g_fields.merge(added);
    return ANVIL_OK;
}
AnvilResult BenchSet(const char* field, uint32_t kind, const std::string& value) {
    std::lock_guard lock(g_mutex);
    const auto found = field != nullptr ? g_fields.find(field) : g_fields.end();
    if (found == g_fields.end()) return BenchRefuse(std::string("not declared: ") + (field ? field : "(null)"));
    if (found->second.kind != kind) return BenchRefuse(std::string("wrong kind: ") + field);
    if (g_dones != 0) return BenchRefuse(std::string("set after done(): ") + field);
    const auto& options = found->second.options;
    if (kind == ANVIL_BENCHMARK_ENUM && std::find(options.begin(), options.end(), value) == options.end()) {
        return BenchRefuse(std::string("not an option of ") + field + ": " + value);
    }
    g_values[field] = value;
    return ANVIL_OK;
}
AnvilResult BenchSetInt(AnvilPlugin*, const char* field, int64_t value) {
    return BenchSet(field, ANVIL_BENCHMARK_INT, std::to_string(value));
}
AnvilResult BenchSetNumber(AnvilPlugin*, const char* field, double value) {
    if (!std::isfinite(value)) {
        std::lock_guard lock(g_mutex);
        return BenchRefuse(std::string("not finite: ") + (field ? field : "(null)"));
    }
    return BenchSet(field, ANVIL_BENCHMARK_NUMBER, std::to_string(value));
}
AnvilResult BenchSetEnum(AnvilPlugin*, const char* field, const char* option) {
    return BenchSet(field, ANVIL_BENCHMARK_ENUM, option != nullptr ? option : "");
}
AnvilResult BenchSetFlag(AnvilPlugin*, const char* field, int value) {
    return BenchSet(field, ANVIL_BENCHMARK_FLAG, value ? "true" : "false");
}
AnvilResult BenchDone(AnvilPlugin*) {
    std::lock_guard lock(g_mutex);
    ++g_dones;
    return ANVIL_OK;
}
AnvilResult BenchLearning(AnvilPlugin*, const char* reason) {
    std::lock_guard lock(g_mutex);
    if (!BenchName(reason) || std::strchr(reason, '.') != nullptr) return BenchRefuse("bad learning reason");
    if (g_dones != 0) return BenchRefuse("learning() after done()");
    g_learning = reason;
    return ANVIL_OK;
}
AnvilResult BenchOpenCost(AnvilPlugin*, uint64_t, double ms) {
    std::lock_guard lock(g_mutex);
    if (!std::isfinite(ms) || ms < 0) return BenchRefuse("bad open cost");
    if (g_dones != 0) return BenchRefuse("open_cost() after done()");
    g_openCost = true;
    return ANVIL_OK;
}
const AnvilBenchmarkService g_benchmark = {sizeof(AnvilBenchmarkService), BenchActive, BenchDeclare,
                                           BenchSetInt, BenchSetNumber, BenchSetEnum, BenchSetFlag,
                                           BenchDone, BenchLearning, BenchOpenCost};

// An Anvil older than 1.4 (scenario "old") has no anvil.benchmark: get_service returns NULL.
const void* GetService(AnvilPlugin*, const char* name, uint32_t version) {
    if (g_scenario == "old" || std::string(name) != ANVIL_BENCHMARK_SERVICE || version != ANVIL_BENCHMARK_SERVICE_VERSION) {
        return nullptr;
    }
    return &g_benchmark;
}
AnvilResult Subscribe(AnvilPlugin*, const char* event, AnvilEventCallback callback, void* context) {
    if (std::string(event) == ANVIL_GAME_EVENT_SESSION) {
        g_session = callback;
        g_sessionContext = context;
        return ANVIL_OK;
    }
    if (std::string(event) == ANVIL_GAME_EVENT_FRAME) {
        g_frame = callback;
        g_frameContext = context;
        return ANVIL_OK;
    }
    assert(std::string(event) == ANVIL_GAME_EVENT_READY);
    g_ready = callback;
    g_readyContext = context;
    return ANVIL_OK;
}
AnvilResult Publish(AnvilPlugin*, const char*, const void*) { return ANVIL_OK; }

bool Logged(const std::string& text) {
    std::lock_guard lock(g_mutex);
    for (const auto& line : g_log) {
        if (line.find(text) != std::string::npos) return true;
    }
    return false;
}

bool Live(const std::string& what) {
    for (const auto& c : g_changes) {
        if (c.live && c.what == what) return true;
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    assert(argc == 4);
    g_scenario = argv[3];
    g_pluginDir = fs::absolute(argv[2]).wstring();
    const fs::path data = fs::temp_directory_path() / ("sb-load-test-" + std::to_string(GetCurrentProcessId()));
    fs::remove_all(data);
    fs::create_directories(data);
    g_dataDir = data.wstring();
    std::ofstream(data / "config.toml", std::ios::binary) << "[modules]\nmetascript_subst = \"off\"\n";

    AnvilGameInfo game{};
    game.size = sizeof(game);
    game.build_id = "eu4-1.37.5-steam-win64";
    game.image_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    game.image_size = 0x1000;
    AnvilHost host{};
    host.size = sizeof(host);
    host.api_version = ANVIL_API_VERSION;
    host.anvil_version = "1.0.0";
    host.game = &game;
    host.log = Log;
    host.plugin_dir = PluginDir;
    host.data_dir = DataDir;
    host.hook_function = HookFunction;
    host.mid_hook = MidHook;
    host.replace_block = ReplaceBlock;
    host.redirect_call = RedirectCall;
    host.patch_bytes = PatchBytes;
    host.swap_pointer = SwapPointer;
    host.replace_import = ReplaceImport;
    host.hook_function_at = HookFunctionAt;
    host.mid_hook_at = MidHookAt;
    host.remove = Remove;
    host.begin_group = WrongPhase;
    host.commit_group = WrongPhase;
    host.abort_group = NoGroup;
    host.address_of = AddressOf;
    host.symbol_value = SymbolValue;
    host.setting_bool = SettingBool;
    host.setting_int = SettingInt;
    host.setting_float = SettingFloat;
    host.setting_string = SettingString;
    host.subscribe_setting = SubscribeSetting;
    host.register_service = RegisterService;
    host.get_service = GetService;
    host.subscribe = Subscribe;
    host.publish = Publish;

    HMODULE dll = LoadLibraryW(fs::absolute(argv[1]).wstring().c_str());
    assert(dll != nullptr);
    const auto load = reinterpret_cast<AnvilResult (*)(const AnvilHost*, AnvilPlugin*)>(
        GetProcAddress(dll, "anvil_plugin_load"));
    assert(load != nullptr);
    g_loading = true;
    assert(load(&host, reinterpret_cast<AnvilPlugin*>(&host)) == ANVIL_OK);  // a failed module never fails the load
    g_loading = false;

    // config.toml was migrated, and its value applies to this start too.
    assert(fs::exists(data / "settings.toml") && fs::exists(data / "config.toml.migrated"));
    assert(Logged("config.toml migrated to settings.toml (1 value(s)"));

    const std::string rest = ", metascript_subst=off, metascript_blocks=on, attr_cache=verify, zip_profile=on, token_dict_cache=on, "
                             "token_dict_profile=off, range_profile=on, range_witness=on, frame_pacing=on, modifier_merge=on, province_modifier_view=on, save_compress=on, tick_profile=on, army_search_profile=on, score_probe=on, monthly_probe=on";
    if (g_scenario != "refuse") {
        assert(Logged("modules: mimalloc=startup, open_warmup=off, ram_prefetch=off, verifypath_listing=on, "
                      "exists_index=on, trigger_intern=on" + rest));
        for (const char* f : {"HeapFree", "HeapReAlloc", "HeapSize", "HeapAlloc", "GetFileAttributesExW",
                              "GetFileAttributesA"}) {
            assert(Live(std::string("replace_import ") + f));
        }
        assert(Live("replace_block " + std::to_string(SYM_VERIFYPATH_STAT_CALL)));
        assert(Live("swap_pointer " + std::to_string(SYM_FS_EXISTS_SLOT)));
        assert(Live("swap_pointer " + std::to_string(SYM_ZIP_IO_READ_SLOT)));
        assert(Live("redirect_call " + std::to_string(SYM_TRIGGER_INTERN_CALL_A)));
        assert(Live("redirect_call " + std::to_string(SYM_TRIGGER_INTERN_CALL_B)));
        assert(Live("redirect_call " + std::to_string(SYM_TOKEN_LOOKUP_TOKENIZER_CALL)));
        assert(Live("redirect_call " + std::to_string(SYM_METASCRIPT_BLOCKS_CALL)));
        assert(Live("hook_function " + std::to_string(SYM_RANGE_MIN_DISTANCE)));
        assert(Live("redirect_call " + std::to_string(SYM_RANGE_CHECK_CALL)));
        assert(Live("mid_hook " + std::to_string(SYM_NESTED_FRAME_COMPARE)));
        assert(Live("mid_hook " + std::to_string(SYM_NESTED_FRAME_RENDER)));
        assert(Live("hook_function " + std::to_string(SYM_DAILY_TICK)));
        assert(Live("hook_function " + std::to_string(SYM_NESTED_FRAME_CHECK)));
        assert(Live("hook_function " + std::to_string(SYM_AI_JOB)));
        assert(Live("hook_function " + std::to_string(SYM_AI_COUNTRY)));
        assert(Live("hook_function " + std::to_string(SYM_ARMY_SEARCH)));
        assert(Live("hook_function " + std::to_string(SYM_SCORE_MOVE)));
        assert(Live("hook_function " + std::to_string(SYM_SCORE_STRENGTH)));
        assert(Live("hook_function " + std::to_string(SYM_SCORE_TABLE)));
        assert(Live("hook_function " + std::to_string(SYM_MONTHLY_TICK)));
        assert(Live("hook_function " + std::to_string(SYM_COUNTRY_MONTH)));
        assert(Live("hook_function " + std::to_string(SYM_PROVINCE_MONTH)));
        assert(Live("hook_function " + std::to_string(SYM_UNITS_MONTH)));
        assert(Live("hook_function " + std::to_string(SYM_PORT_TABLE)));
        assert(Live("hook_function " + std::to_string(SYM_MODIFIER_MERGE)));
        assert(Live("hook_function " + std::to_string(SYM_GAME_RANDOM)));
        assert(Live("hook_function " + std::to_string(SYM_COUNTRY_DAY)));
        assert(Live("hook_function " + std::to_string(SYM_COUNTRY_MODIFIERS)));
    } else {
        // HeapAlloc refused: the three heap imports mimalloc already had are removed. The
        // second trigger_intern site refused: the first one is removed. The rest stays.
        assert(Logged("modules: mimalloc=FAILED, open_warmup=off, ram_prefetch=off, verifypath_listing=on, "
                      "exists_index=on, trigger_intern=FAILED" + rest));
        for (const char* f : {"HeapFree", "HeapReAlloc", "HeapSize", "HeapAlloc"}) {
            assert(!Live(std::string("replace_import ") + f));
        }
        assert(!Live("redirect_call " + std::to_string(SYM_TRIGGER_INTERN_CALL_A)));
        assert(Live("replace_import GetFileAttributesExW"));
        assert(Live("replace_block " + std::to_string(SYM_VERIFYPATH_STAT_CALL)));
    }

    // metascript_subst is off (migrated), so neither of its sites was requested.
    assert(!Live("redirect_call " + std::to_string(SYM_METASCRIPT_CALL_A)));

    // anvil-game's ready event: the modules stop and report on a thread of their own.
    assert(g_ready != nullptr);
    g_ready(ANVIL_GAME_EVENT_READY, nullptr, g_readyContext);
    for (int i = 0; i < 100 && !Logged("trigger_intern: map size"); ++i) Sleep(50);
    assert(Logged("trigger_intern: map size"));

    // mimalloc = "startup" returns freed pages at the ready event and at each session event.
    assert(g_session != nullptr);
    const AnvilGameSession started = {sizeof(AnvilGameSession), ANVIL_GAME_SESSION_STARTED, 0, 0};
    g_session(ANVIL_GAME_EVENT_SESSION, &started, g_sessionContext);
    const bool collects = g_scenario != "refuse";  // refuse: mimalloc failed
    assert(Logged("mimalloc: freed pages returned at ready") == collects);
    assert(Logged("mimalloc: freed pages returned at session start") == collects);
    // The frame handler (mimalloc = "startup" only) collects nothing while little was freed.
    assert(g_frame != nullptr);
    const AnvilGameFrame frame = {sizeof(AnvilGameFrame), 0, 1};
    g_frame(ANVIL_GAME_EVENT_FRAME, &frame, g_frameContext);
    assert(!Logged("mimalloc: freed pages returned at 64 MiB freed"));

    // The benchmark: fields declared in load(), every call one Anvil accepts, done() at the end of the report.
    if (g_scenario == "old") {
        assert(!Logged("benchmark"));  // Bellows runs as before; the log has nothing about the missing service
    } else {
        for (int i = 0; i < 100; ++i) {
            {
                std::lock_guard lock(g_mutex);
                if (g_dones != 0) break;
            }
            Sleep(50);
        }
        std::lock_guard lock(g_mutex);
        std::printf("benchmark: %zu fields declared, %zu set\n", g_fields.size(), g_values.size());
        assert(g_benchErrors.empty());
        assert(g_declares >= 1 && g_dones == 1);
        const bool ok = g_scenario == "ok";
        const std::map<std::string, std::string> modes = {
            {"mimalloc", ok ? "startup" : "failed"}, {"open_warmup", "off"}, {"ram_prefetch", "off"},
            {"verifypath_listing", "on"}, {"exists_index", "on"}, {"trigger_intern", ok ? "on" : "failed"},
            {"metascript_subst", "off"}, {"metascript_blocks", "on"}, {"attr_cache", "verify"}, {"zip_profile", "on"},
            {"token_dict_cache", "on"}, {"token_dict_profile", "off"}, {"range_profile", "on"},
            {"range_witness", "on"}, {"frame_pacing", "on"}, {"modifier_merge", "on"}, {"province_modifier_view", "on"}, {"save_compress", "on"}, {"tick_profile", "on"}, {"army_search_profile", "on"}, {"score_probe", "on"}, {"monthly_probe", "on"},
        };
        for (const auto& [module, mode] : modes) assert(g_values.at(module + ".mode") == mode);
        for (const char* field : {"exists_index.requests", "exists_index.listings", "verifypath_listing.mismatches",
                                  "attr_cache.verifications", "token_dict_cache.entries", "range_profile.checks",
                                  "range_witness.answered"}) {
            assert(g_values.contains(field));
        }
        // open_warmup and ram_prefetch are off: no open cost, no profile being built, their fields stay null.
        assert(!g_openCost && g_learning.empty());
        assert(g_fields.contains("open_warmup.opened") && !g_values.contains("open_warmup.opened"));
        assert(!g_values.contains("metascript_subst.requests"));
    }

    std::printf("plugin load (%s) ok\n", g_scenario.c_str());
    std::fflush(stdout);
    fs::remove_all(data);
    // The plugin's writer threads never end; leave without running its static destructors.
    TerminateProcess(GetCurrentProcess(), 0);
}
