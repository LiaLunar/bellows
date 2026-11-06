#pragma once

// Bellows was published as "EU4 Startup Boost" (plugin id eu4-startup-boost)
// up to 0.5.0. These helpers keep an update from that id working.

#include <filesystem>
#include <string>

namespace sb {

inline constexpr wchar_t kFormerId[] = L"eu4-startup-boost";

// A name as UTF-8. path::string() throws for a character outside the ANSI code page.
std::string Utf8Name(const std::filesystem::path& name);

// True when anvil/plugins/eu4-startup-boost/plugin.toml still sits next to
// this plugin's folder. Both would patch the same game code.
bool FormerPluginInstalled(const std::filesystem::path& plugin_dir);

// Moves everything from anvil/data/eu4-startup-boost/ (settings, learned
// profiles) into data_dir while data_dir is still empty, then removes the
// emptied former folder. Returns a line for the log; empty when there was
// nothing to move.
std::string MigrateFormerData(const std::filesystem::path& data_dir);

}  // namespace sb
