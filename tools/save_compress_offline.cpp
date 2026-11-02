// Offline check of save_compress on compressed saves (ZIP), without the game. The save is only read.
//
// Built twice (CMake option SB_BUILD_SAVE_TOOLS):
//   save_compress_check_game  zlib 1.2.8, the game's version: for each DEFLATE member, inflate it, check its
//                             CRC-32 with CrcUpdate, and compress it again with CompressEntry (the game's
//                             0x153C790 written out) at level 3 with each work-buffer size given; the result
//                             must be the member's bytes in the file, byte for byte.
//   save_compress_check       Bellows' zlib: the same members through CompressParallel (members of at least
//                             kParallelMinChunks chunks; smaller ones are copied), checked with InflateMatches,
//                             and with --out the archive rebuilt with the new members (headers patched with
//                             the new CRC and sizes) for a check with a standard ZIP reader.
//
// usage: save_compress_check_game <save.eu4> [buffer bytes ...]   (default 50000000)
//        save_compress_check <save.eu4> [--threads N] [--chunk BYTES] [--out rebuilt.zip]
#include "modules/save_compress_core.hpp"
#ifndef SB_TOOL_GAME_ZLIB
#include "modules/save_compress_parallel.hpp"
#endif

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

std::uint16_t U16(const Bytes& b, std::size_t at) { return static_cast<std::uint16_t>(b[at] | b[at + 1] << 8); }
std::uint32_t U32(const Bytes& b, std::size_t at) {
    return static_cast<std::uint32_t>(b[at]) | static_cast<std::uint32_t>(b[at + 1]) << 8 |
           static_cast<std::uint32_t>(b[at + 2]) << 16 | static_cast<std::uint32_t>(b[at + 3]) << 24;
}
void Put32(Bytes& b, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + i] = static_cast<std::uint8_t>(v >> (8 * i));
}

struct Member {
    std::string name;
    std::size_t central = 0;  // offset of its central directory record
    std::size_t local = 0, data = 0;
    std::uint16_t method = 0, flags = 0;
    std::uint32_t crc = 0, compressed = 0, size = 0;
};

bool Parse(const Bytes& zip, std::vector<Member>& members, std::size_t& eocd) {
    if (zip.size() < 22) return false;
    eocd = zip.size() - 22;
    while (U32(zip, eocd) != 0x06054B50) {
        if (eocd == 0 || zip.size() - eocd > 22 + 65535) return false;
        --eocd;
    }
    const std::size_t count = U16(zip, eocd + 10);
    std::size_t at = U32(zip, eocd + 16);
    for (std::size_t i = 0; i < count; ++i) {
        if (at + 46 > zip.size() || U32(zip, at) != 0x02014B50) return false;
        Member m;
        m.central = at;
        m.flags = U16(zip, at + 8);
        m.method = U16(zip, at + 10);
        m.crc = U32(zip, at + 16);
        m.compressed = U32(zip, at + 20);
        m.size = U32(zip, at + 24);
        const std::size_t nameLength = U16(zip, at + 28), extra = U16(zip, at + 30), comment = U16(zip, at + 32);
        m.local = U32(zip, at + 42);
        m.name.assign(reinterpret_cast<const char*>(zip.data() + at + 46), nameLength);
        if (m.local + 30 > zip.size() || U32(zip, m.local) != 0x04034B50) return false;
        m.data = m.local + 30 + U16(zip, m.local + 26) + U16(zip, m.local + 28);
        if (m.data + m.compressed > zip.size()) return false;
        members.push_back(m);
        at += 46 + nameLength + extra + comment;
    }
    return true;
}

bool Inflate(const std::uint8_t* in, std::size_t inSize, Bytes& out) {
    z_stream s{};
    if (inflateInit2(&s, -15) != Z_OK) return false;
    s.next_in = const_cast<Bytef*>(in);
    s.avail_in = static_cast<uInt>(inSize);
    s.next_out = out.data();
    s.avail_out = static_cast<uInt>(out.size());
    const int result = inflate(&s, Z_FINISH);
    const bool ok = result == Z_STREAM_END && s.avail_out == 0 && s.avail_in == 0;
    inflateEnd(&s);
    return ok;
}

double Ms(std::chrono::steady_clock::time_point since) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - since).count();
}

#ifdef SB_TOOL_GAME_ZLIB
// The game's input stream and archive writer over memory.
struct MemoryIo {
    const Bytes* input;
    int position = 0;
    Bytes output;
};

int RecompressAndCompare(const Member& m, const Bytes& zip, const Bytes& data, const std::vector<std::uint64_t>& buffers) {
    int failures = 0;
    sb::savez::Deflater z;
    z.init2 = [](void* s, int level, int method, int windowBits, int memLevel, int strategy, const char* version,
                 int size) {
        return deflateInit2_(static_cast<z_stream*>(s), level, method, windowBits, memLevel, strategy, version, size);
    };
    z.deflate = [](void* s, int flush) { return deflate(static_cast<z_stream*>(s), flush); };
    z.end = [](void* s) { return deflateEnd(static_cast<z_stream*>(s)); };
    for (const std::uint64_t bufferSize : buffers) {
        MemoryIo memory{&data};
        sb::savez::EntryIo io;
        io.input = &memory;
        io.writer = &memory;
        io.size = [](void* p) { return static_cast<int>(static_cast<MemoryIo*>(p)->input->size()); };
        io.seek = [](void* p, int position) { static_cast<MemoryIo*>(p)->position = position; };
        io.read = [](void* p, std::uint8_t* destination, int length) {
            auto* m = static_cast<MemoryIo*>(p);
            std::memcpy(destination, m->input->data() + m->position, static_cast<std::size_t>(length));
            m->position += length;
        };
        io.write = [](void* p, const std::uint8_t* bytes, std::int64_t length) {
            auto* m = static_cast<MemoryIo*>(p);
            m->output.insert(m->output.end(), bytes, bytes + length);
        };
        Bytes buffer(static_cast<std::size_t>(bufferSize));
        std::uint32_t crc = 0xFFFFFFFFu;
        const auto start = std::chrono::steady_clock::now();
        const int written = sb::savez::CompressEntry(z, io, buffer.data(), bufferSize, &crc, 3);
        const double ms = Ms(start);
        const bool same = written == static_cast<int>(memory.output.size()) && memory.output.size() == m.compressed &&
                          std::memcmp(memory.output.data(), zip.data() + m.data, m.compressed) == 0;
        std::size_t firstDifference = 0;
        while (firstDifference < memory.output.size() && firstDifference < m.compressed &&
               memory.output[firstDifference] == zip[m.data + firstDifference])
            ++firstDifference;
        std::printf("  level 3, buffer %llu: %d bytes, crc %08X, %.1f ms: %s", static_cast<unsigned long long>(bufferSize),
                    written, crc, ms, same && crc == m.crc ? "SAME as the file" : "DIFFERENT");
        if (!same) std::printf(" (first difference at %zu)", firstDifference);
        std::printf("\n");
        if (!same || crc != m.crc) ++failures;
    }
    return failures;
}
#endif

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <save.eu4> [options]\n", argv[0]);
        return 2;
    }
    std::ifstream file(argv[1], std::ios::binary);
    const Bytes zip((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::vector<Member> members;
    std::size_t eocd = 0;
    if (!Parse(zip, members, eocd)) {
        std::fprintf(stderr, "%s: not a ZIP archive this tool reads\n", argv[1]);
        return 2;
    }
#ifdef SB_TOOL_GAME_ZLIB
    std::vector<std::uint64_t> buffers;
    for (int i = 2; i < argc; ++i) buffers.push_back(std::stoull(argv[i]));
    if (buffers.empty()) buffers.push_back(50000000);
    std::printf("%s: zlib %s\n", argv[1], zlibVersion());
#else
    unsigned threads = std::thread::hardware_concurrency();
    std::size_t chunk = sb::savez::kParallelChunk;
    const char* outPath = nullptr;
    for (int i = 2; i + 1 < argc; i += 2) {
        if (std::strcmp(argv[i], "--threads") == 0) threads = static_cast<unsigned>(std::stoul(argv[i + 1]));
        else if (std::strcmp(argv[i], "--chunk") == 0) chunk = static_cast<std::size_t>(std::stoull(argv[i + 1]));
        else if (std::strcmp(argv[i], "--out") == 0) outPath = argv[i + 1];
    }
    std::printf("%s: zlib %s, %u threads, chunks of %zu bytes\n", argv[1], zlibVersion(), threads, chunk);
    Bytes rebuilt, central;
#endif
    int failures = 0;
    for (const Member& m : members) {
        std::printf("%s: method %u, flags 0x%04X, %u -> %u bytes, crc %08X\n", m.name.c_str(), m.method, m.flags, m.size,
                    m.compressed, m.crc);
        Bytes data(m.size);
        if (m.method == 8) {
            const auto start = std::chrono::steady_clock::now();
            if (!Inflate(zip.data() + m.data, m.compressed, data)) {
                std::printf("  inflate FAILED\n");
                ++failures;
                continue;
            }
            const double inflateMs = Ms(start);
            const auto crcStart = std::chrono::steady_clock::now();
            const std::uint32_t crc = ~sb::savez::CrcUpdate(0xFFFFFFFFu, data.data(), data.size());
            const double crcMs = Ms(crcStart);
            const auto byteStart = std::chrono::steady_clock::now();
            const std::uint32_t byteCrc = ~sb::savez::CrcUpdateBytewise(0xFFFFFFFFu, data.data(), data.size());
            const double byteMs = Ms(byteStart);
            std::printf("  inflate %.1f ms; crc %08X (slice-by-8 %.2f ms, byte loop %08X %.2f ms): %s\n", inflateMs, crc,
                        crcMs, byteCrc, byteMs, crc == m.crc && byteCrc == m.crc ? "matches the header" : "MISMATCH");
            if (crc != m.crc || byteCrc != m.crc) ++failures;
        } else if (m.method == 0) {
            std::memcpy(data.data(), zip.data() + m.data, m.size);
        }
#ifdef SB_TOOL_GAME_ZLIB
        if (m.method == 8) failures += RecompressAndCompare(m, zip, data, buffers);
#else
        Bytes local(zip.begin() + static_cast<std::ptrdiff_t>(m.local), zip.begin() + static_cast<std::ptrdiff_t>(m.data));
        Bytes record(zip.begin() + static_cast<std::ptrdiff_t>(m.central),
                     zip.begin() + static_cast<std::ptrdiff_t>(m.central + 46 + U16(zip, m.central + 28) +
                                                               U16(zip, m.central + 30) + U16(zip, m.central + 32)));
        Put32(record, 42, static_cast<std::uint32_t>(rebuilt.size()));
        rebuilt.insert(rebuilt.end(), local.begin(), local.end());
        const std::size_t localAt = rebuilt.size() - local.size();
        if (m.method == 8 && data.size() >= sb::savez::kParallelMinChunks * chunk) {
            sb::savez::ParallelResult result;
            const auto start = std::chrono::steady_clock::now();
            const bool ok = sb::savez::CompressParallel(data.data(), data.size(), 3, chunk, threads, 0, result);
            const double ms = Ms(start);
            const auto checkStart = std::chrono::steady_clock::now();
            const bool valid = ok && sb::savez::InflateMatches(result, data.data(), data.size());
            const double checkMs = Ms(checkStart);
            std::printf("  parallel: %s, %zu chunks on %u threads, %llu bytes (%+.2f%% against the file), crc %08X %s, "
                        "%.1f ms; inflate check %s, %.1f ms\n",
                        ok ? "ok" : "FAILED", result.pieces.size(), result.threads,
                        static_cast<unsigned long long>(result.compressedSize),
                        100.0 * (static_cast<double>(result.compressedSize) / m.compressed - 1.0), result.crc,
                        result.crc == m.crc ? "(same)" : "(DIFFERENT)", ms, valid ? "ok" : "FAILED", checkMs);
            if (!ok || !valid || result.crc != m.crc) ++failures;
            for (const auto& piece : result.pieces) rebuilt.insert(rebuilt.end(), piece.bytes.get(), piece.bytes.get() + piece.size);
            Put32(rebuilt, localAt + 14, result.crc);
            Put32(rebuilt, localAt + 18, static_cast<std::uint32_t>(result.compressedSize));
            Put32(record, 16, result.crc);
            Put32(record, 20, static_cast<std::uint32_t>(result.compressedSize));
        } else {
            rebuilt.insert(rebuilt.end(), zip.begin() + static_cast<std::ptrdiff_t>(m.data),
                           zip.begin() + static_cast<std::ptrdiff_t>(m.data + m.compressed));
            std::printf("  copied (below %zu chunks)\n", sb::savez::kParallelMinChunks);
        }
        central.insert(central.end(), record.begin(), record.end());
#endif
    }
#ifndef SB_TOOL_GAME_ZLIB
    if (outPath != nullptr) {
        Bytes end(zip.begin() + static_cast<std::ptrdiff_t>(eocd), zip.end());
        Put32(end, 12, static_cast<std::uint32_t>(central.size()));
        Put32(end, 16, static_cast<std::uint32_t>(rebuilt.size()));
        rebuilt.insert(rebuilt.end(), central.begin(), central.end());
        rebuilt.insert(rebuilt.end(), end.begin(), end.end());
        std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(rebuilt.data()), static_cast<std::streamsize>(rebuilt.size()));
        std::printf("wrote %s (%zu bytes)\n", outPath, rebuilt.size());
    }
#endif
    std::printf("%s\n", failures == 0 ? "OK" : "FAILED");
    return failures == 0 ? 0 : 1;
}
