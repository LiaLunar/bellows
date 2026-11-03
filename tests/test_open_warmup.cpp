// open_warmup on real folders: the list a start chooses (learned profile, base layer + mods, the
// full "all" list), which opens learning records and which rows it keeps, the open ahead of the
// game (nothing changes in the file), and a learned profile warming the next start.
#include "modules/open_warmup_core.hpp"

#include <windows.h>

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace warmup = sb::warmup;
using namespace sb::profile;

void Touch(const fs::path& path, const std::string& content = "x") {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    assert(h != INVALID_HANDLE_VALUE);
    DWORD written = 0;
    WriteFile(h, content.data(), static_cast<DWORD>(content.size()), &written, nullptr);
    CloseHandle(h);
}

struct Calls {
    int count = 0;
    bool skipGame = false;
};

auto AllList(Calls& calls, std::vector<std::wstring> result) {
    return [&calls, result](bool skipGame) {
        ++calls.count;
        calls.skipGame = skipGame;
        return result;
    };
}

void TestChooseList(const fs::path& root) {
    const std::wstring learned = (root / L"learned.txt").wstring();
    const std::wstring base = (root / L"base.txt").wstring();
    const std::wstring missing = (root / L"missing.txt").wstring();
    const std::vector<std::wstring> mods = {L"C:\\mods\\a\\events\\e.txt"};
    std::vector<std::wstring> lines;
    Calls calls;

    // A learned profile with entries: used as it is, no folder scan.
    Touch(learned, "@\\common\\a.txt\n@\\gfx\\b.dds\n");
    Touch(base, "@\\map\\default.map\n");
    assert(warmup::ChooseList(learned, base, AllList(calls, mods), lines) == warmup::Learned);
    assert(calls.count == 0 && lines == std::vector<std::wstring>({L"@\\common\\a.txt", L"@\\gfx\\b.dds"}));

    // No learned profile (or an empty one): the base layer, then the mods' "all" list without the game folder.
    for (const std::wstring& noProfile : {missing, learned}) {
        if (noProfile == learned) Touch(learned, "\r\n");
        lines.clear();
        calls = {};
        assert(warmup::ChooseList(noProfile, base, AllList(calls, mods), lines) == warmup::BaseAndMods);
        assert(calls.count == 1 && calls.skipGame);
        assert(lines == std::vector<std::wstring>({L"@\\map\\default.map", mods[0]}));
    }

    // No base layer either (missing or empty): the full "all" list, game folder included.
    for (bool emptyBase : {false, true}) {
        if (emptyBase) Touch(base, "");
        lines.clear();
        calls = {};
        assert(warmup::ChooseList(missing, emptyBase ? base : missing, AllList(calls, mods), lines) == warmup::All);
        assert(calls.count == 1 && !calls.skipGame && lines == mods);
    }
}

void TestLearning() {
    const HANDLE some = reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(0x40));
    const wchar_t* path = L"C:\\Games\\EU4\\common\\a.txt";
    assert(warmup::Recorded(some, path, GENERIC_READ, OPEN_EXISTING));
    assert(!warmup::Recorded(INVALID_HANDLE_VALUE, path, GENERIC_READ, OPEN_EXISTING));  // failed open
    assert(!warmup::Recorded(some, nullptr, GENERIC_READ, OPEN_EXISTING));
    assert(!warmup::Recorded(some, path, GENERIC_READ | GENERIC_WRITE, OPEN_EXISTING));  // a write
    assert(!warmup::Recorded(some, path, GENERIC_WRITE, CREATE_ALWAYS));
    assert(!warmup::Recorded(some, path, GENERIC_READ, OPEN_ALWAYS));  // may create the file
    assert(!warmup::Recorded(some, path, FILE_READ_ATTRIBUTES, OPEN_EXISTING));

    // Rows: whole paths up to kPathChars count, longer ones are dropped; a row claimed but not
    // written yet, and claims past the table, are not read.
    auto rows = std::make_unique<warmup::LearnRecord[]>(4);
    const std::wstring fits(warmup::kPathChars, L'a');
    const std::wstring tooLong(warmup::kPathChars + 1, L'b');
    warmup::Record(rows[0], path);
    warmup::Record(rows[1], tooLong.c_str());
    warmup::Record(rows[2], fits.c_str());
    // rows[3]: claimed, still being written.
    assert(!rows[1].done && std::wstring(rows[1].path).size() == warmup::kPathChars);
    assert(warmup::RecordedPaths(rows.get(), 4, 4) == std::vector<std::wstring>({path, fits}));
    assert(warmup::RecordedPaths(rows.get(), 1000, 4).size() == 2);  // claims past the table
    assert(warmup::RecordedPaths(rows.get(), 1, 4) == std::vector<std::wstring>({path}));
    assert(warmup::RecordedPaths(rows.get(), 0, 4).empty());
}

void TestOpenAhead(const fs::path& root) {
    const fs::path file = root / L"common" / L"ż.txt";
    fs::create_directories(file.parent_path());
    Touch(file, "the game's bytes");
    const auto size = fs::file_size(file);
    const auto written = fs::last_write_time(file);
    assert(warmup::OpenAhead(file.wstring()));
    assert(fs::file_size(file) == size && fs::last_write_time(file) == written);  // nothing changed

    assert(!warmup::OpenAhead((root / L"common" / L"missing.txt").wstring()));
    assert(!warmup::OpenAhead((root / L"no-folder" / L"x.txt").wstring()));
    assert(!warmup::OpenAhead((root / L"common").wstring()));  // a folder: left to the game

    // A file another program writes (sharing reads) is opened; one it holds exclusively is not,
    // and the game's own open would fail the same way.
    HANDLE writer = CreateFileW(file.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    assert(writer != INVALID_HANDLE_VALUE);
    assert(warmup::OpenAhead(file.wstring()));
    CloseHandle(writer);
    HANDLE exclusive = CreateFileW(file.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    assert(exclusive != INVALID_HANDLE_VALUE);
    assert(!warmup::OpenAhead(file.wstring()));
    CloseHandle(exclusive);
    // The warm-up's handle is closed at once: the game can then open the file exclusively.
    exclusive = CreateFileW(file.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    assert(exclusive != INVALID_HANDLE_VALUE);
    CloseHandle(exclusive);
}

// One start records the game's opens and saves them; the next start chooses that profile and
// opens every entry ahead of the game.
void TestLearnThenWarm(const fs::path& root) {
    const fs::path game = root / L"game";
    const fs::path mod = root / L"workshop" / L"111";
    fs::create_directories(game / L"common");
    fs::create_directories(mod / L"events");
    Touch(game / L"common" / L"a.txt");
    Touch(mod / L"events" / L"e.txt");
    const std::vector<std::wstring> opens = {(game / L"common" / L"a.txt").wstring(),
                                             (mod / L"events" / L"e.txt").wstring(),
                                             (game / L"COMMON" / L"A.TXT").wstring()};
    auto rows = std::make_unique<warmup::LearnRecord[]>(opens.size());
    for (std::size_t index = 0; index < opens.size(); ++index) warmup::Record(rows[index], opens[index].c_str());
    const std::wstring target = (root / L"profiles-key.txt").wstring();
    const auto stored = UniqueStored(warmup::RecordedPaths(rows.get(), static_cast<LONG>(opens.size()), opens.size()),
                                     game.wstring());
    assert(stored.size() == 2 && stored[0] == L"@\\common\\a.txt");
    assert(WriteList(target, stored));

    std::vector<std::wstring> lines;
    Calls calls;
    assert(warmup::ChooseList(target, (root / L"no-base.txt").wstring(), AllList(calls, {}), lines) == warmup::Learned);
    for (const auto& line : lines) assert(warmup::OpenAhead(FromStored(line, game.wstring())));
    assert(FromStored(lines[0], game.wstring()) == opens[0]);
}

}  // namespace

int main() {
    const fs::path root = fs::temp_directory_path() / ("sb-warmup-test-" + std::to_string(GetCurrentProcessId()));
    fs::create_directories(root);
    TestChooseList(root);
    TestLearning();
    TestOpenAhead(root);
    TestLearnThenWarm(root);
    fs::remove_all(root);
    std::printf("open_warmup ok\n");
    return 0;
}
