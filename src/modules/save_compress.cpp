// save_compress (off by default): the compression of a save's ZIP members, 0x153C790, without the game's
// byte-at-a-time CRC-32 loop, and optionally on several threads (lab docs: docs/autosave-cost.md,
// "Kompresja zapisu w kodzie").
//
// With compress_autosave (and for compressed saves in general) the game compresses every member of the
// archive (gamestate, meta, ai, ...) on the calling thread: about 0.35 s for a 64 MB gamestate. The hook
// replaces 0x153C790 as a whole:
//   - "on": CompressEntry (save_compress_core.hpp), the function call for call with the game's own zlib
//     1.2.8, the same reads, buffers and flushes, and a slice-by-8 CRC: the file is byte for byte the game's.
//   - "verify": the game's own function runs as well, on the same input into a capture; its bytes and CRC go
//     into the file, and a difference in bytes, CRC or result (or between the two CRC loops) is counted and
//     logged.
//   - save_compress_parallel = "on": members of at least four 1 MiB chunks are read whole and compressed
//     in chunks on several threads (save_compress_parallel.hpp, Bellows' zlib): a valid ZIP member with the
//     same name, method, CRC and sizes, but other DEFLATE bytes than the game's. "verify" also decodes the
//     result and compares it with the input before it is written. When anything fails before the first byte
//     is written, the member goes the "on" way.
// The callers write the local header, the sizes and the CRC as before. Every call appends a row to
// save_compress.tsv in the data directory.
#include "modules.hpp"
#include "save_compress_core.hpp"
#include "save_compress_parallel.hpp"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

extern "C" {
// requests = members compressed, hits = members compressed in parallel, misses = parallel attempts that fell
// back to the game's way, skips = members below the parallel size, verifications = CRC or inflate checks,
// mismatches = checks that differed, mode = 1 on / 2 verify.
__declspec(dllexport) sb::Stats EU4SB_SaveCompressStats = SB_STATS_INIT;
}

namespace {

using MemberFn = int (*)(void* writer, void* input, std::uint8_t* buffer, std::uint64_t size, std::uint32_t* crc, int level);

anvil::HookOf<sym::zip_deflate_member> g_hook;
MemberFn g_next = nullptr;
sb::savez::Deflater g_game;
bool g_verifyCrc = false;
int g_parallel = 0;  // 0 off, 1 on, 2 verify
std::wstring* g_dir = nullptr;
std::mutex* g_fileLock = nullptr;

// The input stream's and the archive writer's vtable slots.
void** Vtable(void* object) { return *static_cast<void***>(object); }
int StreamSize(void* input) { return reinterpret_cast<int (*)(void*)>(Vtable(input)[0x88 / 8])(input); }
void StreamSeek(void* input, int position) { reinterpret_cast<void (*)(void*, int)>(Vtable(input)[0x60 / 8])(input, position); }
void StreamRead(void* input, std::uint8_t* destination, int length) {
    reinterpret_cast<void (*)(void*, std::uint8_t*, int)>(Vtable(input)[0x20 / 8])(input, destination, length);
}
void WriterWrite(void* writer, const std::uint8_t* data, std::int64_t length) {
    reinterpret_cast<void (*)(void*, const std::uint8_t*, std::int64_t)>(Vtable(writer)[0x50 / 8])(writer, data, length);
}

std::int64_t Now() { return sb::Ticks(); }

unsigned ParallelThreads() {
    DWORD_PTR process = 0, system = 0;
    unsigned cpus = GetProcessAffinityMask(GetCurrentProcess(), &process, &system) ? std::popcount(process) : 1;
    return std::clamp(cpus, 1u, 8u);
}

struct Row {
    const char* path = "game";
    int level = 0;
    std::int64_t input = 0, output = 0;
    std::size_t chunks = 0;
    unsigned threads = 1;
    std::int64_t totalTicks = 0, crcTicks = 0, gameCrcTicks = 0, checkTicks = 0;
    bool mismatch = false;
    bool tried = false;  // the parallel way took the member (a "fallback" row when it then went the game's way)
};

void AppendRow(const Row& row) {
    try {
        std::lock_guard lock(*g_fileLock);
        const std::wstring path = *g_dir + L"\\save_compress.tsv";
        const bool fresh = GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES;
        FILE* file = nullptr;
        if (_wfopen_s(&file, path.c_str(), L"ab") != 0 || file == nullptr) return;
        if (fresh) std::fputs("time\tpath\tlevel\tinput_bytes\toutput_bytes\tchunks\tthreads\ttotal_ms\tcrc_ms\tgame_crc_ms\tcheck_ms\tmismatch\n", file);
        SYSTEMTIME t;
        GetLocalTime(&t);
        const double ms = sb::TicksToMs(1);
        std::fprintf(file, "%04u-%02u-%02u %02u:%02u:%02u.%03u\t%s\t%d\t%lld\t%lld\t%zu\t%u\t%.3f\t%.3f\t%.3f\t%.3f\t%d\n",
                     t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, row.path, row.level,
                     row.input, row.output, row.chunks, row.threads, row.totalTicks * ms, row.crcTicks * ms,
                     row.gameCrcTicks * ms, row.checkTicks * ms, row.mismatch ? 1 : 0);
        std::fclose(file);
    } catch (...) {
    }
}

struct FreeVirtual {
    void operator()(std::uint8_t* p) const noexcept { VirtualFree(p, 0, MEM_RELEASE); }
};

// An archive writer that keeps what is written to it ("verify"): the game's own function writes into one
// through its vtable (only the write slot, +0x50; any other slot marks the capture unusable), CompressEntry
// into another through EntryIo.
struct Capture {
    void** vtable;
    std::vector<std::uint8_t> bytes;
    std::vector<std::int64_t> writes;  // the length of each write, in order
    bool failed = false;
};

void CaptureWrite(void* self, const std::uint8_t* data, std::int64_t length) {
    auto* capture = static_cast<Capture*>(self);
    try {
        capture->bytes.insert(capture->bytes.end(), data, data + length);
        capture->writes.push_back(length);
    } catch (...) {
        capture->failed = true;
    }
}

void CaptureOther(void* self) { static_cast<Capture*>(self)->failed = true; }

void* g_captureVtable[32] = {};

// "verify": the game's own 0x153C790 into one capture, CompressEntry (with the game's CRC loop as well) into
// another, both from the start of the same input; the game's writes, CRC and result go into the archive, and
// a difference in bytes, CRC or result is counted. Without the game's capture (memory) the member goes the
// "on" way unchecked.
int VerifyEntry(const sb::savez::EntryIo& io, void* input, std::uint8_t* buffer, std::uint64_t size, std::uint32_t* crc,
                int level, Row& row, sb::savez::EntryTimes& times) {
    Capture game{g_captureVtable};
    Capture ours{g_captureVtable};
    std::uint32_t gameCrc = *crc;
    std::uint32_t ourCrc = *crc;
    void* gameWriter = &game;
    const std::int64_t start = Now();
    const int gameWritten = g_next(&gameWriter, input, buffer, size, &gameCrc, level);
    row.checkTicks = Now() - start;
    if (game.failed) return sb::savez::CompressEntry(g_game, io, buffer, size, crc, level, false, &times, &Now);
    sb::savez::EntryIo captured = io;
    captured.writer = &ours;
    captured.write = CaptureWrite;
    const int ourWritten = sb::savez::CompressEntry(g_game, captured, buffer, size, &ourCrc, level, true, &times, &Now);
    if (!ours.failed) {
        InterlockedIncrement64(&EU4SB_SaveCompressStats.verifications);
        if (ourWritten != gameWritten || ourCrc != gameCrc || ours.bytes != game.bytes || times.crcMismatch) {
            InterlockedIncrement64(&EU4SB_SaveCompressStats.mismatches);
            row.mismatch = true;
            sb::Log(ANVIL_LOG_WARNING,
                    "save_compress: a member differed from the game's (%d and %d bytes, CRC %08X and %08X, CRC loop %s); "
                    "the game's is used",
                    ourWritten, gameWritten, ourCrc, gameCrc, times.crcMismatch ? "differed" : "the same");
        }
    }
    std::int64_t offset = 0;
    for (const std::int64_t length : game.writes) {
        io.write(io.writer, game.bytes.data() + offset, length);
        offset += length;
    }
    *crc = gameCrc;
    return gameWritten;
}

// The parallel way; false (nothing written) when the member goes the game's way instead.
bool CompressInParallel(const sb::savez::EntryIo& io, std::uint32_t* crc, int level, Row& row, int& written) {
    const int size = io.size(io.input);
    if (size <= 0 || static_cast<std::size_t>(size) < sb::savez::kParallelMinChunks * sb::savez::kParallelChunk) {
        InterlockedIncrement64(&EU4SB_SaveCompressStats.skips);
        return false;
    }
    row.tried = true;
    std::unique_ptr<std::uint8_t, FreeVirtual> data(
        static_cast<std::uint8_t*>(VirtualAlloc(nullptr, static_cast<SIZE_T>(size), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)));
    if (data == nullptr) return false;
    io.seek(io.input, 0);
    io.read(io.input, data.get(), size);
    sb::savez::ParallelResult result;
    // The game's register convention: *crc in, ~update(*crc, data) out; zlib's crc32 continues from ~*crc.
    const bool ok = sb::savez::CompressParallel(data.get(), static_cast<std::size_t>(size), level, sb::savez::kParallelChunk,
                                                ParallelThreads(), ~*crc, result);
    row.input = size;
    row.chunks = result.pieces.size();
    row.threads = result.threads;
    if (!ok || result.compressedSize > 0x7FFFFFFFu) return false;
    if (g_parallel == 2) {
        const std::int64_t start = Now();
        const bool same = sb::savez::InflateMatches(result, data.get(), static_cast<std::size_t>(size));
        row.checkTicks = Now() - start;
        InterlockedIncrement64(&EU4SB_SaveCompressStats.verifications);
        if (!same) {
            InterlockedIncrement64(&EU4SB_SaveCompressStats.mismatches);
            row.mismatch = true;
            sb::Log(ANVIL_LOG_WARNING, "save_compress: a parallel member did not decode to its input; the game's way instead");
            return false;
        }
    }
    for (const auto& piece : result.pieces) io.write(io.writer, piece.bytes.get(), static_cast<std::int64_t>(piece.size));
    *crc = result.crc;
    written = static_cast<int>(result.compressedSize);
    return true;
}

int Detour(void* writer, void* input, std::uint8_t* buffer, std::uint64_t size, std::uint32_t* crc, int level) {
    const std::int64_t start = Now();
    InterlockedIncrement64(&EU4SB_SaveCompressStats.requests);
    sb::savez::EntryIo io;
    io.input = input;
    io.writer = *static_cast<void**>(writer);
    io.size = StreamSize;
    io.seek = StreamSeek;
    io.read = StreamRead;
    io.write = WriterWrite;
    Row row;
    row.level = level;
    int written = 0;
    bool done = false;
    if (g_parallel != 0) {
        try {
            done = CompressInParallel(io, crc, level, row, written);
        } catch (...) {
            done = false;
        }
        if (done) {
            InterlockedIncrement64(&EU4SB_SaveCompressStats.hits);
            row.path = "parallel";
        } else if (row.tried) {
            InterlockedIncrement64(&EU4SB_SaveCompressStats.misses);
        }
    }
    if (!done) {
        sb::savez::EntryTimes times;
        written = g_verifyCrc ? VerifyEntry(io, input, buffer, size, crc, level, row, times)
                              : sb::savez::CompressEntry(g_game, io, buffer, size, crc, level, false, &times, &Now);
        row.path = row.tried ? "fallback" : g_verifyCrc ? "verify" : "game";
        row.crcTicks = times.crcTicks;
        row.gameCrcTicks = times.referenceCrcTicks;
        row.input = times.inputSize;
    }
    row.output = written;
    row.totalTicks = Now() - start;
    AppendRow(row);
    return written;
}

}  // namespace

namespace sb {

void SetSaveCompressParallel(int mode) { g_parallel = mode; }

bool InstallSaveCompress(anvil::Host& host, Mode mode) {
    if (mode == Mode::Off) return false;
    g_verifyCrc = mode == Mode::Verify;
    try {
        g_dir = new std::wstring(host.data_dir().wstring());
        g_fileLock = new std::mutex;
    } catch (...) {
        return false;
    }
    g_game.init2 = host.function(sym::zlib_deflate_init2);
    g_game.deflate = host.function(sym::zlib_deflate);
    g_game.end = host.function(sym::zlib_deflate_end);
    for (auto& slot : g_captureVtable) slot = reinterpret_cast<void*>(&CaptureOther);
    g_captureVtable[0x50 / 8] = reinterpret_cast<void*>(&CaptureWrite);
    host.hook(g_hook, sym::zip_deflate_member, &Detour);
    g_next = g_hook.next_pointer();
    InterlockedExchange(&EU4SB_SaveCompressStats.mode, g_verifyCrc ? 2 : 1);
    InterlockedExchange(&EU4SB_SaveCompressStats.initialized, 1);
    static const char* const kParallel[] = {"off", "on", "verify"};
    Log(ANVIL_LOG_INFO, "save_compress: %s, parallel %s", g_verifyCrc ? "verifying against the game's function" : "on",
        kParallel[std::clamp(g_parallel, 0, 2)]);
    return true;
}

void ReportSaveCompress() {
    // No benchmark counters: the report is at its limit of 128 fields; the mode is there, the counters in the log.
    LogStats("save_compress", EU4SB_SaveCompressStats);
}

}  // namespace sb
