#pragma once

// Access profiles: lists of files the game opens during start-up, in first-use
// order (ported from the lab's startup_open_warmup.inc list helpers).
//
// Format: UTF-8, one path per line. Paths inside the game folder are stored as
// "@\relative"; everything else (Workshop mods, the user folder) is absolute,
// so each entry's source is visible from the path itself (game, mod id from
// "...\workshop\content\236850\<id>\...", user folder).

#include <cstdint>
#include <string>
#include <vector>

namespace sb::profile {

std::wstring Lower(std::wstring text);
std::wstring Normalize(std::wstring path);
std::wstring ToStored(const std::wstring& path, const std::wstring& gameDir);
std::wstring FromStored(const std::wstring& line, const std::wstring& gameDir);
std::uint64_t Fnv1a(const std::string& bytes);
std::wstring Widen(const std::string& text);
std::string Narrow(const std::wstring& text);
bool ReadWhole(const std::wstring& path, std::string& out, std::uint64_t limit = 64ULL << 20);
std::vector<std::wstring> ParseList(const std::string& text);
std::vector<std::string> ParseEnabledMods(const std::string& json);
std::string DescriptorValue(const std::string& text, const std::string& key);
bool IsAbsolute(const std::wstring& path);
int Rank(const std::wstring& path);
bool AllListWanted(const std::wstring& relativeLower);

// "all" list: game folder (unless skipGame) + enabled mods' folders/archives,
// definitions everywhere, media only outside folders loaded during play;
// ordered archives/descriptors, definitions, other files.
std::vector<std::wstring> BuildAllList(const std::wstring& gameDir, const std::wstring& userDir,
                                       const std::vector<std::string>& enabledMods, bool skipGame);

// Profile key for a mod set: "<build>-<16 hex digits of FNV-1a(dlc_load.json)>".
std::wstring ProfileKey(const char* buildId, const std::string& dlcLoadJson);

// Folders of the enabled mods (descriptor "path", or the folder holding "archive").
std::vector<std::wstring> ModFolders(const std::wstring& userDir, const std::vector<std::string>& enabledMods);

// Unique stored paths in first-use order, then previous entries not seen this time (max 60000).
std::vector<std::wstring> UniqueStored(const std::vector<std::wstring>& paths, const std::wstring& gameDir,
                                       const std::vector<std::wstring>& previous = {});

// Writes via a temporary file and an atomic replace.
bool WriteList(const std::wstring& target, const std::vector<std::wstring>& lines);

// The name of the game's folder in Documents\Paradox Interactive for the game
// in `gameDir`: "Europa Universalis IV", or "Europa Universalis IV GOG" in a GOG
// install, whose eu4.exe adds " GOG" (GOG leaves goggame-2057001589.info there).
std::wstring UserFolderName(const std::wstring& gameDir);

// That folder in Documents for the running game (empty when unknown).
std::wstring UserDirectory();

}  // namespace sb::profile
