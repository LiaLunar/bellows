// transfer_skip: the decision, the reads of the game's structures on stand-ins with the game's layout, and the
// check of a transfer copy. What the game does with an entry without a copy is a lab test, not this one.
#include "modules/transfer_skip_core.hpp"

#include <zlib.h>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;
using sb::transfer::Action;
using sb::transfer::Decide;

void Put16(Bytes& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
}
void Put32(Bytes& out, std::uint32_t value) {
    Put16(out, static_cast<std::uint16_t>(value));
    Put16(out, static_cast<std::uint16_t>(value >> 16));
}

Bytes Deflate(const Bytes& data) {
    z_stream s{};
    assert(deflateInit2(&s, 9, Z_DEFLATED, -15, 9, Z_DEFAULT_STRATEGY) == Z_OK);
    Bytes out(deflateBound(&s, static_cast<uLong>(data.size())) + 64);
    s.next_in = const_cast<Bytef*>(data.data());
    s.avail_in = static_cast<uInt>(data.size());
    s.next_out = out.data();
    s.avail_out = static_cast<uInt>(out.size());
    assert(deflate(&s, Z_FINISH) == Z_STREAM_END);
    out.resize(s.total_out);
    deflateEnd(&s);
    return out;
}

// An archive as AddFile builds it: one deflated member, local header, central directory, end record.
Bytes Zip(const Bytes& data, const std::string& name = "save", std::uint16_t members = 1) {
    const Bytes stream = Deflate(data);
    const auto crc = static_cast<std::uint32_t>(crc32(0, data.data(), static_cast<uInt>(data.size())));
    const auto compressed = static_cast<std::uint32_t>(stream.size());
    const auto size = static_cast<std::uint32_t>(data.size());
    Bytes zip;
    Put32(zip, 0x04034B50);
    Put16(zip, 20);
    Put16(zip, 0);
    Put16(zip, 8);
    Put32(zip, 0);
    Put32(zip, crc);
    Put32(zip, compressed);
    Put32(zip, size);
    Put16(zip, static_cast<std::uint16_t>(name.size()));
    Put16(zip, 0);
    zip.insert(zip.end(), name.begin(), name.end());
    zip.insert(zip.end(), stream.begin(), stream.end());
    const auto central = static_cast<std::uint32_t>(zip.size());
    Put32(zip, 0x02014B50);
    Put16(zip, 20);
    Put16(zip, 20);
    Put16(zip, 0);
    Put16(zip, 8);
    Put32(zip, 0);
    Put32(zip, crc);
    Put32(zip, compressed);
    Put32(zip, size);
    Put16(zip, static_cast<std::uint16_t>(name.size()));
    Put16(zip, 0);
    Put16(zip, 0);
    Put16(zip, 0);
    Put16(zip, 0);
    Put32(zip, 0);
    Put32(zip, 0);
    zip.insert(zip.end(), name.begin(), name.end());
    const auto centralSize = static_cast<std::uint32_t>(zip.size()) - central;
    Put32(zip, 0x06054B50);
    Put16(zip, 0);
    Put16(zip, 0);
    Put16(zip, members);
    Put16(zip, members);
    Put32(zip, centralSize);
    Put32(zip, central);
    Put16(zip, 0);
    return zip;
}

Bytes SaveText(std::size_t size) {
    std::string text;
    for (int i = 0; text.size() < size; ++i) text += "province_" + std::to_string(i % 977) + "={ owner=\"RUS\" tax=" + std::to_string(i % 13) + " }\n";
    text.resize(size);
    return {text.begin(), text.end()};
}

void TestDecide() {
    // Single player: the copy goes ("on") or stays to be checked ("verify").
    assert(Decide(true, true, false, true, false) == Action::Skip);
    assert(Decide(true, true, false, true, true) == Action::Check);
    // Hosted multiplayer keeps its copy in both modes.
    assert(Decide(true, true, true, false, false) == Action::Pass);
    assert(Decide(true, true, true, false, true) == Action::Pass);
    // A client, a session the plugin cannot read, a byte combination the game does not make.
    assert(Decide(true, true, false, false, false) == Action::Pass);
    assert(Decide(true, false, false, true, false) == Action::Pass);
    assert(Decide(true, true, true, true, false) == Action::Pass);
    // Nothing to compress: a compressed save, the random New World.
    assert(Decide(false, true, false, true, false) == Action::Pass);
    assert(Decide(false, true, false, true, true) == Action::Pass);
}

// CFileTransferManager's start and a handler with the game's vtable shape.
struct FakeManager {
    void* vtable = nullptr;
    std::uint8_t referenceObject[0x18] = {};
    void* handler = nullptr;
    sb::transfer::FileDesc* filesBegin = nullptr;
    sb::transfer::FileDesc* filesEnd = nullptr;
    sb::transfer::FileDesc* filesCapacity = nullptr;
};
static_assert(offsetof(FakeManager, handler) == sb::transfer::kManagerHandler);
static_assert(offsetof(FakeManager, filesBegin) == sb::transfer::kManagerFiles);

std::uint8_t g_session[0x100];
void* GetSession(void*) { return g_session; }
void* NotASlot(void*) {
    assert(false);
    return nullptr;
}
void* g_handlerVtable[] = {reinterpret_cast<void*>(&NotASlot), reinterpret_cast<void*>(&GetSession)};

void TestStructures() {
    void* handler[] = {g_handlerVtable};
    sb::transfer::FileDesc files[2] = {};
    FakeManager manager;
    manager.handler = handler;
    manager.filesBegin = files;
    manager.filesEnd = files + 2;
    manager.filesCapacity = files + 2;
    assert(sb::transfer::ManagerSession(&manager) == g_session);
    assert(sb::transfer::ManagerFile(&manager, 0) == &files[0]);
    assert(sb::transfer::ManagerFile(&manager, 1) == &files[1]);
    assert(sb::transfer::ManagerFile(&manager, 2) == nullptr);
    assert(sb::transfer::ManagerFile(&manager, -1) == nullptr);
    manager.handler = nullptr;
    assert(sb::transfer::ManagerSession(&manager) == nullptr);
}

void TestCopy() {
    const Bytes data = SaveText(3 << 20);
    const Bytes zip = Zip(data);
    sb::transfer::ZipMember member;
    assert(sb::transfer::FindOnlyMember(zip.data(), zip.size(), member));
    assert(member.method == 8 && member.size == data.size());
    assert(sb::transfer::MemberIsData(member, data.data(), data.size()));

    // Other bytes, in the middle and at the end; a shorter and a longer save; the same length, one bit off.
    Bytes other = data;
    other[other.size() / 2] ^= 1;
    assert(!sb::transfer::MemberIsData(member, other.data(), other.size()));
    other = data;
    other.back() ^= 0x80;
    assert(!sb::transfer::MemberIsData(member, other.data(), other.size()));
    assert(!sb::transfer::MemberIsData(member, data.data(), data.size() - 1));
    other = data;
    other.push_back(0);
    assert(!sb::transfer::MemberIsData(member, other.data(), other.size()));

    // A broken stream and a broken CRC.
    Bytes broken = zip;
    broken[40] ^= 0xFF;
    sb::transfer::ZipMember brokenMember;
    assert(sb::transfer::FindOnlyMember(broken.data(), broken.size(), brokenMember));
    assert(!sb::transfer::MemberIsData(brokenMember, data.data(), data.size()));
    sb::transfer::ZipMember wrongCrc = member;
    wrongCrc.crc ^= 1;
    assert(!sb::transfer::MemberIsData(wrongCrc, data.data(), data.size()));
    sb::transfer::ZipMember stored = member;
    stored.method = 0;
    assert(!sb::transfer::MemberIsData(stored, data.data(), data.size()));
}

void TestMalformedArchives() {
    const Bytes data = SaveText(4096);
    const Bytes zip = Zip(data);
    sb::transfer::ZipMember member;
    assert(!sb::transfer::FindOnlyMember(nullptr, 0, member));
    assert(!sb::transfer::FindOnlyMember(zip.data(), 10, member));
    // Every prefix of a valid archive, and two members, are refused without reading past the end.
    for (std::size_t size = 0; size + 1 < zip.size(); size += 97) assert(!sb::transfer::FindOnlyMember(zip.data(), size, member));
    const Bytes two = Zip(data, "save", 2);
    assert(!sb::transfer::FindOnlyMember(two.data(), two.size(), member));
    // An end record pointing outside the archive.
    Bytes bad = zip;
    const std::size_t end = bad.size() - 22;
    std::memset(bad.data() + end + 16, 0xFF, 4);
    assert(!sb::transfer::FindOnlyMember(bad.data(), bad.size(), member));
    // A member size larger than the room for it.
    bad = zip;
    const std::uint32_t central = sb::transfer::Read32(zip.data() + end + 16);
    std::memset(bad.data() + central + 20, 0x7F, 4);
    assert(!sb::transfer::FindOnlyMember(bad.data(), bad.size(), member));
}

}  // namespace

int main() {
    TestDecide();
    TestStructures();
    TestCopy();
    TestMalformedArchives();
    std::puts("transfer_skip: ok");
    return 0;
}
