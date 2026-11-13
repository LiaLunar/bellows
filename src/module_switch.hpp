#pragma once

// Anvil's modules (ADR 0011 of Anvil, bellows#188): the core turns a Bellows module on or off
// (enabled_modules / disabled_modules in anvil.toml, `default` in plugin.toml's [[modules]]); the
// module's setting keeps its mode ("verify", mimalloc's "startup") and, for one release, its "off".
// Plain data; plugin.cpp passes what the host answered, a test passes values.

#include "settings.hpp"

namespace sb {

// What Anvil said about one module.
enum class ModuleSwitch {
    NoModules,  // an Anvil without modules (before 1.22): the setting alone decides, as before
    Enabled,    // module() gave a handle
    Disabled,   // module() answered ANVIL_ERROR_DISABLED: its code must not run
};

// Why the result differs from the setting, for one line in anvil.log.
enum class ModuleNote {
    None,
    OffBySetting,       // enabled by the core, the setting says "off": off, the old switch (one release)
    NeedsEnabledList,   // off by default, the setting turns it on, the core did not: add it to enabled_modules
};

struct ModuleState {
    Mode mode;
    ModuleNote note;
};

// default_on: the module's `default` in plugin.toml, which is its setting's default other than "off".
// An enabled module whose setting is still at the default "off" of a module that is off by default
// runs "on": the player turned it on in anvil.toml (or the Manager), not in the settings.
constexpr ModuleState ResolveModule(ModuleSwitch sw, Mode setting, bool default_on) noexcept {
    switch (sw) {
    case ModuleSwitch::NoModules:
        return {setting, ModuleNote::None};
    case ModuleSwitch::Disabled:
        return {Mode::Off, setting != Mode::Off && !default_on ? ModuleNote::NeedsEnabledList : ModuleNote::None};
    case ModuleSwitch::Enabled:
        if (setting != Mode::Off) return {setting, ModuleNote::None};
        return default_on ? ModuleState{Mode::Off, ModuleNote::OffBySetting} : ModuleState{Mode::On, ModuleNote::None};
    }
    return {setting, ModuleNote::None};
}

}  // namespace sb
