// Start-up answers for the stat calls of PhysFS verifyPath (ported from the
// lab's startup_verifypath_listing.inc).
//
// PhysFS verifyPath runs whenever symbolic links are not permitted (always in
// this game). For every searched directory archive it stats each prefix of the
// path ("gfx", "gfx/interface", "gfx/interface/x.dds") through the archiver's
// stat slot (call [r9+68h]) only to learn: did the stat succeed, is the entry a
// symlink (filetype 2), and on failure is the error PHYSFS_ERR_NOT_FOUND (0xb).
//
// The call and the following `mov ebp,eax` are replaced as one block (Anvil
// replace_block on the declared symbol verifypath_stat_call): the callback
// calls SB_VerifyPathStat with the call's arguments and writes its result to
// eax and ebp. For the directory archiver it answers from
// once-read directory listings exactly as the platform stat would for
// verifyPath's purposes: existing directory/junction -> rc 1, filetype 1;
// existing file -> rc 1, filetype 0; missing name or directory -> rc 0 with
// PHYSFS_setErrorCode(0xb). Symbolic links, other reparse points, unusual
// spellings, non-directory archives, unreadable listings and a disabled module
// go to the original stat, and so does anything after the game is ready unless
// verifypath_session = "on". Every 256th answer is compared with the original;
// a disagreement disables the module ("verify" mode compares every answer).
// Assumes game and mod files do not change while the game starts.
//
// verifypath_session = "on" (on by default since 1.2) keeps the module past the ready
// event: loading a save or starting a game makes the game look for thousands
// of textures in every mod, and each look stats every prefix in every search
// path (lab docs: docs/autosave-cost.md, "Co zostaje w Continue"). After the
// ready event a listing older than 30 s is read again, so a file added to a
// game or mod folder during a session counts after at most 30 s. The counters
// after the ready event go to verifypath_listing.tsv every 5 s.
#include "modules.hpp"
#include "verifypath_listing_core.hpp"

#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>

namespace {

using sb::verifypath::Answer;
using sb::verifypath::Kind;
using sb::verifypath::Listing;
using sb::verifypath::ReadListing;

constexpr int kPhysfsNotFound = 0xB;
constexpr std::size_t kArchiverStat = 0x68;  // the stat slot of a PhysFS archiver function table
constexpr std::size_t kArchiverOpenRead = 0x40;  // its openRead slot

using StatFn = int (*)(void*, const char*, void*);
using SetErrorFn = void (*)(int);
using GetErrorFn = int (*)();
using GetWriteDirFn = const char* (*)();

struct PhysfsStat {
    std::int64_t filesize;
    std::int64_t modtime;
    std::int64_t createtime;
    std::int64_t accesstime;
    std::int32_t filetype;
    std::int32_t readonly;
};
static_assert(sizeof(PhysfsStat) == 0x28, "PHYSFS_Stat layout drift");

uintptr_t g_dirArchiver = 0;
SetErrorFn g_setError = nullptr;
GetErrorFn g_getError = nullptr;
GetWriteDirFn g_getWriteDir = nullptr;
SRWLOCK g_lock = SRWLOCK_INIT;
std::unordered_map<std::wstring, std::shared_ptr<const Listing>>* g_listings = nullptr;
volatile LONG64 g_answerSequence = 0;
bool g_verifyAll = false;
bool g_session = false;  // verifypath_session
std::wstring* g_dir = nullptr;
constexpr DWORD kWriteEveryMs = 5000;

}  // namespace

extern "C" {
// requests = calls seen, hits = answered "exists", misses = answered NOT_FOUND,
// skips = passed to the original, duplicateKeys = listings read,
// verifications/mismatches = checks against the original, mode = 1 patched,
// cacheDisabled = disabled after a mismatch.
__declspec(dllexport) sb::Stats EU4SB_VerifyPathStats = SB_STATS_INIT;
// listing_lookup: the stat after verifyPath in getRealDir (PHYSFS_exists, PHYSFS_getRealDir) and the DIR
// archiver's openRead in PHYSFS_openRead; same fields (openRead: hits = passed on to the game for an existing
// name, misses = answered "not found" without CreateFileW).
__declspec(dllexport) sb::Stats EU4SB_RealDirStatStats = SB_STATS_INIT;
__declspec(dllexport) sb::Stats EU4SB_DirOpenReadStats = SB_STATS_INIT;
}

namespace {

std::shared_ptr<const Listing> GetListing(const std::wstring& directory) {
    AcquireSRWLockShared(&g_lock);
    std::shared_ptr<const Listing> existing;
    auto found = g_listings->find(directory);
    if (found != g_listings->end()) existing = found->second;
    ReleaseSRWLockShared(&g_lock);
    if (existing && !(g_session && sb::ReadyReached() && sb::verifypath::Expired(*existing, GetTickCount64()))) return existing;
    std::shared_ptr<const Listing> fresh = ReadListing(directory);
    sb::Increment(&EU4SB_VerifyPathStats.duplicateKeys);
    AcquireSRWLockExclusive(&g_lock);
    std::shared_ptr<const Listing> result;
    try {
        result = existing ? ((*g_listings)[directory] = fresh) : g_listings->emplace(directory, fresh).first->second;
    } catch (...) {
        result = fresh;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return result;
}

// The write directory unknown (no symbol): every root goes to the original stat.
bool IsWriteRoot(const char* root) {
    if (g_getWriteDir == nullptr) return true;
    return sb::verifypath::IsWriteRoot(root, g_getWriteDir());  // PhysFS state lock is recursive; verifyPath already holds it
}

Answer Decide(const char* root, const char* name, Kind& kind) {
    if (root == nullptr || name == nullptr || *name == '\0') return Answer::Original;
    if (IsWriteRoot(root)) return Answer::Original;
    return sb::verifypath::DecideInRoot(root, name, kind, GetListing);
}

bool Disabled() noexcept {
    if (InterlockedCompareExchange(&EU4SB_VerifyPathStats.cacheDisabled, 0, 0) != 0) return true;
    if (InterlockedCompareExchange(&EU4SB_VerifyPathStats.readyDisabled, 0, 0) != 0) return true;
    if (!g_session && sb::ReadyReached()) {
        InterlockedExchange(&EU4SB_VerifyPathStats.readyDisabled, 1);
        return true;
    }
    return false;
}

void WriteFile() {
    const sb::Stats& s = EU4SB_VerifyPathStats;
    try {
        const std::wstring path = *g_dir + L"\\verifypath_listing.tsv";
        const std::wstring temp = path + L".tmp";
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            out << "requests\texists\tnot_found\tto_game\tlistings\tverifications\tmismatches\tdisabled\n"
                << s.requests << "\t" << s.hits << "\t" << s.misses << "\t" << s.skips << "\t" << s.duplicateKeys << "\t"
                << s.verifications << "\t" << s.mismatches << "\t" << s.cacheDisabled << "\n";
        }
        MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
    } catch (...) {
    }
}

DWORD WINAPI WriterThread(void*) {
    LONG64 written = -1;
    for (;;) {
        Sleep(kWriteEveryMs);
        if (sb::ReadyReached() && EU4SB_VerifyPathStats.requests != written) {
            written = EU4SB_VerifyPathStats.requests;
            WriteFile();
        }
    }
}

}  // namespace

extern "C" int SB_VerifyPathStat(void* opaque, const char* name, void* statOut, const void* funcs) {
    sb::Increment(&EU4SB_VerifyPathStats.requests);
    const auto original = *reinterpret_cast<const StatFn*>(static_cast<const unsigned char*>(funcs) + kArchiverStat);
    try {
        Kind kind = Kind::Unknown;
        const Answer answer = (Disabled() || reinterpret_cast<uintptr_t>(funcs) != g_dirArchiver || g_listings == nullptr)
                                  ? Answer::Original
                                  : Decide(static_cast<const char*>(opaque), name, kind);
        if (answer == Answer::Original) {
            sb::Increment(&EU4SB_VerifyPathStats.skips);
            return original(opaque, name, statOut);
        }
        const LONG64 sequence = InterlockedIncrement64(&g_answerSequence);
        if (g_verifyAll || (sequence & 255) == 0) {
            sb::Increment(&EU4SB_VerifyPathStats.verifications);
            PhysfsStat actual{};
            const int rc = original(opaque, name, &actual);
            const int error = rc ? 0 : g_getError();  // clears the code; set again below
            const bool same = answer == Answer::Exists ? (rc == 1 && actual.filetype != 2)
                                                       : (rc == 0 && error == kPhysfsNotFound);
            if (!same || g_verifyAll) {
                if (!same) {
                    const LONG64 count = InterlockedIncrement64(&EU4SB_VerifyPathStats.mismatches);
                    if (!g_verifyAll) InterlockedExchange(&EU4SB_VerifyPathStats.cacheDisabled, 1);
                    if (count <= 10) {
                        sb::Log(ANVIL_LOG_WARNING,
                                "verifypath mismatch: root '%s' name '%s': listing says %s, original rc %d type %d error %d",
                                static_cast<const char*>(opaque), name,
                                answer == Answer::Exists ? (kind == Kind::Directory ? "directory" : "file") : "not found",
                                rc, actual.filetype, error);
                    }
                }
                std::memcpy(statOut, &actual, sizeof(actual));
                if (!rc) g_setError(error);
                return rc;
            }
        }
        if (answer == Answer::NotFound) {
            sb::Increment(&EU4SB_VerifyPathStats.misses);
            g_setError(kPhysfsNotFound);
            return 0;
        }
        PhysfsStat result{};
        result.filesize = kind == Kind::Directory ? 0 : -1;  // not read by verifyPath
        result.modtime = result.createtime = result.accesstime = -1;
        result.filetype = kind == Kind::Directory ? 1 : 0;
        std::memcpy(statOut, &result, sizeof(result));
        sb::Increment(&EU4SB_VerifyPathStats.hits);
        return 1;
    } catch (...) {
        sb::Increment(&EU4SB_VerifyPathStats.skips);
        return original(opaque, name, statOut);
    }
}

// --- listing_lookup -------------------------------------------------------------
//
// After verifyPath passed for a directory archive, PhysFS asks the archiver again:
//  - getRealDir (0x19FAD70; PHYSFS_exists and PHYSFS_getRealDir) calls its stat and only tests the result
//    (`call [rax+68h]; test eax,eax; jne`): the statbuf is a local nobody reads;
//  - PHYSFS_openRead calls its openRead (`call [rax+40h]; mov rsi,rax`). For a name that is not there the
//    DIR archiver's CreateFileW fails, then it stats the path once more (PhysFS 3.0 doOpen) and sets the error
//    code again: NOT_FOUND for ERROR_FILE_NOT_FOUND / ERROR_PATH_NOT_FOUND.
// verifyPath lets a missing last component through (a file may be opened for writing), so every root whose
// folder holds the parent pays these calls under the PhysFS state lock: on Anbennar the checksum task's 9,372
// flattened paths fail in two roots each, and replace_path enumerations call getRealDir for every file.
// Both blocks answer from verifypath_listing's listings with the same rules (the write directory and anything
// unusual go to the game). The stat gives rc 1 for an existing name and rc 0 with NOT_FOUND otherwise;
// openRead answers only "not found" (null, NOT_FOUND) and passes every existing name to the game, so an
// existing file is always opened through the real API. Every 256th answer (every one in "verify") is checked
// against the game's own call; a difference turns listing_lookup off. Start-up only: off at the ready event.

namespace {

anvil::Change g_realDirBlock;  // kept for the process
anvil::Change g_openReadBlock;
bool g_lookupVerifyAll = false;
volatile LONG64 g_lookupSequence = 0;
using OpenReadFn = void* (*)(void*, const char*);

bool LookupDisabled(sb::Stats& stats) noexcept {
    if (InterlockedCompareExchange(&stats.cacheDisabled, 0, 0) != 0) return true;
    if (InterlockedCompareExchange(&EU4SB_VerifyPathStats.cacheDisabled, 0, 0) != 0) return true;
    if (InterlockedCompareExchange(&stats.readyDisabled, 0, 0) != 0) return true;
    if (sb::ReadyReached()) {
        InterlockedExchange(&stats.readyDisabled, 1);
        return true;
    }
    return false;
}

Answer LookupDecide(sb::Stats& stats, const void* opaque, const char* name, const void* funcs, Kind& kind) {
    if (LookupDisabled(stats) || reinterpret_cast<uintptr_t>(funcs) != g_dirArchiver || g_listings == nullptr) {
        return Answer::Original;
    }
    return Decide(static_cast<const char*>(opaque), name, kind);
}

bool LookupSampled() noexcept { return g_lookupVerifyAll || (InterlockedIncrement64(&g_lookupSequence) & 255) == 0; }

void LookupMismatch(sb::Stats& stats, const char* what, const void* opaque, const char* name, const char* said,
                    long long actual, int error) {
    const LONG64 count = InterlockedIncrement64(&stats.mismatches);
    if (!g_lookupVerifyAll) InterlockedExchange(&stats.cacheDisabled, 1);
    if (count <= 10) {
        sb::Log(ANVIL_LOG_WARNING, "listing_lookup %s mismatch: root '%s' name '%s': listing says %s, original %lld error %d",
                what, static_cast<const char*>(opaque), name, said, actual, error);
    }
}

}  // namespace

extern "C" int SB_RealDirStat(void* opaque, const char* name, void* statOut, const void* funcs) {
    sb::Stats& stats = EU4SB_RealDirStatStats;
    sb::Increment(&stats.requests);
    const auto original = *reinterpret_cast<const StatFn*>(static_cast<const unsigned char*>(funcs) + kArchiverStat);
    try {
        Kind kind = Kind::Unknown;
        const Answer answer = name == nullptr ? Answer::Original : LookupDecide(stats, opaque, name, funcs, kind);
        if (answer == Answer::Original) {
            sb::Increment(&stats.skips);
            return original(opaque, name, statOut);
        }
        if (LookupSampled()) {
            sb::Increment(&stats.verifications);
            const int rc = original(opaque, name, statOut);
            const int error = rc ? 0 : g_getError();
            const bool same = answer == Answer::Exists ? rc == 1 : (rc == 0 && error == kPhysfsNotFound);
            if (!same) LookupMismatch(stats, "stat", opaque, name, answer == Answer::Exists ? "exists" : "not found", rc, error);
            if (!rc) g_setError(error);
            return rc;  // the game's own answer whenever it was asked
        }
        if (answer == Answer::NotFound) {
            sb::Increment(&stats.misses);
            g_setError(kPhysfsNotFound);
            return 0;
        }
        PhysfsStat result{};  // not read by getRealDir; filled as in verifypath_listing
        result.filesize = kind == Kind::Directory ? 0 : -1;
        result.modtime = result.createtime = result.accesstime = -1;
        result.filetype = kind == Kind::Directory ? 1 : 0;
        std::memcpy(statOut, &result, sizeof(result));
        sb::Increment(&stats.hits);
        return 1;
    } catch (...) {
        sb::Increment(&stats.skips);
        return original(opaque, name, statOut);
    }
}

extern "C" void* SB_DirOpenRead(void* opaque, const char* name, const void* funcs) {
    sb::Stats& stats = EU4SB_DirOpenReadStats;
    sb::Increment(&stats.requests);
    const auto original = *reinterpret_cast<const OpenReadFn*>(static_cast<const unsigned char*>(funcs) + kArchiverOpenRead);
    try {
        Kind kind = Kind::Unknown;
        const Answer answer = name == nullptr ? Answer::Original : LookupDecide(stats, opaque, name, funcs, kind);
        if (answer != Answer::NotFound) {
            sb::Increment(answer == Answer::Exists ? &stats.hits : &stats.skips);
            return original(opaque, name);  // existing names always through the real API
        }
        if (LookupSampled()) {
            sb::Increment(&stats.verifications);
            void* io = original(opaque, name);
            const int error = io != nullptr ? 0 : g_getError();
            if (io != nullptr || error != kPhysfsNotFound) {
                LookupMismatch(stats, "openRead", opaque, name, "not found", io != nullptr ? 1 : 0, error);
            }
            if (io == nullptr) g_setError(error);
            return io;  // the game's own answer whenever it was asked
        }
        sb::Increment(&stats.misses);
        g_setError(kPhysfsNotFound);
        return nullptr;
    } catch (...) {
        sb::Increment(&stats.skips);
        return original(opaque, name);
    }
}

namespace sb {

bool InstallListingLookup(anvil::Host& host, Mode mode) {
    if (mode == Mode::Off) return false;
    if (g_listings == nullptr || InterlockedCompareExchange(&EU4SB_VerifyPathStats.mode, 0, 0) != 1) {
        Log(ANVIL_LOG_WARNING, "listing_lookup: needs verifypath_listing, which is off; module off");
        return false;
    }
    g_lookupVerifyAll = mode == Mode::Verify;
    // `call [rax+68h]` (stat(opaque = rcx, name = rdx, stat = r8), rax = the function table) and
    // `test eax,eax`; the following jne reads ZF, so the callback sets the flags as the test would.
    Undo undo;
    g_realDirBlock = host.replace_block(sym::realdir_stat_call, [](anvil::Cpu& cpu) {
        const int rc = SB_RealDirStat(reinterpret_cast<void*>(cpu.rcx), reinterpret_cast<const char*>(cpu.rdx),
                                      reinterpret_cast<void*>(cpu.r8), reinterpret_cast<const void*>(cpu.rax));
        cpu.rax = static_cast<std::uint32_t>(rc);
        cpu.rflags = verifypath::TestFlags(cpu.rflags, static_cast<std::uint32_t>(rc));
    });
    undo.add(g_realDirBlock);
    // `call [rax+40h]` (openRead(opaque = rcx, name = rdx)) and `mov rsi,rax`; the game tests rax next.
    g_openReadBlock = host.replace_block(sym::dir_openread_call, [](anvil::Cpu& cpu) {
        void* io = SB_DirOpenRead(reinterpret_cast<void*>(cpu.rcx), reinterpret_cast<const char*>(cpu.rdx),
                                  reinterpret_cast<const void*>(cpu.rax));
        cpu.rax = reinterpret_cast<std::uintptr_t>(io);
        cpu.rsi = reinterpret_cast<std::uintptr_t>(io);
    });
    undo.add(g_openReadBlock);
    undo.keep();
    for (Stats* stats : {&EU4SB_RealDirStatStats, &EU4SB_DirOpenReadStats}) {
        InterlockedExchange(&stats->mode, 1);
        InterlockedExchange(&stats->initialized, 1);
    }
    return true;
}

void ReportListingLookup() {
    LogStats("listing_lookup stat", EU4SB_RealDirStatStats);
    LogStats("listing_lookup openRead", EU4SB_DirOpenReadStats);
}

void SetVerifyPathSession(bool on) noexcept { g_session = on; }

bool InstallVerifyPathListing(anvil::Host& host, Mode mode) {
    if (mode == Mode::Off) return false;
    g_setError = host.function(sym::physfs_set_error_code);
    g_getError = host.function(sym::physfs_get_last_error_code);
    g_getWriteDir = host.function(sym::physfs_get_write_dir);
    try {
        g_listings = new std::unordered_map<std::wstring, std::shared_ptr<const Listing>>();
        g_dir = new std::wstring(host.data_dir().wstring());
    } catch (...) {
        return false;
    }
    // Anvil verified that the table's stat slot points to the DIR archiver's stat.
    g_dirArchiver = reinterpret_cast<uintptr_t>(host.address(sym::dir_archiver_stat)) - kArchiverStat;
    g_verifyAll = mode == Mode::Verify;

    // The block is `call [r9+68h]` (stat(opaque = rcx, name = rdx, stat = r8),
    // r9 = the archiver's function table) and `mov ebp,eax`. The callback runs
    // instead and the game resumes after the block; the call's volatile
    // registers are free at that point, as after any call.
    host.replace_block(sym::verifypath_stat_call, [](anvil::Cpu& cpu) {
            const int rc = SB_VerifyPathStat(reinterpret_cast<void*>(cpu.rcx), reinterpret_cast<const char*>(cpu.rdx),
                                             reinterpret_cast<void*>(cpu.r8), reinterpret_cast<const void*>(cpu.r9));
            cpu.rax = static_cast<std::uint32_t>(rc);
            cpu.rbp = static_cast<std::uint32_t>(rc);  // mov ebp,eax zero-extends
        }).release();  // kept for the process
    InterlockedExchange(&EU4SB_VerifyPathStats.mode, 1);
    InterlockedExchange(&EU4SB_VerifyPathStats.initialized, 1);
    if (g_session) {
        HANDLE thread = CreateThread(nullptr, 0, WriterThread, nullptr, 0, nullptr);
        if (thread != nullptr) {
            SetThreadDescription(thread, L"bellows verifypath_listing writer");
            CloseHandle(thread);
        }
        Log(ANVIL_LOG_INFO, "verifypath_listing: also after the game is ready, listings at most 30 s old");
    }
    return true;
}

void ReportVerifyPathListing() {
    LogStats("verifypath_listing", EU4SB_VerifyPathStats);
    bench::SetCounters("verifypath_listing", EU4SB_VerifyPathStats);
}

}  // namespace sb
