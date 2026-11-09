#pragma once

// checksum_defer: the wait of the checksum task, without the game (lab docs: docs/checksum-defer-prototype.md, lab #66).
//
// Start-up queues the game's checksum task on the long-task pool just before the databases load, and the two then
// compete for the file system locks. In "on" the task's body first waits for an event that the end of the database
// load sets, at most kWaitMs, and then runs as the game wrote it. In "verify" it runs at once, as in the game.

#include <windows.h>

namespace sb::checksum_defer {

inline constexpr DWORD kWaitMs = 120000;  // the task runs anyway after this long

enum class Waited {
    No,         // "verify": the task ran where the game runs it
    Databases,  // the databases were loaded before the task ran
    Timeout,    // no end of the database load in time (or the wait failed): the task ran anyway
};

// The task's wait before its body runs: none in "verify"; in "on" until the end of the database load sets
// databasesDone, at most timeoutMs. The body runs after it in every case.
inline Waited Wait(bool defer, HANDLE databasesDone, DWORD timeoutMs) noexcept {
    if (!defer) return Waited::No;
    return WaitForSingleObject(databasesDone, timeoutMs) == WAIT_OBJECT_0 ? Waited::Databases : Waited::Timeout;
}

}  // namespace sb::checksum_defer
