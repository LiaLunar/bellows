// save_compress: the slice-by-8 CRC against the game's byte loop and zlib, CompressEntry (the game's
// 0x153C790 written out) with Bellows' zlib standing in for the game's, and the parallel compression.
// The offline check against saves the game wrote is tools/save_compress_offline.cpp.
#include "modules/save_compress_core.hpp"
#include "modules/save_compress_parallel.hpp"

#include <zlib.h>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;
using sb::savez::CrcUpdate;
using sb::savez::CrcUpdateBytewise;

std::mt19937_64 g_random(20261102);

Bytes RandomBytes(std::size_t size) {
    Bytes b(size);
    for (auto& v : b) v = static_cast<std::uint8_t>(g_random());
    return b;
}

// Text shaped like a save: keys, numbers, nesting.
Bytes SaveLikeText(std::size_t size) {
    static const char* const kKeys[] = {"owner", "controller", "base_tax", "base_production", "manpower", "culture",
                                        "religion", "trade_goods", "date", "id", "type", "value", "modifier"};
    std::string s;
    s.reserve(size + 64);
    while (s.size() < size) {
        const auto r = g_random();
        s += kKeys[r % 13];
        s += '=';
        if (r & 0x100) s += std::to_string(static_cast<int>((r >> 16) % 2000) - 1000) + "." + std::to_string((r >> 32) % 1000);
        else s += "\"TAG" + std::to_string((r >> 16) % 50) + "\"";
        s += (r & 0x200) ? "\n\t\t" : "\n";
        if ((r & 0xF000) == 0) s += "history={\n";
        if ((r & 0xF000) == 0x1000) s += "}\n";
    }
    s.resize(size);
    return Bytes(s.begin(), s.end());
}

Bytes Inflate(const Bytes& in, std::size_t expected) {
    Bytes out(expected + 1);
    z_stream s{};
    assert(inflateInit2(&s, -15) == Z_OK);
    s.next_in = const_cast<Bytef*>(in.data());
    s.avail_in = static_cast<uInt>(in.size());
    s.next_out = out.data();
    s.avail_out = static_cast<uInt>(out.size());
    assert(inflate(&s, Z_FINISH) == Z_STREAM_END);
    assert(s.avail_in == 0);
    out.resize(out.size() - s.avail_out);
    inflateEnd(&s);
    return out;
}

void TestCrc() {
    const char* check = "123456789";
    const auto* p = reinterpret_cast<const std::uint8_t*>(check);
    assert(~CrcUpdate(0xFFFFFFFFu, p, 9) == 0xCBF43926u);
    assert(~CrcUpdateBytewise(0xFFFFFFFFu, p, 9) == 0xCBF43926u);
    // The game's table: entry 1 is 0x77073096 (lab docs).
    assert(sb::savez::kCrcTables[0][1] == 0x77073096u);
    const Bytes data = RandomBytes(1 << 16);
    for (std::size_t offset = 0; offset < 9; ++offset) {
        for (std::size_t length = 0; length < 200; ++length) {
            const std::uint32_t reg = static_cast<std::uint32_t>(g_random());
            assert(CrcUpdate(reg, data.data() + offset, length) == CrcUpdateBytewise(reg, data.data() + offset, length));
        }
    }
    const std::size_t big = data.size() - 3;
    assert(CrcUpdate(0x12345678u, data.data() + 3, big) == CrcUpdateBytewise(0x12345678u, data.data() + 3, big));
    assert(~CrcUpdate(0xFFFFFFFFu, data.data(), data.size()) == sb::savez::ZlibCrc32(0, data.data(), data.size()));
    std::puts("crc: ok");
}

// The game's input stream and archive writer over memory, with a log of the calls.
struct MemoryIo {
    const Bytes* input = nullptr;
    int position = 0;
    Bytes output;
    int seeks = 0, reads = 0, writes = 0;
};

sb::savez::EntryIo IoOver(MemoryIo& m) {
    sb::savez::EntryIo io;
    io.input = &m;
    io.writer = &m;
    io.size = [](void* p) { return static_cast<int>(static_cast<MemoryIo*>(p)->input->size()); };
    io.seek = [](void* p, int position) {
        auto* m = static_cast<MemoryIo*>(p);
        assert(position >= 0 && position <= static_cast<int>(m->input->size()));
        m->position = position;
        ++m->seeks;
    };
    io.read = [](void* p, std::uint8_t* destination, int length) {
        auto* m = static_cast<MemoryIo*>(p);
        assert(m->position + length <= static_cast<int>(m->input->size()));
        std::memcpy(destination, m->input->data() + m->position, static_cast<std::size_t>(length));
        m->position += length;
        ++m->reads;
    };
    io.write = [](void* p, const std::uint8_t* bytes, std::int64_t length) {
        auto* m = static_cast<MemoryIo*>(p);
        m->output.insert(m->output.end(), bytes, bytes + length);
        ++m->writes;
    };
    return io;
}

sb::savez::Deflater BellowsZlib() {
    sb::savez::Deflater z;
    z.init2 = [](void* s, int level, int method, int windowBits, int memLevel, int strategy, const char* version,
                 int size) {
        return deflateInit2_(static_cast<z_stream*>(s), level, method, windowBits, memLevel, strategy, version, size);
    };
    z.deflate = [](void* s, int flush) { return deflate(static_cast<z_stream*>(s), flush); };
    z.end = [](void* s) { return deflateEnd(static_cast<z_stream*>(s)); };
    return z;
}

void TestCompressEntry() {
    const sb::savez::Deflater z = BellowsZlib();
    const Bytes inputs[] = {Bytes{}, SaveLikeText(1), SaveLikeText(100000), SaveLikeText(3000000), RandomBytes(300000)};
    for (const Bytes& input : inputs) {
        Bytes reference;
        for (const std::uint64_t bufferSize : {std::uint64_t{50000000}, std::uint64_t{1 << 20}, std::uint64_t{256 * 1024}}) {
            MemoryIo memory;
            memory.input = &input;
            Bytes buffer(static_cast<std::size_t>(bufferSize));
            std::uint32_t crc = 0xFFFFFFFFu;
            sb::savez::EntryTimes times;
            const int written = sb::savez::CompressEntry(z, IoOver(memory), buffer.data(), bufferSize, &crc, 3, true, &times);
            assert(written == static_cast<int>(memory.output.size()));
            assert(!times.crcMismatch);
            assert(times.inputSize == static_cast<int>(input.size()));
            assert(crc == sb::savez::ZlibCrc32(0, input.data(), input.size()));
            assert(Inflate(memory.output, input.size()) == input);
            // Each read follows a seek to what deflate consumed; the whole input is read at least once.
            assert(memory.seeks == memory.reads);
            if (!input.empty()) assert(memory.reads >= static_cast<int>((input.size() + bufferSize / 2 - 1) / (bufferSize / 2)));
            // zlib's raw DEFLATE does not depend on how the input was handed over here.
            if (reference.empty()) reference = memory.output;
            assert(memory.output == reference);
        }
    }
    // Incompressible input: each turn's output is about as large as its input (when the output half fills up,
    // deflate leaves input unconsumed and the next turn seeks back to it; zlib's pending buffer usually
    // absorbs the excess, so the reads printed show whether that happened).
    const Bytes random = RandomBytes(3 << 20);
    MemoryIo memory;
    memory.input = &random;
    Bytes buffer(1 << 20);
    std::uint32_t crc = 0xFFFFFFFFu;
    const int written = sb::savez::CompressEntry(z, IoOver(memory), buffer.data(), buffer.size(), &crc, 3);
    assert(written == static_cast<int>(memory.output.size()));
    assert(crc == sb::savez::ZlibCrc32(0, random.data(), random.size()));
    assert(Inflate(memory.output, random.size()) == random);
    std::printf("compress_entry: incompressible input in %d reads of at most 512 KiB\n", memory.reads);
    // As the game: when the end of the stream does not fit the output half, deflate(Z_FINISH) is called
    // again with no room left, gets Z_BUF_ERROR, and the function returns 0 (the callers never pass so
    // small a buffer: 50,000,000 bytes or the rest of a 90 MiB arena).
    const Bytes text = SaveLikeText(300000);
    MemoryIo small;
    small.input = &text;
    Bytes tiny(4096);
    std::uint32_t smallCrc = 0xFFFFFFFFu;
    assert(sb::savez::CompressEntry(z, IoOver(small), tiny.data(), tiny.size(), &smallCrc, 3) == 0);
    std::puts("compress_entry: ok");
}

void TestParallel() {
    constexpr std::size_t kChunk = 64 * 1024;
    const std::size_t sizes[] = {0, 1, kChunk - 1, kChunk, kChunk + 1, 5 * kChunk + kChunk / 2, 40 * kChunk};
    for (const std::size_t size : sizes) {
        for (const bool text : {true, false}) {
            const Bytes input = text ? SaveLikeText(size) : RandomBytes(size);
            for (const unsigned threads : {1u, 3u, 8u}) {
                sb::savez::ParallelResult result;
                assert(sb::savez::CompressParallel(input.data(), input.size(), 3, kChunk, threads, 0, result));
                assert(result.pieces.size() == (size == 0 ? 1 : (size + kChunk - 1) / kChunk));
                assert(result.threads >= 1 && result.threads <= threads);
                assert(result.crc == sb::savez::ZlibCrc32(0, input.data(), input.size()));
                assert(sb::savez::InflateMatches(result, input.data(), input.size()));
                Bytes joined;
                for (const auto& piece : result.pieces) joined.insert(joined.end(), piece.bytes.get(), piece.bytes.get() + piece.size);
                assert(joined.size() == result.compressedSize);
                assert(Inflate(joined, input.size()) == input);
                // A CRC continued from another value, as the hook passes the game's.
                sb::savez::ParallelResult continued;
                assert(sb::savez::CompressParallel(input.data(), input.size(), 3, kChunk, threads, 0xDEADBEEFu, continued));
                assert(continued.crc == sb::savez::ZlibCrc32(0xDEADBEEFu, input.data(), input.size()));
                // A damaged stream or other data is caught.
                if (size > 1000) {
                    Bytes other = input;
                    other[size / 2] ^= 1;
                    assert(!sb::savez::InflateMatches(result, other.data(), other.size()));
                    assert(!sb::savez::InflateMatches(result, input.data(), input.size() - 1));
                    result.pieces.back().size -= 1;
                    assert(!sb::savez::InflateMatches(result, input.data(), input.size()));
                }
            }
        }
    }
    // Same level and dictionary: within a fraction of a percent of one stream.
    const Bytes text = SaveLikeText(8 << 20);
    sb::savez::ParallelResult result;
    assert(sb::savez::CompressParallel(text.data(), text.size(), 3, sb::savez::kParallelChunk, 4, 0, result));
    uLongf whole = compressBound(static_cast<uLong>(text.size()));
    Bytes single(whole);
    z_stream s{};
    assert(deflateInit2(&s, 3, Z_DEFLATED, -15, 9, Z_DEFAULT_STRATEGY) == Z_OK);
    s.next_in = const_cast<Bytef*>(text.data());
    s.avail_in = static_cast<uInt>(text.size());
    s.next_out = single.data();
    s.avail_out = static_cast<uInt>(single.size());
    assert(deflate(&s, Z_FINISH) == Z_STREAM_END);
    const double ratio = static_cast<double>(result.compressedSize) / static_cast<double>(s.total_out);
    deflateEnd(&s);
    std::printf("parallel: ok (8 MiB of text: %llu bytes against %.0f in one stream, %+.3f%%)\n",
                static_cast<unsigned long long>(result.compressedSize), result.compressedSize / ratio, 100.0 * (ratio - 1.0));
    assert(ratio < 1.01);
}

}  // namespace

int main() {
    TestCrc();
    TestCompressEntry();
    TestParallel();
    return 0;
}
