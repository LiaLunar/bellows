// ram_prefetch without the game: the profile lines, the memory budget with waiting workers, the
// states of a member when the game opens it before, during or after its read, the entry check at
// open, reads served from a buffer against the game's bytes, the profile merge, and DEFLATE members
// read through the ZIP reader with zlib's raw inflate standing in for the game's tinfl.
#include "modules/ram_prefetch_core.hpp"

#include <zlib.h>

#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace prefetch = sb::prefetch;
using namespace std::chrono_literals;

void TestSplitLine() {
    std::wstring archive, member;
    assert(prefetch::SplitLine(L"@\\dlc\\dlc001.zip\tgfx/a.dds", archive, member));
    assert(archive == L"@\\dlc\\dlc001.zip" && member == L"gfx/a.dds");
    assert(prefetch::SplitLine(L"C:\\mods\\m.zip\tsub\tname", archive, member) && member == L"sub\tname");
    for (const wchar_t* bad : {L"", L"no tab", L"\tmember", L"archive\t"}) {
        assert(!prefetch::SplitLine(bad, archive, member));
    }
}

void TestBudget() {
    prefetch::Budget b;
    assert(!prefetch::Reserve(b, 101, 100));  // larger than the whole budget: never waits
    assert(prefetch::Reserve(b, 60, 100) && b.committed == 60);

    // A worker that needs 50 more waits until the game takes 60.
    std::atomic<int> state{0};
    std::thread worker([&] {
        state = prefetch::Reserve(b, 50, 100) ? 1 : 2;
    });
    std::this_thread::sleep_for(50ms);
    assert(state == 0);
    prefetch::Release(b, 60);
    worker.join();
    assert(state == 1 && b.committed == 50 && b.committedPeak == 60);
    assert(prefetch::Reserve(b, 40, 100) && b.committedPeak == 90);

    // Stopping wakes a waiting worker, which gets nothing.
    state = 0;
    std::thread waiting([&] {
        state = prefetch::Reserve(b, 20, 100) ? 1 : 2;
    });
    std::this_thread::sleep_for(50ms);
    assert(state == 0);
    assert(prefetch::Stop(b) && !prefetch::Stop(b));
    waiting.join();
    assert(state == 2 && b.committed == 90);
    assert(!prefetch::Reserve(b, 1, 100));  // nothing more once stopping

    // Stopping while a worker is about to wait: it always wakes (a lost wake-up would hang here).
    for (int round = 0; round < 2000; ++round) {
        prefetch::Budget full;
        assert(prefetch::Reserve(full, 100, 100));
        std::thread late([&] { assert(!prefetch::Reserve(full, 1, 100)); });
        if (round % 2) std::this_thread::yield();
        prefetch::Stop(full);
        late.join();
    }
}

void TestStatus() {
    std::atomic<int> s{prefetch::Pending};

    // Read before the game opens it: the game takes the buffer once.
    assert(prefetch::Claim(s) && s == prefetch::Running);
    assert(!prefetch::Claim(s));
    assert(prefetch::Publish(s, true, false) && s == prefetch::Ready);
    assert(prefetch::Take(s) && s == prefetch::Taken);
    assert(!prefetch::Take(s) && s == prefetch::Taken);  // a second open reads from the archive

    // The game opens it while it is queued: it leaves the queue.
    s = prefetch::Pending;
    assert(!prefetch::Take(s) && s == prefetch::Abandoned);
    assert(!prefetch::Claim(s) && s == prefetch::Abandoned);

    // The game opens it while a worker reads it: the result is discarded.
    s = prefetch::Pending;
    assert(prefetch::Claim(s));
    assert(!prefetch::Take(s) && s == prefetch::Abandoned);
    assert(!prefetch::Publish(s, true, false) && s == prefetch::Abandoned);
    assert(!prefetch::Take(s));

    // A failed read, or one that ends after the game is ready, is not published.
    for (const bool ok : {false, true}) {
        s = prefetch::Pending;
        assert(prefetch::Claim(s));
        const bool stopping = ok;
        assert(!prefetch::Publish(s, ok, stopping) && s == prefetch::Failed);
        assert(!prefetch::Take(s) && s == prefetch::Failed);
    }
}

void TestSameEntry() {
    sb::zip::Entry e;
    e.method = 8;
    e.crc32 = 0x12345678;
    e.compressedSize = 100;
    e.uncompressedSize = 300;
    assert(prefetch::SameEntry(e, 8, 0x12345678, 100, 300));
    assert(!prefetch::SameEntry(e, 0, 0x12345678, 100, 300));
    assert(!prefetch::SameEntry(e, 8, 0x12345679, 100, 300));
    assert(!prefetch::SameEntry(e, 8, 0x12345678, 101, 300));
    assert(!prefetch::SameEntry(e, 8, 0x12345678, 100, 299));
}

// The game reads a member in pieces: served from the buffer, the same bytes, then 0 at the end.
void TestReads() {
    std::vector<std::uint8_t> member(1000);
    std::mt19937 random(7);
    for (auto& byte : member) byte = static_cast<std::uint8_t>(random());
    for (const std::uint64_t piece : {1u, 7u, 333u, 1000u, 4096u}) {
        std::vector<std::uint8_t> served;
        std::uint64_t position = 0;
        for (;;) {
            std::uint8_t destination[4096];
            const std::uint64_t count = prefetch::ReadCount(member.size(), position, piece);
            if (count == 0) break;
            std::memcpy(destination, member.data() + position, static_cast<std::size_t>(count));
            assert(prefetch::SameRead(static_cast<std::int64_t>(count), destination, member.data(), position, count));
            served.insert(served.end(), destination, destination + count);
            position += count;
        }
        assert(served == member && position == member.size());
    }
    assert(prefetch::ReadCount(1000, 1000, 10) == 0 && prefetch::ReadCount(1000, 990, 0) == 0);

    // "verify": a different count or a different byte from the game is a mismatch.
    std::vector<std::uint8_t> game(member.begin() + 100, member.begin() + 150);
    assert(prefetch::SameRead(50, game.data(), member.data(), 100, 50));
    assert(!prefetch::SameRead(49, game.data(), member.data(), 100, 50));
    assert(!prefetch::SameRead(-1, game.data(), member.data(), 100, 50));
    game[49] ^= 1;
    assert(!prefetch::SameRead(50, game.data(), member.data(), 100, 50));
    assert(prefetch::SameRead(0, nullptr, member.data(), 1000, 0));
}

void TestMergeProfile() {
    const std::vector<std::wstring> learned = {L"a\tx", L"a\ty", L"b\tz"};
    const std::vector<std::wstring> previous = {L"a\ty", L"c\told", L"b\tz", L"c\told2"};
    const auto none = [](const std::wstring&) { return false; };
    assert(prefetch::MergeProfile(learned, previous, 100, none) ==
           std::vector<std::wstring>({L"a\tx", L"a\ty", L"b\tz", L"c\told", L"c\told2"}));
    assert(prefetch::MergeProfile(learned, previous, 4, none).size() == 4);
    assert(prefetch::MergeProfile(learned, previous, 2, none) == learned);  // this start's members are all kept
    assert(prefetch::MergeProfile({}, {L"a\tx", L"a\tx", L"b\tz"}, 100, none).size() == 2);  // duplicates once

    // A previous member that is gone leaves; a member this start read stays without being asked about.
    const auto gone = [](const std::wstring& line) { return line == L"c\told" || line == L"a\ty"; };
    assert(prefetch::MergeProfile(learned, previous, 100, gone) ==
           std::vector<std::wstring>({L"a\tx", L"a\ty", L"b\tz", L"c\told2"}));
}

void TestMemberGone() {
    assert(prefetch::MemberGone(true, false, false));    // the archive is gone
    assert(prefetch::MemberGone(false, true, false));    // the archive no longer lists it
    assert(!prefetch::MemberGone(false, true, true));    // still there
    assert(!prefetch::MemberGone(false, false, false));  // an archive that could not be read says nothing
}

// ---- DEFLATE members through the ZIP reader -----------------------------------------------------

void Put16(std::vector<std::uint8_t>& b, unsigned v) {
    b.push_back(static_cast<std::uint8_t>(v));
    b.push_back(static_cast<std::uint8_t>(v >> 8));
}
void Put32(std::vector<std::uint8_t>& b, std::uint32_t v) {
    Put16(b, v & 0xFFFF);
    Put16(b, v >> 16);
}

std::string Deflate(const std::string& data) {
    z_stream s{};
    assert(deflateInit2(&s, 6, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) == Z_OK);
    std::string out(deflateBound(&s, static_cast<uLong>(data.size())), '\0');
    s.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
    s.avail_in = static_cast<uInt>(data.size());
    s.next_out = reinterpret_cast<Bytef*>(out.data());
    s.avail_out = static_cast<uInt>(out.size());
    assert(deflate(&s, Z_FINISH) == Z_STREAM_END);
    out.resize(s.total_out);
    deflateEnd(&s);
    return out;
}

// As the game's tinfl with a non-wrapping output buffer: the whole stream into the whole output.
bool InflateWhole(const std::uint8_t* input, std::size_t inputSize, std::uint8_t* output, std::size_t outputSize) {
    z_stream s{};
    if (inflateInit2(&s, -15) != Z_OK) return false;
    s.next_in = const_cast<Bytef*>(input);
    s.avail_in = static_cast<uInt>(inputSize);
    s.next_out = output;
    s.avail_out = static_cast<uInt>(outputSize);
    const int status = inflate(&s, Z_FINISH);
    const bool ok = status == Z_STREAM_END && s.avail_in == 0 && s.avail_out == 0;
    inflateEnd(&s);
    return ok;
}

struct Stored {
    std::string name;
    std::string data;        // uncompressed
    std::string payload;     // as written
    std::uint32_t crc = 0;
};

std::vector<std::uint8_t> MakeDeflateZip(const std::vector<Stored>& members) {
    std::vector<std::uint8_t> zip, central;
    for (const auto& m : members) {
        const auto offset = static_cast<std::uint32_t>(zip.size());
        for (auto* out : {&zip, &central}) {
            const bool local = out == &zip;
            Put32(*out, local ? 0x04034B50 : 0x02014B50);
            if (!local) Put16(*out, 20);
            Put16(*out, 20); Put16(*out, 0); Put16(*out, 8); Put16(*out, 0); Put16(*out, 0);
            Put32(*out, m.crc); Put32(*out, static_cast<std::uint32_t>(m.payload.size()));
            Put32(*out, static_cast<std::uint32_t>(m.data.size()));
            Put16(*out, static_cast<unsigned>(m.name.size())); Put16(*out, 0);
            if (!local) {
                Put16(*out, 0); Put16(*out, 0); Put16(*out, 0); Put32(*out, 0); Put32(*out, offset);
            }
            out->insert(out->end(), m.name.begin(), m.name.end());
        }
        zip.insert(zip.end(), m.payload.begin(), m.payload.end());
    }
    const auto centralOffset = static_cast<std::uint32_t>(zip.size());
    zip.insert(zip.end(), central.begin(), central.end());
    Put32(zip, 0x06054B50); Put16(zip, 0); Put16(zip, 0);
    Put16(zip, static_cast<unsigned>(members.size())); Put16(zip, static_cast<unsigned>(members.size()));
    Put32(zip, static_cast<std::uint32_t>(central.size())); Put32(zip, centralOffset); Put16(zip, 0);
    return zip;
}

void TestDeflateMembers() {
    std::string text;
    for (int line = 0; line < 4000; ++line) text += "country_event = { id = flavor." + std::to_string(line) + " }\n";
    std::string noise(70000, '\0');
    std::mt19937 random(11);
    for (auto& c : noise) c = static_cast<char>(random());
    auto crc = [](const std::string& d) { return sb::zip::Crc32(reinterpret_cast<const std::uint8_t*>(d.data()), d.size()); };
    std::string truncated = Deflate(text);
    truncated.resize(truncated.size() / 2);
    const std::vector<Stored> members = {
        {"events/flavor.txt", text, Deflate(text), crc(text)},
        {"gfx/noise.dds", noise, Deflate(noise), crc(noise)},
        {"bad/crc.txt", text, Deflate(text), crc(text) ^ 1},
        {"bad/truncated.txt", text, truncated, crc(text)},
        {"empty.txt", "", Deflate(""), 0},
    };
    const fs::path dir = fs::temp_directory_path() / ("sb-prefetch-test-" + std::to_string(GetCurrentProcessId()));
    fs::create_directories(dir);
    const fs::path path = dir / "dlc.zip";
    {
        const auto bytes = MakeDeflateZip(members);
        FILE* f = _wfopen(path.c_str(), L"wb");
        std::fwrite(bytes.data(), 1, bytes.size(), f);
        std::fclose(f);
    }
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    assert(file != INVALID_HANDLE_VALUE);
    std::unordered_map<std::string, sb::zip::Entry> entries;
    assert(sb::zip::ParseCentralDirectory(file, {}, entries) && entries.size() == members.size());

    auto read = [&](const std::string& name, std::string& out) {
        const auto& e = entries.at(name);
        out.assign(static_cast<std::size_t>(e.uncompressedSize) + 1, '\0');  // + 1 as the module allocates
        const bool ok = sb::zip::ReadMember(file, name, e, &InflateWhole, reinterpret_cast<std::uint8_t*>(out.data()));
        out.resize(static_cast<std::size_t>(e.uncompressedSize));
        return ok;
    };
    std::string out;
    assert(read("events/flavor.txt", out) && out == text);
    assert(entries.at("events/flavor.txt").compressedSize < text.size());
    assert(prefetch::SameEntry(entries.at("events/flavor.txt"), 8, crc(text), members[0].payload.size(), text.size()));
    assert(read("gfx/noise.dds", out) && out == noise);
    assert(!read("bad/crc.txt", out));        // decoded, but not the bytes the archive names
    assert(!read("bad/truncated.txt", out));  // the stream ends early
    assert(read("empty.txt", out) && out.empty());

    // Served in pieces from the prefetched buffer: the bytes the game would have decoded.
    assert(read("events/flavor.txt", out));
    std::string served;
    for (std::uint64_t position = 0, count; (count = prefetch::ReadCount(out.size(), position, 4093)) != 0; position += count) {
        served.append(out, static_cast<std::size_t>(position), static_cast<std::size_t>(count));
    }
    assert(served == text);
    CloseHandle(file);
    fs::remove_all(dir);
}

}  // namespace

static void TestEffectiveBudget() {
    constexpr std::uint64_t GiB = 1ULL << 30;
    assert(sb::prefetch::EffectiveBudget(GiB, 32 * GiB) == GiB);
    assert(sb::prefetch::EffectiveBudget(GiB, 4 * GiB) == GiB);
    assert(sb::prefetch::EffectiveBudget(GiB, 2 * GiB) == GiB / 2);
    assert(sb::prefetch::EffectiveBudget(GiB, 0) == GiB);
}

int main() {
    TestEffectiveBudget();
    TestSplitLine();
    TestBudget();
    TestStatus();
    TestSameEntry();
    TestReads();
    TestMergeProfile();
    TestMemberGone();
    TestDeflateMembers();
    std::printf("ram_prefetch ok\n");
    return 0;
}
