// verifypath_listing: listings of real folders and the decision for verifyPath's stat calls.
#include "modules/verifypath_listing_core.hpp"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>

namespace {

namespace fs = std::filesystem;
namespace vp = sb::verifypath;

struct Lister {
    std::map<std::wstring, std::shared_ptr<const vp::Listing>> cache;
    int reads = 0;
    std::shared_ptr<const vp::Listing> operator()(const std::wstring& directory) {
        auto found = cache.find(directory);
        if (found != cache.end()) return found->second;
        ++reads;
        return cache[directory] = vp::ReadListing(directory);
    }
};

vp::Answer Ask(Lister& lister, const std::string& root, const char* name, vp::Kind& kind) {
    kind = vp::Kind::Unknown;
    return vp::DecideInRoot(root.c_str(), name, kind, lister);
}

}  // namespace

int main() {
    const fs::path base = fs::temp_directory_path() / ("sb-verifypath-test-" + std::to_string(GetCurrentProcessId()));
    fs::remove_all(base);
    fs::create_directories(base / "gfx" / "interface");
    fs::create_directories(base / "common");
    std::ofstream(base / "gfx" / "interface" / "x.dds") << "x";
    std::ofstream(base / "common" / "a.txt") << "a";
    // The archiver's root: the native folder in UTF-8 with a trailing separator.
    const std::string root = base.string() + "\\";
    Lister lister;
    vp::Kind kind = vp::Kind::Unknown;

    // Existing names, case-insensitive like the platform stat.
    assert(Ask(lister, root, "gfx", kind) == vp::Answer::Exists && kind == vp::Kind::Directory);
    assert(Ask(lister, root, "GFX/Interface/X.DDS", kind) == vp::Answer::Exists && kind == vp::Kind::File);
    assert(Ask(lister, root, "common/a.txt", kind) == vp::Answer::Exists && kind == vp::Kind::File);
    // Missing names, at the end and in the middle of the path.
    assert(Ask(lister, root, "gfx/interface/missing.dds", kind) == vp::Answer::NotFound);
    assert(Ask(lister, root, "missing/x.dds", kind) == vp::Answer::NotFound);
    // Each folder is listed once.
    const int reads = lister.reads;
    assert(Ask(lister, root, "gfx/interface/x.dds", kind) == vp::Answer::Exists);
    assert(lister.reads == reads);

    // Left to the original: a child of a file, unusual components and spellings.
    assert(Ask(lister, root, "common/a.txt/child", kind) == vp::Answer::Original);
    for (const char* name : {"gfx//interface", "gfx/./interface", "gfx/../common", "gfx/interface.", "gfx/interface ",
                             "gfx\\interface", "gfx/inter~1", "gfx/a:b", "gfx/a*", "gfx/\xC3\xA9.dds"}) {
        assert(Ask(lister, root, name, kind) == vp::Answer::Original);
    }
    // A root the directory archiver would not give: too short, or without the trailing separator.
    assert(Ask(lister, "C:\\", "gfx", kind) == vp::Answer::Original);
    assert(Ask(lister, base.string(), "gfx", kind) == vp::Answer::Original);

    // A root whose folder does not exist: every name is missing, as the platform stat says.
    const std::string gone = (base / "gone").string() + "\\";
    assert(Ask(lister, gone, "gfx", kind) == vp::Answer::NotFound);

    // A junction is a directory (the platform stat reports a symlink only for IO_REPARSE_TAG_SYMLINK).
    const fs::path junction = base / "linked";
    const std::string command = "mklink /J \"" + junction.string() + "\" \"" + (base / "gfx").string() + "\" >nul";
    if (std::system(command.c_str()) == 0) {
        Lister fresh;
        assert(Ask(fresh, root, "linked", kind) == vp::Answer::Exists && kind == vp::Kind::Directory);
        assert(Ask(fresh, root, "linked/interface/x.dds", kind) == vp::Answer::Exists && kind == vp::Kind::File);
        fs::remove(junction);
    }

    // A listing read before a file appears does not see it: the module's assumption that game and mod
    // files do not change while the game starts.
    std::ofstream(base / "common" / "b.txt") << "b";
    assert(Ask(lister, root, "common/b.txt", kind) == vp::Answer::NotFound);
    Lister later;
    assert(Ask(later, root, "common/b.txt", kind) == vp::Answer::Exists);

    // The write directory and folders inside it go to the original stat.
    assert(vp::IsWriteRoot("C:\\Users\\u\\EU4\\", "C:\\Users\\u\\EU4"));
    assert(vp::IsWriteRoot("C:\\Users\\u\\EU4", "C:\\Users\\u\\EU4\\"));
    assert(vp::IsWriteRoot("C:\\Users\\u\\EU4\\logs\\", "c:\\users\\U\\eu4\\"));
    assert(!vp::IsWriteRoot("C:\\Users\\u\\EU4x\\", "C:\\Users\\u\\EU4"));
    assert(!vp::IsWriteRoot("C:\\Users\\u\\", "C:\\Users\\u\\EU4"));
    assert(!vp::IsWriteRoot("C:\\Games\\EU4\\", nullptr));
    assert(!vp::IsWriteRoot("C:\\Games\\EU4\\", ""));

    fs::remove_all(base);
    // A listing is good for 30 s after it was read (verifypath_session).
    const auto fresh = vp::ReadListing(base.wstring());
    assert(fresh->readAtMs != 0);
    assert(!vp::Expired(*fresh, fresh->readAtMs));
    assert(!vp::Expired(*fresh, fresh->readAtMs + vp::kSessionListingMs - 1));
    assert(vp::Expired(*fresh, fresh->readAtMs + vp::kSessionListingMs));

    // listing_lookup: the flags `test eax,eax` leaves for the game's jne (ZF), and SF/PF/CF/OF as the test.
    static_assert(vp::TestFlags(0x202, 0) == (0x202 | 0x40 | 0x4));       // zero: ZF, PF (even parity)
    static_assert(vp::TestFlags(0x202 | 0x40 | 0x4 | 0x1 | 0x800, 1) == 0x202);  // one: ZF, PF, CF, OF cleared
    static_assert(vp::TestFlags(0x202, 3) == (0x202 | 0x4));              // 0b11: even parity
    static_assert(vp::TestFlags(0x202, 0x80000000u) == (0x202 | 0x80 | 0x4));  // SF; low byte 0 -> PF
    static_assert(vp::TestFlags(0x202 | 0x10, 1) == (0x202 | 0x10));      // AF kept

    std::puts("verifypath_listing: ok");
    return 0;
}
