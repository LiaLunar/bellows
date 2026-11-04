#pragma once

// transfer_skip: what the plugin knows about the game's save transfer, without the game (lab docs:
// docs/autosave-cost.md, "Wczytanie przez „Continue”").
//
// Starting a game from a save (CGameSetup::Load, 0x1132CA0; "Continue" goes this way) hands the whole file to
// CFileTransferManager::AddFile (0xD7AAD0), on a session that is a server or a dummy. For a file that is not a
// ZIP already, AddFile first deflates it at level 9 into a second buffer, SFileDesc::CompressedData. That buffer
// is read in one place, CFileTransferManager::SendFile (0xD7B2A0), which sends it to a client instead of Data
// when it exists. The game's own load gets neither from the wire: the start command (0x279310) passes Data and
// the flag 0 to CGameSetup::LoadFile (0x110D760), with or without the copy. A compressed save has no copy either,
// so every path after AddFile already runs without one.
//
// A single-player session is the dummy session (CSession::_bIsDummy): nobody can receive the copy, so it can go.
// A server (hosted multiplayer) keeps it, as does anything this file cannot tell.

#include <zlib.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <vector>

namespace sb::transfer {

enum class Action {
    Pass,   // the game's call as it came
    Skip,   // the same call with compress = false
    Check,  // the game's call as it came, then compare what it built ("verify")
};

// compress: AddFile's argument. server, dummy: CSession::_bIsServer and _bIsDummy. verify: the module's mode.
inline Action Decide(bool compress, bool haveSession, bool server, bool dummy, bool verify) {
    if (!compress || !haveSession || server || !dummy) return Action::Pass;
    return verify ? Action::Check : Action::Skip;
}

// The game's types, as far as the module reads them (CFileTransferManager in the GOG build's eu4.pdb; the Steam
// build's code uses the same offsets, plugin/symbols.toml).
struct Blob {  // CBlob: a std::vector of bytes
    std::uint8_t* begin;
    std::uint8_t* end;
    std::uint8_t* capacity;
    std::size_t size() const { return static_cast<std::size_t>(end - begin); }
};
static_assert(sizeof(Blob) == 0x18, "CBlob layout");

struct FileDesc {  // CFileTransferManager::SFileDesc
    std::uint8_t fileName[0x20];
    Blob* data;
    void* dataControl;
    Blob* compressedData;
    void* compressedControl;
    std::uint32_t checkSum;
};
static_assert(sizeof(FileDesc) == 0x48, "SFileDesc layout");
static_assert(offsetof(FileDesc, data) == 0x20 && offsetof(FileDesc, compressedData) == 0x30, "SFileDesc layout");

inline constexpr std::size_t kManagerHandler = 0x20;  // CFileTransferManager::_Parent
inline constexpr std::size_t kManagerFiles = 0x28;    // CFileTransferManager::_Files (begin, end, capacity)
inline constexpr std::size_t kHandlerGetSession = 1;  // the slot CFileTransferManager::GetSession calls

// CFileTransferManager::GetSession: the handler's slot 1. Null without a handler.
inline void* ManagerSession(void* manager) {
    void* handler = *reinterpret_cast<void**>(static_cast<std::uint8_t*>(manager) + kManagerHandler);
    if (handler == nullptr) return nullptr;
    void** vtable = *static_cast<void***>(handler);
    return reinterpret_cast<void* (*)(void*)>(vtable[kHandlerGetSession])(handler);
}

// The entry AddFile returned the index of; null when the index is outside _Files.
inline const FileDesc* ManagerFile(void* manager, int index) {
    auto* const* files = reinterpret_cast<FileDesc* const*>(static_cast<std::uint8_t*>(manager) + kManagerFiles);
    if (index < 0 || index >= files[1] - files[0]) return nullptr;
    return files[0] + index;
}

// The one member of the archive AddFile builds, from its central directory: AddFile patches the sizes in, and the
// central directory is what the game's reader goes by.
struct ZipMember {
    const std::uint8_t* compressed = nullptr;
    std::size_t compressedSize = 0;
    std::size_t size = 0;
    std::uint32_t crc = 0;
    std::uint16_t method = 0;
};

inline std::uint32_t Read32(const std::uint8_t* p) {
    std::uint32_t value;
    std::memcpy(&value, p, sizeof(value));
    return value;
}
inline std::uint16_t Read16(const std::uint8_t* p) {
    std::uint16_t value;
    std::memcpy(&value, p, sizeof(value));
    return value;
}

// False unless the archive has exactly one member and every offset stays inside it.
inline bool FindOnlyMember(const std::uint8_t* zip, std::size_t size, ZipMember& member) {
    constexpr std::size_t kEnd = 22, kCentral = 46, kLocal = 30;
    if (zip == nullptr || size < kEnd + kCentral + kLocal) return false;
    std::size_t end = size - kEnd;
    for (;; --end) {
        if (Read32(zip + end) == 0x06054B50) break;
        if (end == 0 || size - end > kEnd + 0xFFFF) return false;
    }
    if (Read16(zip + end + 10) != 1) return false;
    const std::size_t central = Read32(zip + end + 16);
    if (central > end || end - central < kCentral || Read32(zip + central) != 0x02014B50) return false;
    member.method = Read16(zip + central + 10);
    member.crc = Read32(zip + central + 16);
    member.compressedSize = Read32(zip + central + 20);
    member.size = Read32(zip + central + 24);
    const std::size_t local = Read32(zip + central + 42);
    if (local > central || central - local < kLocal || Read32(zip + local) != 0x04034B50) return false;
    const std::size_t start = local + kLocal + Read16(zip + local + 26) + Read16(zip + local + 28);
    if (start > central || central - start < member.compressedSize) return false;
    member.compressed = zip + start;
    return true;
}

// True when the raw DEFLATE stream decodes to exactly data, and data's CRC-32 is the member's.
inline bool MemberIsData(const ZipMember& member, const std::uint8_t* data, std::size_t size) {
    if (member.method != 8 || member.size != size || size > 0x7FFFFFFFu || member.compressedSize > 0x7FFFFFFFu) return false;
    const std::unique_ptr<std::uint8_t[]> out(new (std::nothrow) std::uint8_t[size + 1]);
    if (out == nullptr) return false;
    z_stream s{};
    if (inflateInit2(&s, -15) != Z_OK) return false;
    s.next_in = const_cast<Bytef*>(member.compressed);
    s.avail_in = static_cast<uInt>(member.compressedSize);
    s.next_out = out.get();
    s.avail_out = static_cast<uInt>(size + 1);
    const int status = inflate(&s, Z_FINISH);
    const bool ended = status == Z_STREAM_END && s.avail_in == 0 && s.total_out == size;
    inflateEnd(&s);
    if (!ended || std::memcmp(out.get(), data, size) != 0) return false;
    return static_cast<std::uint32_t>(crc32(0, data, static_cast<uInt>(size))) == member.crc;
}

}  // namespace sb::transfer
