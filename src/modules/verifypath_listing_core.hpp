#pragma once
// verifypath_listing: the listings and the decision, apart from the hook so that tests can run
// them on real folders (tests/test_verifypath_listing.cpp). See verifypath_listing.cpp for the
// PhysFS side.
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>

namespace sb::verifypath {

enum class Kind : unsigned char { File, Directory, Unknown };

struct Listing {
    bool usable = false;
    bool missing = false;  // the directory itself does not exist
    std::unordered_map<std::string, Kind> names;  // upper-cased ASCII names
    std::uint64_t readAtMs = 0;  // GetTickCount64 when it was read
};

// verifypath_session = "on": after the game is ready a listing is read again once it is this old, so a file
// added to or removed from a game or mod folder during a session counts after at most this long.
inline constexpr std::uint64_t kSessionListingMs = 30000;

inline bool Expired(const Listing& listing, std::uint64_t nowMs) noexcept {
    return nowMs - listing.readAtMs >= kSessionListingMs;
}

inline std::shared_ptr<const Listing> ReadListing(const std::wstring& directory) {
    auto listing = std::make_shared<Listing>();
    listing->readAtMs = GetTickCount64();
    const std::wstring pattern = directory + L"\\*";
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr,
                                   FIND_FIRST_EX_LARGE_FETCH);
    if (find == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_PATH_NOT_FOUND) {
            listing->usable = true;
            listing->missing = true;
        }
        return listing;
    }
    bool complete = false;
    for (;;) {
        const std::size_t length = wcsnlen(data.cFileName, MAX_PATH);
        const bool dots = (length == 1 && data.cFileName[0] == L'.') ||
                          (length == 2 && data.cFileName[0] == L'.' && data.cFileName[1] == L'.');
        std::string key;
        bool ascii = true;
        for (std::size_t index = 0; index < length && ascii; ++index) {
            const wchar_t value = data.cFileName[index];
            ascii = value > 0 && value < 0x7f;
            key.push_back(static_cast<char>(value >= L'a' && value <= L'z' ? value - (L'a' - L'A') : value));
        }
        if (!dots && ascii) {
            Kind kind = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? Kind::Directory : Kind::File;
            // The platform stat reports a symlink only for IO_REPARSE_TAG_SYMLINK;
            // a junction is a directory. Other reparse points: let the original decide.
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && data.dwReserved0 != IO_REPARSE_TAG_MOUNT_POINT) {
                kind = Kind::Unknown;
            }
            listing->names.emplace(std::move(key), kind);
        }
        if (!FindNextFileW(find, &data)) {
            complete = GetLastError() == ERROR_NO_MORE_FILES;
            break;
        }
    }
    FindClose(find);
    listing->usable = complete;
    return listing;
}

enum class Answer { Original, Exists, NotFound };

// True when `root` is PhysFS's write directory (or inside it): the game creates,
// renames and deletes files there while it starts (logs, settings), so a listing
// read earlier can be stale (seen in the plugin's verify run: logs/error.log
// renamed during start-up). Such roots always go to the original stat.
inline bool IsWriteRoot(const char* root, const char* write) {
    if (write == nullptr || *write == '\0') return false;
    std::size_t wl = std::strlen(write);
    while (wl > 0 && (write[wl - 1] == '\\' || write[wl - 1] == '/')) --wl;
    std::size_t rl = std::strlen(root);
    while (rl > 0 && (root[rl - 1] == '\\' || root[rl - 1] == '/')) --rl;
    if (rl < wl || _strnicmp(root, write, wl) != 0) return false;
    return rl == wl || root[wl] == '\\' || root[wl] == '/';
}

// Mirrors the directory archiver: native path = opaque + name, '/' -> '\'.
// `root` and `name` are not null and `name` is not empty; `get_listing(directory)` returns
// the listing of a directory (shared_ptr<const Listing>).
template <class GetListing>
Answer DecideInRoot(const char* root, const char* name, Kind& kind, GetListing&& get_listing) {
    std::wstring directory;
    const int rootLength = static_cast<int>(std::strlen(root));
    if (rootLength < 4) return Answer::Original;
    const int wide = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, root, rootLength, nullptr, 0);
    if (wide <= 0) return Answer::Original;
    directory.assign(static_cast<std::size_t>(wide), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, root, rootLength, directory.data(), wide);
    if (directory.back() != L'\\') return Answer::Original;  // the archiver keeps a trailing separator
    directory.pop_back();
    std::string component;
    for (const char* cursor = name;; ++cursor) {
        const unsigned char value = static_cast<unsigned char>(*cursor);
        if (value != '/' && value != '\0') {
            if (value < 0x20 || value > 0x7e || value == '\\' || value == ':' || value == '*' || value == '?' ||
                value == '"' || value == '<' || value == '>' || value == '|' || value == '~') {
                return Answer::Original;
            }
            component.push_back(static_cast<char>(value >= 'a' && value <= 'z' ? value - ('a' - 'A') : value));
            continue;
        }
        if (component.empty() || component == "." || component == ".." || component.back() == '.' ||
            component.back() == ' ') {
            return Answer::Original;
        }
        const std::shared_ptr<const Listing> listing = get_listing(directory);
        if (!listing || !listing->usable) return Answer::Original;
        if (listing->missing) return Answer::NotFound;
        const auto entry = listing->names.find(component);
        if (entry == listing->names.end()) return Answer::NotFound;
        if (entry->second == Kind::Unknown) return Answer::Original;
        if (value == '\0') {
            kind = entry->second;
            return Answer::Exists;
        }
        if (entry->second != Kind::Directory) return Answer::Original;  // "file/child": exact error left to the original
        directory.push_back(L'\\');
        directory.append(component.begin(), component.end());
        component.clear();
    }
}

}  // namespace sb::verifypath
