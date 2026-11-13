// Bellows: the Anvil plugin (API 2, C++ wrapper anvil.hpp).
#include "benchmark.hpp"
#include "common.hpp"
#include "module_switch.hpp"
#include "modules/modules.hpp"
#include "rename.hpp"
#include "settings.hpp"

#include <anvil/anvil_game.h>

#include "settings.gen.hpp"  // cfg::<name>, written by anvil gen from plugin/settings.toml

#include <cctype>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace {

// A step that fails only stops that step. Nothing above FinishAfterReady catches, and an exception
// out of a thread ends the game; in load() it would cost the whole plugin for a note in the log.
template <class Step>
void Guarded(const char* name, Step&& step) noexcept {
    try {
        step();
    } catch (const std::exception& e) {
        sb::Log(ANVIL_LOG_WARNING, "%s: %s", name, e.what());
    } catch (...) {
        sb::Log(ANVIL_LOG_WARNING, "%s: failed", name);
    }
}

DWORD WINAPI FinishAfterReady(void*) {
    struct Step {
        const char* name;
        void (*run)();
    };
    static constexpr Step steps[] = {
        {"open_warmup finish", sb::FinishOpenWarmup},
        {"ram_prefetch finish", sb::FinishRamPrefetch},
        {"mimalloc report", sb::ReportMimalloc},
        {"verifypath_listing report", sb::ReportVerifyPathListing},
        {"listing_lookup report", sb::ReportListingLookup},
        {"exists_index report", sb::ReportExistsIndex},
        {"trigger_intern report", sb::ReportTriggerIntern},
        {"metascript_subst report", sb::ReportMetascriptSubst},
        {"metascript_blocks report", sb::ReportMetascriptBlocks},
        {"open_warmup report", sb::ReportOpenWarmup},
        {"attr_cache report", sb::ReportAttrCache},
        {"ram_prefetch report", sb::ReportRamPrefetch},
        {"zip_profile report", sb::ReportZipProfile},
        {"token_dict_profile report", sb::ReportTokenDictProfile},
        {"token_dict_cache report", sb::ReportTokenDictCache},
        {"range_profile report", sb::ReportRangeProfile},
        {"range_witness report", sb::ReportRangeWitness},
        {"frame_pacing report", sb::ReportFramePacing},
        {"pops_pacing report", sb::ReportPopsPacing},
        {"never_throttle report", sb::ReportNeverThrottle},
        {"modifier_merge report", sb::ReportModifierMerge},
        {"port_table report", sb::ReportPortTable},
        {"province_modifier_view report", sb::ReportProvinceModifierView},
        {"army_search_memo report", sb::ReportArmySearchMemo},
        {"save_compress report", sb::ReportSaveCompress},
        {"transfer_skip report", sb::ReportTransferSkip},
        {"history_add report", sb::ReportHistoryAdd},
        {"benchmark report", sb::bench::Done},  // the profiles are saved and the reports made
    };
    for (const Step& step : steps) Guarded(step.name, step.run);
    return 0;
}

// anvil-game publishes its ready event on the game's main thread. The modules
// stop at once; saving the profiles waits for the workers (up to seconds), so
// it runs on a thread of its own, as it did on Anvil's watcher thread before.
void OnGameReady() {
    if (InterlockedExchange(&sb::g_gameReady, 1) != 0) return;
    sb::CollectMimalloc("ready");  // on the main thread: mimalloc collects through a thread it knows
    HANDLE thread = CreateThread(nullptr, 0, &FinishAfterReady, nullptr, 0, nullptr);
    if (thread != nullptr) {
        SetThreadDescription(thread, L"bellows ready report");
        CloseHandle(thread);
    } else {
        FinishAfterReady(nullptr);
    }
}

// True when ANVIL_BELLOWS_<NAME> is set: Anvil applied it over the player's
// file, so a value migrated from config.toml must not replace it.
bool Overridden(const std::string& name) {
    std::string variable = "ANVIL_BELLOWS_";
    for (char c : name) variable += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return GetEnvironmentVariableA(variable.c_str(), nullptr, 0) != 0;
}

struct Module {
    const char* name;  // also the setting's name in plugin/settings.toml, and the module's id in plugin.toml
    AnvilSetting setting;
    bool (*install)(anvil::Host&, sb::Mode);
    // A module of plugin.toml's [[modules]] (Anvil 1.22, ADR 0011), with its `default`; the diagnostics are
    // not: they stay settings of the plugin itself.
    bool declared = false;
    bool default_on = true;
    sb::Mode mode = sb::Mode::Off;
    sb::ModuleSwitch state = sb::ModuleSwitch::NoModules;
    std::optional<anvil::Host> handle;  // the module's host; the plugin's own without modules
};

// A module of [[modules]]: `default` is the setting's default other than "off" (tests/test_module_switch.cpp).
constexpr bool kOn = true, kOff = false;
Module Declared(const char* name, AnvilSetting setting, bool (*install)(anvil::Host&, sb::Mode), bool default_on) {
    Module m{name, setting, install};
    m.declared = true;
    m.default_on = default_on;
    return m;
}

// Asks Anvil about each module of [[modules]] (Anvil 1.22, ADR 0011) and sets what it runs as
// (sb::ResolveModule). Without modules in the host (an older Anvil) nothing changes: the settings decide.
template <size_t N> void ResolveModules(anvil::Host& host, Module (&modules)[N]) {
    std::string turned_off, need_list, old_off;
    for (auto& m : modules) {
        if (!m.declared) continue;
        try {
            m.handle = host.module(m.name);
            m.state = m.handle ? sb::ModuleSwitch::Enabled : sb::ModuleSwitch::Disabled;
        } catch (const anvil::Error& e) {
            // UNSUPPORTED: an Anvil without modules. NOT_DECLARED would be a plugin.toml out of step with the
            // table above (tests/test_module_switch.cpp checks it); the module then runs as before.
            if (e.result() != ANVIL_ERROR_UNSUPPORTED) sb::Log(ANVIL_LOG_WARNING, "%s: %s", m.name, e.what());
            m.state = sb::ModuleSwitch::NoModules;
        }
        const sb::ModuleState r = sb::ResolveModule(m.state, m.mode, m.default_on);
        m.mode = r.mode;
        std::string* list = r.note == sb::ModuleNote::NeedsEnabledList ? &need_list
                            : r.note == sb::ModuleNote::OffBySetting   ? &old_off
                            // a module off by default and left so is not news
                            : m.state == sb::ModuleSwitch::Disabled && m.default_on ? &turned_off
                                                                                    : nullptr;
        if (list != nullptr) *list += std::string(list->empty() ? "" : ", ") + m.name;
    }
    if (!turned_off.empty()) sb::Log(ANVIL_LOG_INFO, "modules off in anvil.toml: %s", turned_off.c_str());
    if (!need_list.empty()) {
        sb::Log(ANVIL_LOG_WARNING, "settings.toml turns on %s, but Anvil keeps these modules off by default now: "
                                   "turn them on in Anvil Manager or add \"bellows.<module>\" to enabled_modules in "
                                   "anvil/anvil.toml (the setting still chooses \"verify\")",
                need_list.c_str());
    }
    if (!old_off.empty()) {
        sb::Log(ANVIL_LOG_WARNING, "settings.toml turns off %s with the module setting; this works in Bellows 2.x only: "
                                   "turn them off in Anvil Manager or add \"bellows.<module>\" to disabled_modules in "
                                   "anvil/anvil.toml",
                old_off.c_str());
    }
}

struct Bellows : anvil::Plugin {
    void load(anvil::Host& host) override {
        const std::filesystem::path plugin_dir = host.plugin_dir();
        const std::filesystem::path data_dir = host.data_dir();
        // The old names are a note in the log: nothing here may stop the modules.
        Guarded("former data", [&] {
            if (sb::FormerPluginInstalled(plugin_dir)) {
                sb::Log(ANVIL_LOG_INFO, "anvil/plugins/eu4-startup-boost (the former name of Bellows, plugin API 1) is "
                                        "still installed; Anvil does not load it, so the folder can be deleted");
            }
            if (const std::string note = sb::MigrateFormerData(data_dir); !note.empty()) {
                sb::Log(ANVIL_LOG_INFO, "%s", note.c_str());
            }
            for (const auto& note : sb::FormerEnvironmentNotes()) sb::Log(ANVIL_LOG_WARNING, "%s", note.c_str());
        });

        Module modules[] = {
            Declared("mimalloc", cfg::mimalloc.id, sb::InstallMimalloc, kOn),  // first: before the game's runtime allocates
            Declared("open_warmup", cfg::open_warmup.id, sb::InstallOpenWarmup, kOn),  // early: its threads must run ahead of the game
            Declared("ram_prefetch", cfg::ram_prefetch.id, sb::InstallRamPrefetch, kOn),  // early as well: workers decode ahead of the game
            Declared("verifypath_listing", cfg::verifypath_listing.id, sb::InstallVerifyPathListing, kOn),
            Declared("listing_lookup", cfg::listing_lookup.id, sb::InstallListingLookup, kOff),  // prototype, off; needs verifypath_listing
            Declared("exists_index", cfg::exists_index.id, sb::InstallExistsIndex, kOn),
            Declared("trigger_intern", cfg::trigger_intern.id, sb::InstallTriggerIntern, kOn),
            Declared("metascript_subst", cfg::metascript_subst.id, sb::InstallMetascriptSubst, kOn),
            Declared("metascript_blocks", cfg::metascript_blocks.id, sb::InstallMetascriptBlocks, kOn),
            Declared("attr_cache", cfg::attr_cache.id, sb::InstallAttrCache, kOff),
            {"zip_profile", cfg::zip_profile.id, sb::InstallZipProfile},  // diagnostic, off by default
            // token_dict_cache and token_dict_profile redirect the same call: both chain, the profile then counts
            // only the cache's misses.
            Declared("token_dict_cache", cfg::token_dict_cache.id, sb::InstallTokenDictCache, kOn),
            {"token_dict_profile", cfg::token_dict_profile.id, sb::InstallTokenDictProfile},  // diagnostic
            {"range_profile", cfg::range_profile.id, sb::InstallRangeProfile},  // diagnostic
            // range_witness answers before 0x3526B0 runs: range_profile then sees only the walks it still makes.
            Declared("range_witness", cfg::range_witness.id, sb::InstallRangeWitness, kOn),
            Declared("frame_pacing", cfg::frame_pacing.id, sb::InstallFramePacing, kOn),
            Declared("pops_pacing", cfg::pops_pacing.id, sb::InstallPopsPacing, kOn),
            Declared("never_throttle", cfg::never_throttle.id, sb::InstallNeverThrottle, kOff),
            Declared("modifier_merge", cfg::modifier_merge.id, sb::InstallModifierMerge, kOn),
            // hooks the entry of 0x350B80; monthly_probe's hook on it chains.
            Declared("port_table", cfg::port_table.id, sb::InstallPortTable, kOn),
            Declared("province_modifier_view", cfg::province_modifier_view.id, sb::InstallProvinceModifierView, kOn),
            // redirects the call of 0x1022B0 in 0xEAB50; army_search_profile hooks its entry, so both chain.
            Declared("army_search_memo", cfg::army_search_memo.id, sb::InstallArmySearchMemo, kOn),
            Declared("save_compress", cfg::save_compress.id, sb::InstallSaveCompress, kOn),
            Declared("transfer_skip", cfg::transfer_skip.id, sb::InstallTransferSkip, kOn),
            Declared("history_add", cfg::history_add.id, sb::InstallHistoryAdd, kOn),
            Declared("checksum_defer", cfg::checksum_defer.id, sb::InstallChecksumDefer, kOff),  // prototype, off by default
            // diagnostic; its hook on 0x769EA0 sits at the entry, frame_pacing's inside the function.
            {"tick_profile", cfg::tick_profile.id, sb::InstallTickProfile},
            {"army_search_profile", cfg::army_search_profile.id, sb::InstallArmySearchProfile},  // diagnostic
            {"score_probe", cfg::score_probe.id, sb::InstallScoreProbe},  // diagnostic
            {"monthly_probe", cfg::monthly_probe.id, sb::InstallMonthlyProbe},  // diagnostic
        };
        std::vector<const char*> names;
        for (const auto& m : modules) names.push_back(m.name);
        Guarded("benchmark", [&] { sb::bench::Join(host, names); });  // the Benchmark tab, not the modules

        // The values Anvil validated (defaults, the player's file, ANVIL_BELLOWS_* overrides).
        for (auto& m : modules) {
            const char* text = host.c_api().setting_string(host.self(), m.setting);
            m.mode = sb::ParseMode(text != nullptr ? text : "").value_or(sb::Mode::Off);
        }
        std::int64_t budget = cfg::ram_prefetch_budget_mb.get();

        // First start after the update from config.toml: its values apply now too.
        Guarded("config.toml", [&] {
            const sb::Migration migration = sb::MigrateConfigFile(data_dir, plugin_dir / "settings.toml");
            for (const auto& note : migration.notes) sb::Log(ANVIL_LOG_WARNING, "%s", note.c_str());
            for (const auto& value : migration.values) {
                if (Overridden(value.name)) continue;
                if (value.name == "ram_prefetch_budget_mb") {
                    budget = std::stoll(value.literal);
                    continue;
                }
                for (auto& m : modules) {
                    if (value.name != m.name) continue;
                    // The literal is a quoted option name.
                    m.mode = sb::ParseMode(value.literal.substr(1, value.literal.size() - 2)).value_or(m.mode);
                }
            }
        });
        ResolveModules(host, modules);
        sb::SetRamPrefetchBudget(static_cast<std::uint64_t>(budget));
        sb::SetFramePacingInterval(cfg::frame_pacing_interval_ms.get(),
                                   cfg::frame_pacing_input.get() == cfg::frame_pacing_input.draw);
        // restart = false: a change in the overlay applies at the next check of 0x769EA0.
        Guarded("frame_pacing settings", [&] {
            const auto apply = [] {
                sb::ChangeFramePacing(cfg::frame_pacing_interval_ms.get(),
                                      cfg::frame_pacing_input.get() == cfg::frame_pacing_input.draw);
            };
            host.subscribe_setting(cfg::frame_pacing_interval_ms, apply);
            host.subscribe_setting(cfg::frame_pacing_input, apply);
        });
        sb::SetVerifyPathSession(cfg::verifypath_session.get() == cfg::verifypath_session.on);
        sb::SetSaveCompressParallel(cfg::save_compress_parallel.get() == cfg::save_compress_parallel.on       ? 1
                                    : cfg::save_compress_parallel.get() == cfg::save_compress_parallel.verify ? 2
                                                                                                              : 0);

        std::string summary;
        for (auto& m : modules) {
            bool active = false;
            try {
                // A module's changes belong to its handle (Anvil 1.22); the diagnostics' to the plugin.
                anvil::Host& owner = m.handle ? *m.handle : host;
                if (m.state != sb::ModuleSwitch::Disabled) active = m.install(owner, m.mode);
            } catch (const std::exception& e) {
                // Mostly anvil::Error: Anvil refused a change (and logged why). The module's
                // sb::Undo removed its other changes; the other modules stay.
                sb::Log(ANVIL_LOG_WARNING, "%s: %s; module off", m.name, e.what());
            }
            const std::string mode = active ? sb::ModeName(m.mode) : (m.mode == sb::Mode::Off ? "off" : "failed");
            // "off" for a module the player turned off in anvil.toml, as for one off by its setting.
            summary += std::string(summary.empty() ? "" : ", ") + m.name + "=" + (mode == "failed" ? "FAILED" : mode);
            sb::bench::SetMode(m.name, mode.c_str());
        }
        sb::Log(ANVIL_LOG_INFO, "modules: %s", summary.c_str());

        // The plugin.toml dependency makes anvil-game load first (both are early).
        host.subscribe(ANVIL_GAME_EVENT_READY, [] { OnGameReady(); });
        // A save loaded or left frees most of what mimalloc still holds in "startup" mode.
        host.subscribe(ANVIL_GAME_EVENT_SESSION, [](const void* payload) {
            const auto* session = static_cast<const AnvilGameSession*>(payload);
            const bool ended = session != nullptr && session->change == ANVIL_GAME_SESSION_ENDED;
            sb::CollectMimalloc(ended ? "session end" : "session start");
            sb::NotePopsSession(session != nullptr && !ended && session->multiplayer != 0);
            sb::NoteNeverThrottleSession(session != nullptr && !ended && session->multiplayer != 0);
        });
        // ... and so does a save loaded from the in-game menu, which publishes no session event in
        // anvil-game 1.5: a frame collects once enough was freed (a counter read otherwise).
        if (modules[0].mode == sb::Mode::Startup) {
            host.subscribe(ANVIL_GAME_EVENT_FRAME, [] { sb::CollectMimallocIfDue(); });
        }
        // A failed module only turns itself off: load() returns normally.
    }
};

}  // namespace

ANVIL_PLUGIN(Bellows)
