// checksum_defer: the task waits for the end of the database load in "on", not at all in "verify", and no longer
// than the timeout. The effect on start-up is a lab measurement, not this test.
#include "modules/checksum_defer_core.hpp"

#include <cassert>
#include <cstdio>
#include <thread>

using sb::checksum_defer::Wait;
using sb::checksum_defer::Waited;

namespace {

ULONGLONG Now() { return GetTickCount64(); }

}  // namespace

int main() {
    const HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    assert(done != nullptr);
    // "verify": no wait, even though the databases are not loaded.
    ULONGLONG t = Now();
    assert(Wait(false, done, 5000) == Waited::No);
    assert(Now() - t < 1000);

    // "on", the databases never end: the task runs anyway after the timeout.
    t = Now();
    assert(Wait(true, done, 100) == Waited::Timeout);
    assert(Now() - t >= 90);

    // "on", the database load ends on another thread while the task waits.
    std::thread loader([done] {
        Sleep(150);
        SetEvent(done);
    });
    t = Now();
    assert(Wait(true, done, 10000) == Waited::Databases);
    const ULONGLONG waited = Now() - t;
    loader.join();
    assert(waited >= 100 && waited < 5000);

    // "on", the databases were loaded before the task started: no wait.
    t = Now();
    assert(Wait(true, done, 10000) == Waited::Databases);
    assert(Now() - t < 1000);

    // A failed wait (no event) counts as a timeout: the task runs anyway.
    assert(Wait(true, nullptr, 10000) == Waited::Timeout);

    CloseHandle(done);
    std::puts("checksum_defer: ok");
    return 0;
}
