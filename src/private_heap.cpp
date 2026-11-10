// Lab experiment (not shipped): bellows.dll's own C++ allocations on a private heap instead of the process heap.
// The lab's start-up profile once showed the main thread waiting on PhysFS's state section while the thread
// holding it waited on the process heap lock (lab docs/startup-phase-profile.md, "Łańcuch blokad przed
// aplikacją"). With EU4LAB_PRIVATE_HEAP=1 every operator new/delete of this DLL uses a heap of its own
// (HeapCreate, serialized by its own lock); otherwise the process heap, as the static CRT does. One binary for
// both arms of a series.
#include <windows.h>

#include <cstdlib>
#include <new>

namespace {

HANDLE Heap() noexcept {
    static const HANDLE heap = [] {
        wchar_t value[8] = {};
        const DWORD n = GetEnvironmentVariableW(L"EU4LAB_PRIVATE_HEAP", value, 8);
        if (n == 1 && value[0] == L'1') {
            if (HANDLE h = HeapCreate(0, 0, 0)) return h;
        }
        return GetProcessHeap();
    }();
    return heap;
}

void* Allocate(std::size_t size) {
    void* p = HeapAlloc(Heap(), 0, size == 0 ? 1 : size);
    if (p == nullptr) throw std::bad_alloc();
    return p;
}

void Release(void* p) noexcept {
    if (p != nullptr) HeapFree(Heap(), 0, p);
}

}  // namespace

extern "C" int SB_PrivateHeapActive() noexcept { return Heap() != GetProcessHeap() ? 1 : 0; }

void* operator new(std::size_t size) { return Allocate(size); }
void* operator new[](std::size_t size) { return Allocate(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    return HeapAlloc(Heap(), 0, size == 0 ? 1 : size);
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    return HeapAlloc(Heap(), 0, size == 0 ? 1 : size);
}
void operator delete(void* p) noexcept { Release(p); }
void operator delete[](void* p) noexcept { Release(p); }
void operator delete(void* p, std::size_t) noexcept { Release(p); }
void operator delete[](void* p, std::size_t) noexcept { Release(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { Release(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { Release(p); }
