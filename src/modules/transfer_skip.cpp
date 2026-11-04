// transfer_skip (on by default): "Continue" without the level 9 copy of the save that only a multiplayer client
// could receive (transfer_skip_core.hpp; lab docs: docs/autosave-cost.md, "Wczytanie przez „Continue”").
//
// The hook on CFileTransferManager::AddFile (0xD7AAD0) looks at the session of the manager:
//   - dummy, not a server (single player), compress asked: "on" calls the game's AddFile with compress = false,
//     as for a save that is compressed already. The entry has its Data and checksum and no CompressedData, and
//     the load goes on as before: it never read the copy.
//   - anything else (a server, a client, no session, a file that needs no compression): the game's call as it
//     came. Hosted multiplayer keeps the copy it sends (save_compress_parallel still applies to it).
//   - "verify": the game's call as it came, and the claims behind "on" are checked on what the game does: the copy
//     decodes to exactly Data (nothing else is in it), CGameSetup::LoadFile (0x110D760) then gets the entry's Data
//     with the flag 0 (the load does not use the copy), and CFileTransferManager::SendFile (0xD7B2A0) is not called
//     in a dummy session (nobody receives it). Each difference is a mismatch; the game's result stands.
// Every AddFile that was asked to compress appends a row to transfer_skip.tsv in the data directory.
#include "modules.hpp"
#include "transfer_skip_core.hpp"

#include <cstdio>
#include <mutex>
#include <string>

extern "C" {
// requests = AddFile calls asked to compress, hits = copies skipped, skips = calls left as they came (no single
// player session), verifications = the checks of "verify", mismatches = checks that differed, mode = 1 on / 2 verify.
__declspec(dllexport) sb::Stats EU4SB_TransferSkipStats = SB_STATS_INIT;
}

namespace {

using sb::transfer::Action;

anvil::HookOf<sym::file_transfer_add> g_addHook;
anvil::HookOf<sym::file_transfer_send> g_sendHook;
anvil::HookOf<sym::game_setup_load_file> g_loadHook;
decltype(g_addHook.next_pointer()) g_addNext = nullptr;
decltype(g_sendHook.next_pointer()) g_sendNext = nullptr;
decltype(g_loadHook.next_pointer()) g_loadNext = nullptr;
std::size_t g_sessionIsServer = 0, g_sessionIsDummy = 0;  // CSession offsets, from the game's own check
bool g_verify = false;
std::wstring* g_dir = nullptr;
std::mutex* g_fileLock = nullptr;

// "verify": the Data of the entry whose load is still to come. All of it on the main thread.
const void* g_expectedData = nullptr;

// CSession::_bIsServer and _bIsDummy, read as the game reads them.
struct SessionKind {
    bool have = false, server = false, dummy = false;
};

SessionKind ReadSession(void* manager) {
    SessionKind kind;
    const auto* session = static_cast<const std::uint8_t*>(sb::transfer::ManagerSession(manager));
    if (session == nullptr) return kind;
    kind.have = true;
    kind.server = session[g_sessionIsServer] != 0;
    kind.dummy = session[g_sessionIsDummy] != 0;
    return kind;
}

struct Row {
    const char* action = "kept";
    SessionKind kind;
    std::int64_t input = 0, copy = 0;
    std::int64_t addTicks = 0, checkTicks = 0;
    bool mismatch = false;
};

void AppendRow(const Row& row) {
    try {
        std::lock_guard lock(*g_fileLock);
        const std::wstring path = *g_dir + L"\\transfer_skip.tsv";
        const bool fresh = GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES;
        FILE* file = nullptr;
        if (_wfopen_s(&file, path.c_str(), L"ab") != 0 || file == nullptr) return;
        if (fresh) std::fputs("time\taction\tserver\tdummy\tinput_bytes\tcopy_bytes\tadd_ms\tcheck_ms\tmismatch\n", file);
        SYSTEMTIME t;
        GetLocalTime(&t);
        const double ms = sb::TicksToMs(1);
        std::fprintf(file, "%04u-%02u-%02u %02u:%02u:%02u.%03u\t%s\t%d\t%d\t%lld\t%lld\t%.3f\t%.3f\t%d\n", t.wYear, t.wMonth,
                     t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, row.action, row.kind.server ? 1 : 0,
                     row.kind.dummy ? 1 : 0, row.input, row.copy, row.addTicks * ms, row.checkTicks * ms, row.mismatch ? 1 : 0);
        std::fclose(file);
    } catch (...) {
    }
}

void Mismatch(Row* row) {
    InterlockedIncrement64(&EU4SB_TransferSkipStats.mismatches);
    if (row != nullptr) row->mismatch = true;
}

// "verify": the copy the game just built is the save and nothing else.
void CheckCopy(void* manager, int index, Row& row) {
    const std::int64_t start = sb::Ticks();
    const sb::transfer::FileDesc* file = sb::transfer::ManagerFile(manager, index);
    bool same = false;
    if (file != nullptr && file->data != nullptr && file->compressedData != nullptr) {
        row.input = static_cast<std::int64_t>(file->data->size());
        row.copy = static_cast<std::int64_t>(file->compressedData->size());
        sb::transfer::ZipMember member;
        same = sb::transfer::FindOnlyMember(file->compressedData->begin, file->compressedData->size(), member) &&
               sb::transfer::MemberIsData(member, file->data->begin, file->data->size());
        g_expectedData = file->data;
    }
    row.checkTicks = sb::Ticks() - start;
    InterlockedIncrement64(&EU4SB_TransferSkipStats.verifications);
    if (same) {
        sb::Log(ANVIL_LOG_INFO, "transfer_skip: the game's copy of %lld bytes decodes to the save (%lld bytes)", row.copy, row.input);
    } else {
        Mismatch(&row);
        sb::Log(ANVIL_LOG_WARNING, "transfer_skip: the game's transfer copy is not the save it was made from");
    }
}

int AddFileDetour(void* manager, const char* name, void** data, bool compress) {
    if (!compress) return g_addNext(manager, name, data, compress);
    InterlockedIncrement64(&EU4SB_TransferSkipStats.requests);
    const std::int64_t start = sb::Ticks();
    Row row;
    row.kind = ReadSession(manager);
    const auto* blob = data != nullptr ? static_cast<const sb::transfer::Blob*>(*data) : nullptr;
    if (blob != nullptr) row.input = static_cast<std::int64_t>(blob->size());
    const Action action = sb::transfer::Decide(compress, row.kind.have, row.kind.server, row.kind.dummy, g_verify);
    const int index = g_addNext(manager, name, data, action != Action::Skip);
    row.addTicks = sb::Ticks() - start;
    if (action == Action::Skip) {
        InterlockedIncrement64(&EU4SB_TransferSkipStats.hits);
        row.action = "skipped";
        sb::Log(ANVIL_LOG_INFO, "transfer_skip: no transfer copy of a save of %lld bytes (single player)", row.input);
    } else if (action == Action::Check) {
        row.action = "verify";
        CheckCopy(manager, index, row);
    } else {
        InterlockedIncrement64(&EU4SB_TransferSkipStats.skips);
        sb::Log(ANVIL_LOG_INFO, "transfer_skip: the game keeps its copy of a save of %lld bytes (server %d, dummy %d)", row.input,
                row.kind.server ? 1 : 0, row.kind.dummy ? 1 : 0);
    }
    AppendRow(row);
    return index;
}

std::uint64_t LoadFileDetour(void* handler, const char* name, void* data, bool compressed) {
    if (g_expectedData != nullptr) {
        InterlockedIncrement64(&EU4SB_TransferSkipStats.verifications);
        if (data != g_expectedData || compressed) {
            Mismatch(nullptr);
            sb::Log(ANVIL_LOG_WARNING, "transfer_skip: the load did not get the entry's Data with the flag 0 (flag %d)",
                    compressed ? 1 : 0);
        } else {
            sb::Log(ANVIL_LOG_INFO, "transfer_skip: the load got the entry's Data with the flag 0");
        }
        g_expectedData = nullptr;
    }
    return g_loadNext(handler, name, data, compressed);
}

std::uint64_t SendFileDetour(void* manager, std::uint32_t index, std::uint32_t peer) {
    const SessionKind kind = ReadSession(manager);
    if (kind.have && kind.dummy) {
        InterlockedIncrement64(&EU4SB_TransferSkipStats.verifications);
        Mismatch(nullptr);
        sb::Log(ANVIL_LOG_WARNING, "transfer_skip: a dummy session sent file %u to peer %u", index, peer);
    }
    return g_sendNext(manager, index, peer);
}

}  // namespace

namespace sb {

bool InstallTransferSkip(anvil::Host& host, Mode mode) {
    if (mode == Mode::Off) return false;
    g_verify = mode == Mode::Verify;
    try {
        g_dir = new std::wstring(host.data_dir().wstring());
        g_fileLock = new std::mutex;
    } catch (...) {
        return false;
    }
    g_sessionIsServer = static_cast<std::size_t>(host.value(sym::session_kind_check, "session_is_server"));
    g_sessionIsDummy = static_cast<std::size_t>(host.value(sym::session_kind_check, "session_is_dummy"));
    host.hook(g_addHook, sym::file_transfer_add, &AddFileDetour);
    g_addNext = g_addHook.next_pointer();
    if (g_verify) {
        host.hook(g_sendHook, sym::file_transfer_send, &SendFileDetour);
        g_sendNext = g_sendHook.next_pointer();
        host.hook(g_loadHook, sym::game_setup_load_file, &LoadFileDetour);
        g_loadNext = g_loadHook.next_pointer();
    }
    InterlockedExchange(&EU4SB_TransferSkipStats.mode, g_verify ? 2 : 1);
    InterlockedExchange(&EU4SB_TransferSkipStats.initialized, 1);
    Log(ANVIL_LOG_INFO, "transfer_skip: %s", g_verify ? "verifying, the game keeps its copy" : "on");
    return true;
}

void ReportTransferSkip() {
    // No benchmark counters: the report is at its limit of 128 fields; the counters are in the log.
    LogStats("transfer_skip", EU4SB_TransferSkipStats);
}

}  // namespace sb
