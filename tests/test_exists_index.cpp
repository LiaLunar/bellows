// exists_index: the virtual path check and the lookup in listings of real folders.
#include "modules/exists_index_core.hpp"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

namespace {

namespace fs = std::filesystem;
namespace ex = sb::exists;

std::vector<std::string> Split(const char* text) {
    std::vector<std::string> parts;
    return ex::SplitVirtualPath(text, std::strlen(text), parts) ? parts : std::vector<std::string>{};
}

// The folder's listings, read once each, as the module keeps them per root.
struct Root {
    std::wstring path;
    bool symlinks = false;
    std::map<std::wstring, std::shared_ptr<const ex::Listing>> cache;
    int reads = 0;

    bool Exists(const char* text) {
        const std::vector<std::string> parts = Split(text);
        assert(!parts.empty());
        return ex::Found(parts, [&](std::size_t depth) {
            std::wstring directory = path;
            for (std::size_t index = 0; index < depth; ++index) {
                directory += L'\\';
                directory.append(parts[index].begin(), parts[index].end());
            }
            auto found = cache.find(directory);
            if (found != cache.end()) return found->second;
            ++reads;
            return cache[directory] = ex::ReadListing(directory, symlinks);
        });
    }
};

}  // namespace

int main() {
    // Only plain relative paths of printable ASCII, upper-cased per component.
    assert((Split("gfx/interface/x.dds") == std::vector<std::string>{"GFX", "INTERFACE", "X.DDS"}));
    assert(Split("a").size() == 1);
    for (const char* bad : {"", "/gfx", "gfx/", "gfx//x", "gfx/./x", "gfx/../x", ".", "..", "x.", "x ", "gfx\\x",
                            "c:x", "x*", "x?", "x~1", "\"x\"", "x<", "x>", "x|", "\xC3\xA9", "a\tb"}) {
        std::vector<std::string> parts;
        assert(!ex::SplitVirtualPath(bad, std::strlen(bad), parts));
    }

    const fs::path base = fs::temp_directory_path() / ("sb-exists-test-" + std::to_string(GetCurrentProcessId()));
    fs::remove_all(base);
    fs::create_directories(base / "gfx" / "interface");
    std::ofstream(base / "gfx" / "interface" / "x.dds") << "x";
    std::ofstream(base / "gfx" / "a.txt") << "a";
    Root root{base.wstring()};

    assert(root.Exists("gfx/interface/x.dds"));
    assert(root.Exists("GFX/INTERFACE/X.DDS"));
    assert(root.Exists("gfx/interface"));  // a directory exists too, as PHYSFS_exists says
    assert(!root.Exists("gfx/interface/y.dds"));
    assert(!root.Exists("missing/x.dds"));  // the module then asks the game: "not found" is never invented
    assert(!root.Exists("gfx/a.txt/x"));     // a file is not a folder
    const int reads = root.reads;
    assert(root.Exists("gfx/interface/x.dds"));
    assert(root.reads == reads);  // each folder is listed once

    // A missing root folder: a usable, empty listing, so nothing is answered from it.
    Root gone{(base / "gone").wstring()};
    assert(!gone.Exists("gfx"));
    assert(gone.cache.begin()->second->usable && gone.cache.begin()->second->names.empty());

    // A junction counts as a directory.
    const fs::path junction = base / "linked";
    const std::string command = "mklink /J \"" + junction.string() + "\" \"" + (base / "gfx").string() + "\" >nul";
    if (std::system(command.c_str()) == 0) {
        Root fresh{base.wstring()};
        assert(fresh.Exists("linked/interface/x.dds"));
        fs::remove(junction);
    }

    // A file created after its folder was listed is not seen until the next start; the module only
    // answers "exists" from listings, so the game's own check still finds it.
    std::ofstream(base / "gfx" / "b.txt") << "b";
    assert(!root.Exists("gfx/b.txt"));
    Root later{base.wstring()};
    assert(later.Exists("gfx/b.txt"));

    fs::remove_all(base);
    std::puts("exists_index: ok");
    return 0;
}
