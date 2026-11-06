#include "rename.hpp"

#include <vector>

namespace sb {

namespace fs = std::filesystem;

std::string Utf8Name(const fs::path& name) {
    const std::u8string text = name.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

bool FormerPluginInstalled(const fs::path& plugin_dir) {
    std::error_code ec;
    return fs::exists(plugin_dir.parent_path() / kFormerId / "plugin.toml", ec);
}

std::string MigrateFormerData(const fs::path& data_dir) {
    const fs::path former = data_dir.parent_path() / kFormerId;
    std::error_code ec;
    if (!fs::is_directory(former, ec)) return {};
    if (fs::exists(data_dir, ec) && !fs::is_empty(data_dir, ec)) return {};  // already migrated or in use
    fs::create_directories(data_dir, ec);

    std::vector<fs::path> entries;
    for (const auto& entry : fs::directory_iterator(former, ec)) entries.push_back(entry.path());
    if (ec) return "could not list anvil/data/eu4-startup-boost: " + ec.message();
    int moved = 0;
    for (const auto& from : entries) {
        fs::rename(from, data_dir / from.filename(), ec);
        if (ec) {
            return "moved " + std::to_string(moved) + " of " + std::to_string(entries.size()) +
                   " entries from anvil/data/eu4-startup-boost; stopped at " + Utf8Name(from.filename()) + ": " +
                   ec.message();
        }
        ++moved;
    }
    fs::remove(former, ec);  // only succeeds once empty
    return "moved settings and learned profiles from anvil/data/eu4-startup-boost (" + std::to_string(moved) +
           " entries)";
}

}  // namespace sb
