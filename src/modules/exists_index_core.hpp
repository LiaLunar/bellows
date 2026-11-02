#pragma once
// exists_index: the virtual path check, the listings and the lookup, apart from the vtable slot
// so that tests can run them on real folders (tests/test_exists_index.cpp). See
// exists_index.cpp for the game side.
#include <windows.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace sb::exists {

enum class Kind : unsigned char { File, Directory, Unknown };

struct Listing {
    bool usable = false;
    std::unordered_map<std::string, Kind> names;  // upper-cased ASCII name -> kind
};

inline // Accept only plain relative PhysFS paths "a/b/c.ext" of printable ASCII.
bool SplitVirtualPath(const char* text, std::size_t length, std::vector<std::string>& parts) {
    if (length == 0 || text[0] == '/' || text[length - 1] == '/') return false;
    std::string current;
    for (std::size_t index = 0; index < length; ++index) {
        const unsigned char value = static_cast<unsigned char>(text[index]);
        if (value < 0x20 || value > 0x7e || value == '\\' || value == ':' || value == '*' || value == '?' ||
            value == '"' || value == '<' || value == '>' || value == '|' || value == '~') {
            return false;
        }
        if (value == '/') {
            if (current.empty() || current == "." || current == "..") return false;
            parts.push_back(std::move(current));
            current.clear();
            continue;
        }
        current.push_back(static_cast<char>(value >= 'a' && value <= 'z' ? value - ('a' - 'A') : value));
    }
    if (current.empty() || current == "." || current == ".." || current.back() == '.' || current.back() == ' ') {
        return false;
    }
    parts.push_back(std::move(current));
    return true;
}

inline std::shared_ptr<const Listing> ReadListing(const std::wstring& directory, bool symlinksPermitted) {
    auto listing = std::make_shared<Listing>();
    const std::wstring pattern = directory + L"\\*";
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr,
                                   FIND_FIRST_EX_LARGE_FETCH);
    if (find == INVALID_HANDLE_VALUE) {
        // A missing directory is a usable, empty listing: nothing is answered from it.
        listing->usable = GetLastError() == ERROR_PATH_NOT_FOUND || GetLastError() == ERROR_FILE_NOT_FOUND;
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
            if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                // Junctions behave like directories for PhysFS; symbolic links only
                // when the game permits them; anything else is unknown.
                if (data.dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT) {
                } else if (data.dwReserved0 == IO_REPARSE_TAG_SYMLINK && symlinksPermitted) {
                } else {
                    kind = Kind::Unknown;
                }
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

// True when every part of `parts` is in the listing of the folder above it, and every part but
// the last is a directory; `get_listing(depth)` returns the listing of the folder that holds
// parts[depth] (shared_ptr<const Listing>).
template <class GetListing>
bool Found(const std::vector<std::string>& parts, GetListing&& get_listing) {
    for (std::size_t depth = 0; depth < parts.size(); ++depth) {
        const std::shared_ptr<const Listing> listing = get_listing(depth);
        if (!listing || !listing->usable) return false;
        const auto entry = listing->names.find(parts[depth]);
        if (entry == listing->names.end() || entry->second == Kind::Unknown) return false;
        if (depth + 1 < parts.size() && entry->second != Kind::Directory) return false;
    }
    return true;
}

}  // namespace sb::exists
