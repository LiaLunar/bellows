#include "save_compress_parallel.hpp"

#include "save_compress_core.hpp"

#include <zlib.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <new>
#include <thread>

namespace sb::savez {

namespace {

constexpr std::size_t kWindow = 32768;

struct Job {
    const std::uint8_t* data;
    std::size_t size, chunkSize, chunks;
    int level;
    std::uint32_t initialCrc;
    std::vector<Piece>* pieces;
    std::vector<std::uint32_t>* crcs;
    std::atomic<std::size_t> next{0};
    std::atomic<bool> failed{false};
};

bool CompressChunk(const Job& job, std::size_t index) {
    const std::size_t start = index * job.chunkSize;
    const std::size_t length = std::min(job.chunkSize, job.size - start);
    const bool last = index + 1 == job.chunks;
    z_stream s{};
    if (deflateInit2(&s, job.level, Z_DEFLATED, -15, 9, Z_DEFAULT_STRATEGY) != Z_OK) return false;
    bool ok = true;
    if (start != 0) {
        const std::size_t dictionary = std::min(kWindow, start);
        ok = deflateSetDictionary(&s, job.data + start - dictionary, static_cast<uInt>(dictionary)) == Z_OK;
    }
    // deflateBound covers Z_FINISH; a Z_SYNC_FLUSH adds at most an empty stored block (5 bytes) to it.
    const std::size_t capacity = deflateBound(&s, static_cast<uLong>(length)) + 16;
    Piece& piece = (*job.pieces)[index];
    if (ok) {
        piece.bytes.reset(new (std::nothrow) std::uint8_t[capacity]);
        ok = piece.bytes != nullptr;
    }
    if (ok) {
        s.next_in = const_cast<Bytef*>(job.data + start);
        s.avail_in = static_cast<uInt>(length);
        s.next_out = piece.bytes.get();
        s.avail_out = static_cast<uInt>(capacity);
        const int result = deflate(&s, last ? Z_FINISH : Z_SYNC_FLUSH);
        ok = last ? result == Z_STREAM_END : result == Z_OK && s.avail_in == 0 && s.avail_out != 0;
        piece.size = capacity - s.avail_out;
    }
    deflateEnd(&s);
    if (!ok) return false;
    // The game's register update, then zlib's convention (crc32(crc, data) = ~update(~crc, data)).
    const std::uint32_t from = index == 0 ? job.initialCrc : 0;
    (*job.crcs)[index] = ~CrcUpdate(~from, job.data + start, length);
    return true;
}

void Work(Job& job) {
    for (;;) {
        const std::size_t index = job.next.fetch_add(1);
        if (index >= job.chunks || job.failed.load(std::memory_order_relaxed)) return;
        if (!CompressChunk(job, index)) job.failed.store(true);
    }
}

}  // namespace

bool CompressParallel(const std::uint8_t* data, std::size_t size, int level, std::size_t chunkSize,
                      unsigned threads, std::uint32_t initialCrc, ParallelResult& result) {
    result = ParallelResult{};
    if (chunkSize == 0 || size > 0x7FFFFFFFu) return false;
    std::vector<std::uint32_t> crcs;
    try {
        const std::size_t chunks = size == 0 ? 1 : (size + chunkSize - 1) / chunkSize;
        result.pieces.resize(chunks);
        crcs.resize(chunks);
        Job job{data, size, chunkSize, chunks, level, initialCrc, &result.pieces, &crcs};
        const unsigned workers = static_cast<unsigned>(std::min<std::size_t>(std::max(threads, 1u), chunks));
        std::vector<std::thread> helpers;
        helpers.reserve(workers - 1);
        try {
            for (unsigned i = 1; i < workers; ++i) helpers.emplace_back([&job] { Work(job); });
        } catch (...) {
            // Fewer threads: the ones started and this one finish the job.
        }
        Work(job);
        for (auto& helper : helpers) helper.join();
        result.threads = static_cast<unsigned>(helpers.size()) + 1;
        if (job.failed.load()) return false;
    } catch (...) {
        return false;
    }
    std::uint32_t crc = crcs[0];
    std::uint64_t compressed = result.pieces[0].size;
    for (std::size_t i = 1; i < crcs.size(); ++i) {
        const std::size_t length = std::min(chunkSize, size - i * chunkSize);
        crc = static_cast<std::uint32_t>(crc32_combine(crc, crcs[i], static_cast<z_off_t>(length)));
        compressed += result.pieces[i].size;
    }
    result.crc = crc;
    result.compressedSize = compressed;
    return true;
}

bool InflateMatches(const ParallelResult& result, const std::uint8_t* data, std::size_t size) {
    z_stream s{};
    if (inflateInit2(&s, -15) != Z_OK) return false;
    constexpr std::size_t kOut = 1 << 20;
    std::unique_ptr<std::uint8_t[]> out(new (std::nothrow) std::uint8_t[kOut]);
    bool ok = out != nullptr;
    std::size_t produced = 0;
    int status = Z_OK;
    for (std::size_t i = 0; ok && i < result.pieces.size(); ++i) {
        s.next_in = result.pieces[i].bytes.get();
        s.avail_in = static_cast<uInt>(result.pieces[i].size);
        while (ok && s.avail_in != 0) {
            if (status == Z_STREAM_END) {
                ok = false;  // bytes after the end of the stream
                break;
            }
            s.next_out = out.get();
            s.avail_out = static_cast<uInt>(kOut);
            status = inflate(&s, Z_NO_FLUSH);
            const std::size_t length = kOut - s.avail_out;
            if ((status != Z_OK && status != Z_STREAM_END) || length > size - produced ||
                std::memcmp(out.get(), data + produced, length) != 0) {
                ok = false;
                break;
            }
            produced += length;
        }
    }
    // Output still held back by inflate at the end of the input.
    while (ok && status == Z_OK) {
        s.next_out = out.get();
        s.avail_out = static_cast<uInt>(kOut);
        status = inflate(&s, Z_NO_FLUSH);
        const std::size_t length = kOut - s.avail_out;
        if ((status != Z_OK && status != Z_STREAM_END) || (status == Z_OK && length == 0) || length > size - produced ||
            std::memcmp(out.get(), data + produced, length) != 0) {
            ok = false;
            break;
        }
        produced += length;
    }
    inflateEnd(&s);
    return ok && status == Z_STREAM_END && produced == size;
}

std::uint32_t ZlibCrc32(std::uint32_t crc, const std::uint8_t* data, std::size_t size) {
    if (size == 0) return crc;  // zlib's crc32 returns 0 for a null buffer (an empty vector's data())
    return static_cast<std::uint32_t>(crc32(crc, data, static_cast<uInt>(size)));
}

}  // namespace sb::savez
