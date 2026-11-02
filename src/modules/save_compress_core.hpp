#pragma once

// save_compress: the game's compression of one ZIP member (0x153C790) written out, and a faster CRC-32
// (lab docs: docs/autosave-cost.md, "Kompresja zapisu w kodzie").
//
// 0x153C790(writer, input, buffer, size, crc, level) compresses the whole input stream into the archive
// writer as raw DEFLATE: deflateInit2_(level, 8, -15, 9, 0), then, while input is left, it seeks the
// input to what deflate consumed, reads at most size / 2 bytes into the first half of the buffer, calls
// deflate(Z_NO_FLUSH) with the second half as output, runs CRC-32 over the bytes deflate consumed (a byte
// at a time, table 0x1D91CD0) and writes the new output; then deflate(Z_FINISH) until the stream ends.
// It returns the compressed size (0 on an error) and leaves ~crc in *crc. The callers write the local
// header before it and rewrite it with the sizes and CRC after it.
//
// CompressEntry is that function call for call, with the same zlib functions (in the plugin the game's
// own), the same reads, seeks, buffers and flushes; only the CRC loop is replaced by CrcUpdate, which
// computes the same function eight bytes at a time. The DEFLATE bytes are therefore the game's.

#include <zlib.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace sb::savez {

static_assert(sizeof(z_stream) == 0x58, "the game's z_stream (zlib 1.2.8, 64-bit Windows)");

// The CRC-32 register update of the game's loop (reflected polynomial 0xEDB88320, no inversion):
// reg = (reg >> 8) ^ table[(reg ^ byte) & 0xFF] for each byte. The CRC of data is ~update(~0, data).
inline constexpr std::array<std::array<std::uint32_t, 256>, 8> kCrcTables = [] {
    std::array<std::array<std::uint32_t, 256>, 8> t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        t[0][i] = c;
    }
    for (std::uint32_t i = 0; i < 256; ++i)
        for (int s = 1; s < 8; ++s) t[s][i] = (t[s - 1][i] >> 8) ^ t[0][t[s - 1][i] & 0xFF];
    return t;
}();

// The game's loop, byte by byte (also the reference for the tests).
inline std::uint32_t CrcUpdateBytewise(std::uint32_t reg, const std::uint8_t* p, std::size_t n) noexcept {
    for (; n != 0; --n) reg = (reg >> 8) ^ kCrcTables[0][(reg ^ *p++) & 0xFF];
    return reg;
}

// The same register update, slice-by-8 (little-endian).
inline std::uint32_t CrcUpdate(std::uint32_t reg, const std::uint8_t* p, std::size_t n) noexcept {
    const auto& t = kCrcTables;
    for (; n >= 8; n -= 8, p += 8) {
        const std::uint32_t lo = reg ^ (static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
                                        static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24);
        const std::uint32_t hi = static_cast<std::uint32_t>(p[4]) | static_cast<std::uint32_t>(p[5]) << 8 |
                                 static_cast<std::uint32_t>(p[6]) << 16 | static_cast<std::uint32_t>(p[7]) << 24;
        reg = t[7][lo & 0xFF] ^ t[6][(lo >> 8) & 0xFF] ^ t[5][(lo >> 16) & 0xFF] ^ t[4][lo >> 24] ^
              t[3][hi & 0xFF] ^ t[2][(hi >> 8) & 0xFF] ^ t[1][(hi >> 16) & 0xFF] ^ t[0][hi >> 24];
    }
    return CrcUpdateBytewise(reg, p, n);
}

// The game's input stream and archive writer behind plain function pointers (in the plugin: their vtable
// slots +0x88 size, +0x60 seek, +0x20 read and the writer's +0x50 write).
struct EntryIo {
    void* input = nullptr;
    void* writer = nullptr;
    int (*size)(void* input) = nullptr;
    void (*seek)(void* input, int position) = nullptr;
    void (*read)(void* input, std::uint8_t* destination, int length) = nullptr;
    void (*write)(void* writer, const std::uint8_t* data, std::int64_t length) = nullptr;
};

// The zlib entry points used, on a z_stream (in the plugin: the game's 0x181FC70, 0x181F210, 0x181FB90).
struct Deflater {
    int (*init2)(void* strm, int level, int method, int windowBits, int memLevel, int strategy, const char* version,
                 int streamSize) = nullptr;
    int (*deflate)(void* strm, int flush) = nullptr;
    int (*end)(void* strm) = nullptr;
};

// What CompressEntry measured (ticks of the caller's clock; 0 when it passes no clock).
struct EntryTimes {
    std::int64_t crcTicks = 0;
    std::int64_t referenceCrcTicks = 0;  // the game's byte loop, when checked
    bool crcMismatch = false;
    int inputSize = 0;  // the input's size at the start
};

// 0x153C790. checkCrc also runs the game's byte loop over the same bytes and keeps its value (crcMismatch
// when the two differ). ticks: an optional clock for the times.
inline int CompressEntry(const Deflater& z, const EntryIo& io, std::uint8_t* buffer, std::uint64_t size,
                         std::uint32_t* crc, int level, bool checkCrc = false, EntryTimes* times = nullptr,
                         std::int64_t (*ticks)() = nullptr) {
    z_stream s{};
    if (z.init2(&s, level, 8, -15, 9, 0, "1.2.3", 0x58) != Z_OK) return 0;
    const std::uint64_t half = size >> 1;
    const int inputHalf = static_cast<int>(half);
    const auto outputHalf = static_cast<std::uint32_t>(size - half);
    std::uint8_t* const output = buffer + half;
    int written = 0;
    int left = io.size(io.input);
    if (times != nullptr) times->inputSize = left;
    while (left > 0) {
        io.seek(io.input, io.size(io.input) - left);
        const int chunk = left < inputHalf ? left : inputHalf;
        io.read(io.input, buffer, chunk);
        const std::uint32_t consumedBefore = s.total_in;
        s.next_in = buffer;
        s.avail_in = static_cast<std::uint32_t>(chunk);
        s.next_out = output;
        s.avail_out = outputHalf;
        if (z.deflate(&s, Z_NO_FLUSH) != Z_OK) {
            z.end(&s);
            return 0;
        }
        const std::uint32_t consumed = s.total_in - consumedBefore;
        const std::int64_t start = ticks != nullptr ? ticks() : 0;
        std::uint32_t reg = CrcUpdate(*crc, buffer, consumed);
        if (times != nullptr && ticks != nullptr) times->crcTicks += ticks() - start;
        if (checkCrc) {
            const std::int64_t referenceStart = ticks != nullptr ? ticks() : 0;
            const std::uint32_t reference = CrcUpdateBytewise(*crc, buffer, consumed);
            if (times != nullptr) {
                if (ticks != nullptr) times->referenceCrcTicks += ticks() - referenceStart;
                if (reference != reg) times->crcMismatch = true;
            }
            reg = reference;
        }
        *crc = reg;
        const int produced = static_cast<int>(s.total_out) - written;
        left = io.size(io.input) - static_cast<int>(s.total_in);
        if (produced > 0) {
            written += produced;
            io.write(io.writer, output, produced);
        }
    }
    s.next_in = nullptr;
    s.avail_in = 0;
    s.next_out = output;
    s.avail_out = outputHalf;
    int result;
    do {
        result = z.deflate(&s, Z_FINISH);
    } while (result == Z_OK);
    if (result != Z_STREAM_END) return 0;  // as the game: without deflateEnd
    const int produced = static_cast<int>(s.total_out) - written;
    if (produced > 0) {
        written += produced;
        io.write(io.writer, output, produced);
    }
    z.end(&s);
    *crc = ~*crc;
    return written;
}

}  // namespace sb::savez
