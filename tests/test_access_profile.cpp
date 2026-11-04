// Fixture for the list helpers of startup_open_warmup.inc (no hooks, no game):
// "all" list from a copy + mod descriptors, ranking, "@\" relativization,
// learned-list dedupe and UTF-8 round trip.
// Ported from the lab (scripts/native/test_startup_open_warmup.cpp).
#include "modules/access_profile.hpp"

#include <windows.h>
#include <algorithm>
#include <cassert>
#include <cstdio>

namespace {

using namespace sb::profile;

void Touch(const std::wstring& path, const std::string& content = "x") {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    assert(h != INVALID_HANDLE_VALUE);
    DWORD written = 0;
    WriteFile(h, content.data(), static_cast<DWORD>(content.size()), &written, nullptr);
    CloseHandle(h);
}

void Dir(const std::wstring& path) { CreateDirectoryW(path.c_str(), nullptr); }

std::size_t IndexOf(const std::vector<std::wstring>& v, const std::wstring& item) {
    const auto it = std::find(v.begin(), v.end(), item);
    return it == v.end() ? static_cast<std::size_t>(-1) : static_cast<std::size_t>(it - v.begin());
}

}  // namespace

int main() {
    wchar_t temp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, temp);
    const std::wstring root = std::wstring(temp) + L"sb-profile-test-" + std::to_wstring(GetCurrentProcessId());
    const std::wstring copy = root + L"\\copy";
    const std::wstring user = root + L"\\user";
    const std::wstring modAbs = root + L"\\workshop\\111";
    Dir(root);

    // A GOG install has its own user folder beside Steam's.
    assert(UserFolderName(root) == L"Europa Universalis IV");
    Touch(root + L"\\goggame-2057001589.info");
    assert(UserFolderName(root) == L"Europa Universalis IV GOG");
    DeleteFileW((root + L"\\goggame-2057001589.info").c_str());

    Dir(copy);
    Dir(copy + L"\\common");
    Dir(copy + L"\\gfx");
    Dir(copy + L"\\music");
    Dir(copy + L"\\gfx\\interface");
    Dir(copy + L"\\gfx\\models");
    Dir(copy + L"\\dlc");
    Dir(user);
    Dir(user + L"\\mod");
    Dir(user + L"\\mod\\local");
    Dir(root + L"\\workshop");
    Dir(modAbs);
    Dir(modAbs + L"\\events");
    Touch(copy + L"\\eu4-lab.exe");
    Touch(copy + L"\\EU4StartupCache.dll");
    Touch(copy + L"\\common\\a.txt");
    Touch(copy + L"\\gfx\\b.dds");
    Touch(copy + L"\\music\\c.ogg");
    Touch(copy + L"\\dlc\\d.zip");
    Touch(copy + L"\\gfx\\interface\\icon.dds");
    Touch(copy + L"\\gfx\\interface\\sprites.gfx");
    Touch(copy + L"\\gfx\\models\\m.mesh");
    Touch(modAbs + L"\\events\\e.txt");
    Dir(modAbs + L"\\gfx");
    Dir(modAbs + L"\\gfx\\interface");
    Touch(modAbs + L"\\gfx\\interface\\modicon.tga");
    Touch(user + L"\\mod\\local\\f.yml");
    Touch(user + L"\\mod\\packed.zip");
    const std::string absPath = Narrow(Normalize(modAbs));
    std::string forward = absPath;
    std::replace(forward.begin(), forward.end(), '\\', '/');
    Touch(user + L"\\mod\\ugc_111.mod", "name=\"A\"\n\tpath=\"" + forward + "\"\nremote_file_id=\"111\"\n");
    Touch(user + L"\\mod\\local.mod", "name=\"B\"\npath=\"mod/local\"\n");
    Touch(user + L"\\mod\\zipped.mod", "name=\"C\"\narchive=\"mod/packed.zip\"\n");
    const std::string json = "{\"enabled_mods\":[\"mod/ugc_111.mod\",\"mod/local.mod\",\"mod/zipped.mod\",\"mod/missing.mod\"],\"disabled_dlcs\":[]}";

    // Parsing.
    const auto mods = ParseEnabledMods(json);
    assert(mods.size() == 4 && mods[0] == "mod/ugc_111.mod" && mods[3] == "mod/missing.mod");
    assert(ParseEnabledMods("{}").empty());
    assert(DescriptorValue("name=\"A\"\n  path = \"C:/x\"\n", "path") == "C:/x");
    assert(DescriptorValue("archive=\"a.zip\"\n", "path").empty());
    assert(Fnv1a(json) != Fnv1a(json + " "));

    // "all" list.
    const auto all = BuildAllList(copy, user, mods, false);
    assert(IndexOf(all, L"@\\eu4-lab.exe") == static_cast<std::size_t>(-1));
    assert(IndexOf(all, L"@\\EU4StartupCache.dll") == static_cast<std::size_t>(-1));
    const std::size_t zip = IndexOf(all, L"@\\dlc\\d.zip");
    const std::size_t txt = IndexOf(all, L"@\\common\\a.txt");
    const std::size_t dds = IndexOf(all, L"@\\gfx\\b.dds");
    const std::size_t ogg = IndexOf(all, L"@\\music\\c.ogg");
    const std::size_t icon = IndexOf(all, L"@\\gfx\\interface\\icon.dds");
    const std::size_t sprites = IndexOf(all, L"@\\gfx\\interface\\sprites.gfx");
    const std::size_t mesh = IndexOf(all, L"@\\gfx\\models\\m.mesh");
    const std::size_t modIcon = IndexOf(all, modAbs + L"\\gfx\\interface\\modicon.tga");
    const std::size_t modTxt = IndexOf(all, modAbs + L"\\events\\e.txt");
    const std::size_t localYml = IndexOf(all, user + L"\\mod\\local\\f.yml");
    const std::size_t packed = IndexOf(all, user + L"\\mod\\packed.zip");
    const std::size_t descriptor = IndexOf(all, user + L"\\mod\\ugc_111.mod");
    const std::size_t npos = static_cast<std::size_t>(-1);
    assert(zip != npos && txt != npos && dds != npos && modTxt != npos && localYml != npos && packed != npos &&
           descriptor != npos && sprites != npos && mesh != npos);
    // Media in folders loaded during play are left out (game and mods alike); definitions stay.
    assert(ogg == npos && icon == npos && modIcon == npos);
    assert(zip < txt && descriptor < txt && packed < txt);   // archives/descriptors first
    assert(txt < dds && modTxt < dds && localYml < dds && sprites < mesh);  // definitions before other files

    // Learned list: unique (case/slash-insensitive), first-use order, "@\" inside the copy.
    const std::vector<std::wstring> opened = {
        copy + L"\\common\\a.txt", copy + L"/COMMON/A.TXT", modAbs + L"\\events\\e.txt", copy + L"\\gfx\\b.dds",
        modAbs + L"/events/e.txt", root + L"\\elsewhere\\ż.txt",
    };
    const auto unique = UniqueStored(opened, copy);
    assert(unique.size() == 4);
    assert(unique[0] == L"@\\common\\a.txt" && unique[1] == modAbs + L"\\events\\e.txt" && unique[2] == L"@\\gfx\\b.dds");
    assert(FromStored(unique[0], copy) == copy + L"\\common\\a.txt");
    assert(FromStored(unique[1], copy) == unique[1]);

    // Merge with the previous learned list: this run's order first, then unseen previous entries.
    const std::vector<std::wstring> previous = {L"@\\gfx\\loadingscreens\\load_7.dds", L"@\\COMMON\\a.txt", L""};
    const auto merged = UniqueStored(opened, copy, previous);
    assert(merged.size() == 5 && merged[3] == unique[3] && merged[4] == L"@\\gfx\\loadingscreens\\load_7.dds");

    // Round trip through the file format (UTF-8, replace existing).
    const std::wstring listFile = root + L"\\list-test.txt";
    assert(WriteList(listFile, unique));
    assert(WriteList(listFile, unique));
    std::string content;
    assert(ReadWhole(listFile, content));
    const auto back = ParseList(content);
    assert(back == unique);
    assert(ParseList("\xEF\xBB\xBF" "a\r\nb\n\nc").size() == 3);

    std::printf("open warmup fixture ok: all=%zu learned=%zu\n", all.size(), unique.size());
    return 0;
}
