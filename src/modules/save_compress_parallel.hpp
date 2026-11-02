#pragma once

// save_compress_parallel: one ZIP member's raw DEFLATE stream from chunks compressed on several threads,
// as pigz does (lab docs: docs/autosave-cost.md, hypothesis (b)). Each chunk is compressed with zlib
// (the copy linked into Bellows, not the game's) at the game's level and parameters, with the 32 KiB of
// input before it as its dictionary; every chunk but the last ends with Z_SYNC_FLUSH (an empty stored
// block, so the next one starts on a byte boundary) and the last with Z_FINISH. Joined in order, the
// pieces are one valid DEFLATE stream of the whole input; its bytes differ from the game's. The CRC-32
// of the whole input comes from the chunks' CRCs (crc32_combine).

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace sb::savez {

// 1 MiB chunks; members smaller than four chunks go through the game's path (save_compress_core.hpp).
inline constexpr std::size_t kParallelChunk = 1 << 20;
inline constexpr std::size_t kParallelMinChunks = 4;

struct Piece {
    std::unique_ptr<std::uint8_t[]> bytes;
    std::size_t size = 0;
};

struct ParallelResult {
    std::vector<Piece> pieces;  // in stream order
    std::uint64_t compressedSize = 0;
    std::uint32_t crc = 0;  // zlib convention: crc32(initialCrc, data)
    unsigned threads = 0;   // threads that compressed, the caller's included
};

// Compresses data (size bytes). initialCrc: zlib-style CRC to continue from (0 for a whole member).
// threads: the most threads to use, the caller's included (at least 1). Returns false when zlib failed
// or memory ran out (result is then incomplete).
bool CompressParallel(const std::uint8_t* data, std::size_t size, int level, std::size_t chunkSize,
                      unsigned threads, std::uint32_t initialCrc, ParallelResult& result);

// Decodes the joined pieces with zlib's inflate and compares them with data: true when the stream is
// valid, ends exactly at its end and gives exactly data.
bool InflateMatches(const ParallelResult& result, const std::uint8_t* data, std::size_t size);

// zlib's crc32 over data (the plugin's copy; the tests compare it with save_compress_core.hpp).
std::uint32_t ZlibCrc32(std::uint32_t crc, const std::uint8_t* data, std::size_t size);

}  // namespace sb::savez
