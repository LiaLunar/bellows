// Migration from the former plugin id eu4-startup-boost.
#include "rename.hpp"

#include <windows.h>

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

static void Touch(const fs::path& file, const char* text) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

static std::string Slurp(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

int main() {
    const fs::path root = fs::temp_directory_path() / ("bellows-test-rename-" + std::to_string(GetCurrentProcessId()));
    fs::remove_all(root);
    const fs::path plugins = root / "plugins";
    const fs::path data = root / "data";

    // A name outside the ANSI code page: the log line must not throw.
    assert(sb::Utf8Name(fs::path(L"中文.txt")) == "\xE4\xB8\xAD\xE6\x96\x87.txt");

    // Former plugin folder still installed.
    fs::create_directories(plugins / "bellows");
    assert(!sb::FormerPluginInstalled(plugins / "bellows"));
    Touch(plugins / "eu4-startup-boost" / "plugin.toml", "id = \"eu4-startup-boost\"\n");
    assert(sb::FormerPluginInstalled(plugins / "bellows"));

    // Nothing to migrate.
    assert(sb::MigrateFormerData(data / "bellows").empty());

    // Former data moves into an empty (Anvil-created) folder, including subfolders.
    Touch(data / "eu4-startup-boost" / "config.toml", "[modules]\nmimalloc = \"off\"\n");
    Touch(data / "eu4-startup-boost" / "profiles" / "learned.txt", "abc");
    fs::create_directories(data / "bellows");
    const std::string note = sb::MigrateFormerData(data / "bellows");
    assert(note.find("2 entries") != std::string::npos);
    assert(Slurp(data / "bellows" / "config.toml") == "[modules]\nmimalloc = \"off\"\n");
    assert(Slurp(data / "bellows" / "profiles" / "learned.txt") == "abc");
    assert(!fs::exists(data / "eu4-startup-boost"));

    // A second start has nothing to do.
    assert(sb::MigrateFormerData(data / "bellows").empty());

    // Bellows already has data: the former folder is left alone.
    Touch(data / "eu4-startup-boost" / "config.toml", "old");
    assert(sb::MigrateFormerData(data / "bellows").empty());
    assert(Slurp(data / "eu4-startup-boost" / "config.toml") == "old");
    assert(Slurp(data / "bellows" / "config.toml") == "[modules]\nmimalloc = \"off\"\n");

    fs::remove_all(root);
    std::puts("ok");
    return 0;
}
