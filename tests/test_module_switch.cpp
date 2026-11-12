// ResolveModule (bellows#188): what a module runs as, from what Anvil said about it and its setting;
// and plugin.toml's [[modules]] against plugin/settings.toml: one module per setting that switches a
// feature for players (not the diagnostics), its `default` the setting's default other than "off",
// its name the setting's label (so the translations cover it), never a gameplay module.
#include "module_switch.hpp"

#include <toml++/toml.hpp>

#include <cassert>
#include <cstdio>
#include <set>
#include <string>

using sb::Mode;
using sb::ModuleNote;
using sb::ModuleSwitch;
using sb::ResolveModule;

int main() {
    // An Anvil without modules: the setting, as before.
    for (Mode m : {Mode::Off, Mode::On, Mode::Verify, Mode::Startup}) {
        assert(ResolveModule(ModuleSwitch::NoModules, m, true).mode == m);
        assert(ResolveModule(ModuleSwitch::NoModules, m, false).mode == m);
    }
    // Turned off in anvil.toml: off, whatever the setting says.
    assert(ResolveModule(ModuleSwitch::Disabled, Mode::On, true).mode == Mode::Off);
    assert(ResolveModule(ModuleSwitch::Disabled, Mode::On, true).note == ModuleNote::None);
    assert(ResolveModule(ModuleSwitch::Disabled, Mode::Off, false).note == ModuleNote::None);
    // Off by default and turned on only in the settings, as before 2.0: off, with a note.
    assert(ResolveModule(ModuleSwitch::Disabled, Mode::On, false).mode == Mode::Off);
    assert(ResolveModule(ModuleSwitch::Disabled, Mode::On, false).note == ModuleNote::NeedsEnabledList);
    assert(ResolveModule(ModuleSwitch::Disabled, Mode::Verify, false).note == ModuleNote::NeedsEnabledList);
    // Enabled: the setting's mode; its "off" is the old switch for a module on by default.
    assert(ResolveModule(ModuleSwitch::Enabled, Mode::Verify, true).mode == Mode::Verify);
    assert(ResolveModule(ModuleSwitch::Enabled, Mode::Startup, true).mode == Mode::Startup);
    assert(ResolveModule(ModuleSwitch::Enabled, Mode::Off, true).mode == Mode::Off);
    assert(ResolveModule(ModuleSwitch::Enabled, Mode::Off, true).note == ModuleNote::OffBySetting);
    // Off by default, turned on in anvil.toml, the setting at its default "off": on.
    assert(ResolveModule(ModuleSwitch::Enabled, Mode::Off, false).mode == Mode::On);
    assert(ResolveModule(ModuleSwitch::Enabled, Mode::Off, false).note == ModuleNote::None);
    assert(ResolveModule(ModuleSwitch::Enabled, Mode::Verify, false).mode == Mode::Verify);

    // plugin.toml's [[modules]] against settings.toml.
    const toml::table manifest = toml::parse_file(SB_PLUGIN_MANIFEST);
    const toml::table settings = toml::parse_file(SB_SETTINGS_SCHEMA);
    const toml::table& setting = *settings["setting"].as_table();
    const toml::array& modules = *manifest["modules"].as_array();
    assert(manifest["affects_gameplay"].value<bool>() == false);
    std::set<std::string> declared;
    for (const auto& node : modules) {
        const toml::table& m = *node.as_table();
        const std::string id = m["id"].value<std::string>().value();
        assert(declared.insert(id).second);
        const toml::table* s = setting[id].as_table();
        if (s == nullptr) std::printf("module %s has no setting\n", id.c_str());
        assert(s != nullptr);
        assert((*s)["type"].value<std::string>() == "enum");
        assert((*s)["group"].value<std::string>() != "Diagnostics");
        assert(m["name"].value<std::string>() == (*s)["label"].value<std::string>());
        assert(m["description"].value<std::string>() == (*s)["description"].value<std::string>());
        assert(m["affects_gameplay"].value<bool>() == false);
        assert(!m.contains("allow_ironman") && !m.contains("allow_multiplayer"));
        const bool default_on = (*s)["default"].value<std::string>() != "off";
        if (m["default"].value_or(true) != default_on) std::printf("module %s: default differs\n", id.c_str());
        assert(m["default"].value_or(true) == default_on);
    }
    // Every enum setting with "on" and "off" outside the diagnostics is a module, except the two that
    // only refine another module (frame_pacing_input, save_compress_parallel) and verifypath_session.
    const std::set<std::string> not_modules = {"frame_pacing_input", "save_compress_parallel", "verifypath_session"};
    for (const auto& [key, node] : setting) {
        const toml::table& s = *node.as_table();
        const std::string id(key.str());
        if (s["type"].value<std::string>() != "enum" || s["group"].value<std::string>() == "Diagnostics") continue;
        if (not_modules.contains(id)) {
            assert(!declared.contains(id));
            continue;
        }
        if (!declared.contains(id)) std::printf("setting %s is not a module\n", id.c_str());
        assert(declared.contains(id));
    }
    assert(declared.size() == 23 && declared.size() <= 32);  // Anvil's limit is 32 modules

    std::printf("module_switch ok (%zu modules)\n", declared.size());
    return 0;
}
