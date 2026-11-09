// Trigger profile lookup at the end of loading (ported from the lab's
// startup_trigger_intern.inc; docs in the lab: docs/startup-trigger-intern.md).
//
// Every trigger node created while loading registers itself in a registry.
// Near the end of loading the game walks both registry lists and sets
// node+0x58 = lookup(singleton, node). The original deep-copies the node's whole
// subtree into a key (id = node+0x50, children as a deduplicated set), hashes
// it, looks it up in the singleton's map and returns the entry's value or 0.
// The map is the trigger profile the game reads from trigger_profile.txt at
// start-up: a trigger shape and a number per line. The value is that number,
// not a shared node, so this is a lookup and not interning (the module kept
// its old name). Without the file (the lab's early copies) the map is empty,
// and 2.35 M calls each build a key only to find nothing.
//
// Both call sites are redirected (Anvil redirect_call on the declared call
// sites; the instructions around each call are declared too) to
// SB_TriggerIntern / SB_TriggerInternB: an empty map answers 0 immediately.
// Otherwise the map keys and their substructures get canonical ids by
// hash-consing (id + sorted unique child ids) in a flat table built with the
// index. A node's shape is only looked up there (memoized per inner node
// pointer): a shape outside the table, or a node with such a child, is
// "foreign" and answers 0, since a key consists solely of key substructures and
// so cannot equal it.
// "verify" = compute both, return the original's value, count mismatches;
// "on" = return the index answer, compare every 256th call and disable on a
// mismatch. A changed map size or registry, an unexpected shape, the game
// being ready or any exception send the call to the original.
#include "modules.hpp"

#include <algorithm>
#include <vector>

namespace {

constexpr std::size_t kMaxDepth = 4096;

// Game layouts (eu4.exe 1.37.5).
constexpr std::size_t kNodeChildren = 0x08;  // node**: child array
constexpr std::size_t kNodeCount = 0x14;     // int: number of children
constexpr std::size_t kNodeId = 0x50;        // int
constexpr std::size_t kMapListHead = 0x50;   // singleton + 0x48 map, +0x50 list sentinel
constexpr std::size_t kMapSize = 0x58;       // size_t
constexpr std::size_t kMapNodeKey = 0x10;    // key embedded in a map node
constexpr std::size_t kMapNodeValue = 0x58;  // value of a map entry
constexpr std::size_t kKeyId = 0x00;         // int
constexpr std::size_t kKeySetHead = 0x10;    // key + 8 set, +0x10 list sentinel
constexpr std::size_t kSetNodeValue = 0x10;  // unique_ptr<key> in a set node
// Registry: two unordered_set<node*> of 0x40 bytes at +0x00 and +0x40 (list
// head +0x08, size +0x10 each); the sizes only presize the memo.
constexpr std::size_t kRegistrySizes[2] = {0x10, 0x50};
constexpr std::uint64_t kMaxPresize = 1ULL << 26;
constexpr std::size_t kDefaultPresize = 1u << 16;

constexpr std::uint32_t kForeign = 0xFFFFFFFFu;  // not a key structure
constexpr std::uint32_t kEmpty = 0xFFFFFFFEu;    // free slot in the structure table

using InternFn = std::uint64_t (*)(void*, void*);

struct ShapeError {};

// Power of two with a load factor of at most 0.7.
std::size_t Capacity(std::size_t expected) {
    std::size_t cap = 16;
    while (cap / 10 * 7 < expected) cap <<= 1;
    return cap;
}

// Key structures: shape = (id, sorted unique child ids) stored in an arena as
// [length, id, c0, c1, ...]; open addressing over structure indices.
struct Shapes {
    std::vector<std::uint32_t> arena;
    std::vector<std::uint32_t> offset;  // structure -> arena offset
    std::vector<std::uint32_t> table;   // structure index or kEmpty
    std::size_t mask = 0;

    static std::uint64_t Hash(const std::uint32_t* p, std::size_t n) noexcept {
        std::uint64_t h = 0x9E3779B97F4A7C15ULL ^ n;
        for (std::size_t i = 0; i < n; ++i) {
            h ^= p[i];
            h *= 0xff51afd7ed558ccdULL;
            h ^= h >> 32;
        }
        return h;
    }
    std::uint32_t Count() const noexcept { return static_cast<std::uint32_t>(offset.size()); }
    bool Equal(std::uint32_t s, const std::uint32_t* p, std::size_t n) const noexcept {
        const std::uint32_t* q = arena.data() + offset[s];
        return q[0] == n && std::equal(p, p + n, q + 1);
    }
    std::uint32_t Find(const std::uint32_t* p, std::size_t n) const noexcept {
        if (table.empty()) return kForeign;
        for (std::size_t i = Hash(p, n) & mask;; i = (i + 1) & mask) {
            const std::uint32_t s = table[i];
            if (s == kEmpty) return kForeign;
            if (Equal(s, p, n)) return s;
        }
    }
    std::uint32_t Insert(const std::uint32_t* p, std::size_t n) {
        if (Capacity(offset.size() + 1) > table.size()) Rehash(Capacity(offset.size() + 1));
        for (std::size_t i = Hash(p, n) & mask;; i = (i + 1) & mask) {
            const std::uint32_t s = table[i];
            if (s == kEmpty) {
                const std::uint32_t index = Count();
                if (index >= kEmpty) throw ShapeError{};
                offset.push_back(static_cast<std::uint32_t>(arena.size()));
                arena.push_back(static_cast<std::uint32_t>(n));
                arena.insert(arena.end(), p, p + n);
                table[i] = index;
                return index;
            }
            if (Equal(s, p, n)) return s;
        }
    }
    void Rehash(std::size_t cap) {
        table.assign(cap, kEmpty);
        mask = cap - 1;
        for (std::uint32_t s = 0; s < Count(); ++s) {
            const std::uint32_t* q = arena.data() + offset[s];
            std::size_t i = Hash(q + 1, q[0]) & mask;
            while (table[i] != kEmpty) i = (i + 1) & mask;
            table[i] = s;
        }
    }
};

// Trigger node -> structure index or kForeign, open addressing on the pointer.
struct Memo {
    struct Slot {
        const void* node;
        std::uint32_t id;
    };
    std::vector<Slot> slots;
    std::size_t mask = 0;
    std::size_t used = 0;

    static std::size_t Hash(const void* p) noexcept {
        auto x = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(p));
        x ^= x >> 33;
        x *= 0xff51afd7ed558ccdULL;
        x ^= x >> 33;
        return static_cast<std::size_t>(x);
    }
    void Init(std::size_t expected) {
        slots.assign(Capacity(expected), Slot{nullptr, 0});
        mask = slots.size() - 1;
        used = 0;
    }
    bool Get(const void* node, std::uint32_t& id) const noexcept {
        if (slots.empty()) return false;
        for (std::size_t i = Hash(node) & mask;; i = (i + 1) & mask) {
            if (slots[i].node == node) {
                id = slots[i].id;
                return true;
            }
            if (slots[i].node == nullptr) return false;
        }
    }
    // node is not in the table yet (Get missed and nothing was inserted since).
    void Put(const void* node, std::uint32_t id) {
        if (Capacity(used + 1) > slots.size()) Grow();  // the presize was too small
        std::size_t i = Hash(node) & mask;
        while (slots[i].node != nullptr) i = (i + 1) & mask;
        slots[i] = Slot{node, id};
        ++used;
    }
    void Grow() {
        std::vector<Slot> old;
        old.swap(slots);
        slots.assign(Capacity(used + 1) * 2, Slot{nullptr, 0});
        mask = slots.size() - 1;
        for (const Slot& slot : old) {
            if (slot.node == nullptr) continue;
            std::size_t i = Hash(slot.node) & mask;
            while (slots[i].node != nullptr) i = (i + 1) & mask;
            slots[i] = slot;
        }
    }
};

struct State {
    Shapes shapes;
    Memo memo;
    // Frames of (id, child ids...) during a walk; a heap stack instead of a
    // buffer per call keeps recursion frames small for deep trees.
    std::vector<std::uint32_t> scratch;
    std::vector<std::uint64_t> values;  // structure -> map value
    std::vector<std::uint8_t> isKey;    // structure -> 1 when it is a whole map key
    std::size_t keyCount = 0;
    const void* singleton = nullptr;
    std::uint64_t mapSize = 0;
    std::uintptr_t registry = 0;
    bool usable = false;
};

// The rest of each call site's chain (the game's lookup unless another plugin
// redirected the site too).
InternFn g_original = nullptr;   // first site
InternFn g_originalB = nullptr;  // second site
anvil::FunctionHook<std::uint64_t(void*, void*)> g_siteA, g_siteB;
volatile const std::uintptr_t* g_registrySlot = nullptr;
bool g_verifyOnly = false;
SRWLOCK g_lock = SRWLOCK_INIT;
State* g_state = nullptr;
std::uint64_t g_answerSequence = 0;  // answered calls, guarded by g_lock
volatile LONG64 g_ticksOriginal = 0;  // calls to the original only

// Hook timing: the span between the first and the last call's entry. The game
// makes all calls in two back-to-back loops, so the span is the hook time plus
// the game's loop overhead (a getter call and a store per node), minus the
// last calls. Timing each call instead (two QPC reads and a locked add on each
// of 2.35 M calls) cost ~0.2 s offline and inflated the result, since a timed
// call cannot overlap its cache misses with its neighbours'. The entry time is
// taken every 16th call, so the span misses at most the last 16 calls.
constexpr LONG64 kTimingSample = 16;
volatile LONG64 g_calls = 0;
volatile LONG64 g_firstCall = 0;
volatile LONG64 g_lastCall = 0;

// Hot-path counters use plain (non-locked) updates: the game looks up from one
// thread under its own mutex, and a locked instruction on every call waits for
// the walk's outstanding cache misses. Concurrent calls could lose a count,
// never change an answer.
inline void Bump(volatile LONG64* value) noexcept { *value = *value + 1; }

void NoteCall() noexcept {
    const LONG64 call = g_calls;
    g_calls = call + 1;
    if (call % kTimingSample != 0) return;
    const LONG64 now = sb::Ticks();
    if (call == 0) g_firstCall = now;
    g_lastCall = now;
}

// QPC ticks from the first to the last timed call entry.
LONG64 CallSpan() noexcept { return g_lastCall - g_firstCall; }

std::uint64_t CallOriginal(InternFn original, void* singleton, void* node) {
    sb::ScopedTicks t(&g_ticksOriginal);
    return original(singleton, node);
}

}  // namespace

extern "C" {
// requests = calls, hits = non-null answers, misses = null answers, skips =
// passed to the original, duplicateKeys = index builds, verifications /
// mismatches = comparisons with the original, mode = 1 on / 2 verify,
// cacheDisabled = disabled after a mismatch or a shape error.
__declspec(dllexport) sb::Stats EU4SB_TriggerInternStats = SB_STATS_INIT;
// Last index build: requests = size of the singleton's key map, hits = distinct key structures.
__declspec(dllexport) sb::Stats EU4SB_TriggerInternMap = SB_STATS_INIT;
}

namespace {

using sb::Read;

// Canonical id of the frame (id, children...) at scratch[base..], which is popped.
template <bool Insert>
std::uint32_t Canonical(State& state, std::size_t base) {
    std::uint32_t* const p = state.scratch.data() + base;
    std::uint32_t* const end = state.scratch.data() + state.scratch.size();
    std::sort(p + 1, end);
    const auto n = static_cast<std::size_t>(std::unique(p + 1, end) - p);
    const std::uint32_t id = Insert ? state.shapes.Insert(p, n) : state.shapes.Find(p, n);
    state.scratch.resize(base);
    return id;
}

// Canonical id of a key stored in the map (key -> set of unique_ptr<key>).
std::uint32_t KeyId(State& state, const void* key, std::size_t depth) {
    if (key == nullptr || depth > kMaxDepth) throw ShapeError{};
    const void* head = Read<const void*>(key, kKeySetHead);
    if (head == nullptr) throw ShapeError{};
    const std::size_t base = state.scratch.size();
    state.scratch.push_back(static_cast<std::uint32_t>(Read<std::int32_t>(key, kKeyId)));
    for (const void* entry = Read<const void*>(head, 0); entry != head; entry = Read<const void*>(entry, 0)) {
        if (entry == nullptr) throw ShapeError{};
        const std::uint32_t child = KeyId(state, Read<const void*>(entry, kSetNodeValue), depth + 1);
        state.scratch.push_back(child);
    }
    return Canonical<true>(state, base);
}

// Canonical id of a trigger node (kForeign outside the key structures), memoized by node pointer.
std::uint32_t NodeId(State& state, const void* node, std::size_t depth) {
    if (node == nullptr || depth > kMaxDepth) throw ShapeError{};
    const auto* const* array = Read<const void* const*>(node, kNodeChildren);
    const std::int32_t count = Read<std::int32_t>(node, kNodeCount);
    if (count < 0 || (count > 0 && array == nullptr)) throw ShapeError{};
    // Leaves (most nodes) skip the memo: one lookup in the small structure
    // table is cheaper than a probe in the large pointer table.
    if (count == 0) {
        const auto leaf = static_cast<std::uint32_t>(Read<std::int32_t>(node, kNodeId));
        return state.shapes.Find(&leaf, 1);
    }
    std::uint32_t id = 0;
    if (state.memo.Get(node, id)) return id;
    const std::size_t base = state.scratch.size();
    state.scratch.push_back(static_cast<std::uint32_t>(Read<std::int32_t>(node, kNodeId)));
    bool foreign = false;
    for (std::int32_t index = 0; index < count; ++index) {
        // A foreign child makes the node foreign, but every child is still visited and memoized.
        const std::uint32_t child = NodeId(state, array[index], depth + 1);
        if (child == kForeign) {
            foreign = true;
        } else if (!foreign) {
            state.scratch.push_back(child);
        }
    }
    if (foreign) {
        state.scratch.resize(base);
        id = kForeign;
    } else {
        id = Canonical<false>(state, base);
    }
    state.memo.Put(node, id);
    return id;
}

// Number of registered nodes, a presize hint for the memo.
std::size_t RegisteredNodes(std::uintptr_t registry) {
    if (registry == 0) return kDefaultPresize;
    std::uint64_t total = 0;
    for (std::size_t offset : kRegistrySizes) {
        total += std::min(Read<std::uint64_t>(reinterpret_cast<const void*>(registry), offset), kMaxPresize);
    }
    return static_cast<std::size_t>(std::min(total, kMaxPresize));
}

// (Re)build the index when the singleton, the map size or the registry changed.
State& Prepare(void* singleton) {
    const std::uint64_t mapSize = Read<std::uint64_t>(singleton, kMapSize);
    const std::uintptr_t registry = g_registrySlot ? *g_registrySlot : 0;
    State& state = *g_state;
    if (state.usable && state.singleton == singleton && state.mapSize == mapSize && state.registry == registry) {
        return state;
    }
    state = State{};
    state.singleton = singleton;
    state.mapSize = mapSize;
    state.registry = registry;
    const void* head = Read<const void*>(singleton, kMapListHead);
    if (head == nullptr) throw ShapeError{};
    std::uint64_t seen = 0;
    for (const void* entry = Read<const void*>(head, 0); entry != head; entry = Read<const void*>(entry, 0)) {
        if (entry == nullptr || ++seen > mapSize) throw ShapeError{};
        const std::uint32_t id = KeyId(state, static_cast<const unsigned char*>(entry) + kMapNodeKey, 0);
        if (id >= state.isKey.size()) {
            state.values.resize(state.shapes.Count(), 0);
            state.isKey.resize(state.shapes.Count(), 0);
        }
        // Keys in the game's map are unique; a duplicate structure would mean a model error.
        if (state.isKey[id] != 0) throw ShapeError{};
        state.isKey[id] = 1;
        state.values[id] = Read<std::uint64_t>(entry, kMapNodeValue);
        ++state.keyCount;
    }
    if (seen != mapSize) throw ShapeError{};
    // Node ids are only needed when a key can match.
    if (state.keyCount != 0) state.memo.Init(RegisteredNodes(registry));
    sb::Increment(&EU4SB_TriggerInternStats.duplicateKeys);  // number of index builds
    InterlockedExchange64(&EU4SB_TriggerInternMap.requests, static_cast<LONG64>(mapSize));
    InterlockedExchange64(&EU4SB_TriggerInternMap.hits, static_cast<LONG64>(state.keyCount));
    state.usable = true;
    return state;
}

bool Disabled() noexcept {
    // Plain volatile reads: aligned 32-bit loads are atomic on x64, and a locked
    // read on each of the 2.35 M calls costs more than the lookup itself.
    if (EU4SB_TriggerInternStats.cacheDisabled != 0) return true;
    if (EU4SB_TriggerInternStats.readyDisabled != 0) return true;
    if (sb::ReadyReached()) {
        InterlockedExchange(&EU4SB_TriggerInternStats.readyDisabled, 1);
        return true;
    }
    return false;
}

}  // namespace

namespace {

std::uint64_t Intern(InternFn original, void* singleton, void* node) {
    NoteCall();
    Bump(&EU4SB_TriggerInternStats.requests);
    if (Disabled() || g_state == nullptr || singleton == nullptr) {
        sb::Increment(&EU4SB_TriggerInternStats.skips);
        return CallOriginal(original, singleton, node);
    }
    std::uint64_t answer = 0;
    std::uint64_t sequence = 0;
    try {
        AcquireSRWLockExclusive(&g_lock);
        // The index may have been released at the ready event while this call waited.
        if (EU4SB_TriggerInternStats.readyDisabled != 0) {
            ReleaseSRWLockExclusive(&g_lock);
            sb::Increment(&EU4SB_TriggerInternStats.skips);
            return CallOriginal(original, singleton, node);
        }
        try {
            State& state = Prepare(singleton);
            // An empty map cannot contain the key: the original returns 0 after building it.
            if (state.keyCount != 0) {
                const std::uint32_t id = NodeId(state, node, 0);
                if (id != kForeign && id < state.isKey.size() && state.isKey[id] != 0) answer = state.values[id];
            }
            sequence = ++g_answerSequence;  // under the lock: no interlocked operation needed
        } catch (...) {
            g_state->usable = false;
            ReleaseSRWLockExclusive(&g_lock);
            throw;
        }
        ReleaseSRWLockExclusive(&g_lock);
    } catch (...) {
        InterlockedExchange(&EU4SB_TriggerInternStats.cacheDisabled, 1);
        sb::Increment(&EU4SB_TriggerInternStats.skips);
        return CallOriginal(original, singleton, node);
    }
    if (g_verifyOnly || (sequence & 255) == 1) {
        sb::Increment(&EU4SB_TriggerInternStats.verifications);
        const std::uint64_t actual = CallOriginal(original, singleton, node);
        if (actual != answer) {
            sb::Increment(&EU4SB_TriggerInternStats.mismatches);
            if (!g_verifyOnly) InterlockedExchange(&EU4SB_TriggerInternStats.cacheDisabled, 1);
            return actual;
        }
        if (g_verifyOnly) return actual;
    }
    Bump(answer ? &EU4SB_TriggerInternStats.hits : &EU4SB_TriggerInternStats.misses);
    return answer;
}

}  // namespace

extern "C" std::uint64_t SB_TriggerIntern(void* singleton, void* node) { return Intern(g_original, singleton, node); }
extern "C" std::uint64_t SB_TriggerInternB(void* singleton, void* node) { return Intern(g_originalB, singleton, node); }

namespace sb {

bool InstallTriggerIntern(anvil::Host& host, Mode mode) {
    if (mode == Mode::Off) return false;
    // Each site (declared with the code around it): "mov rdx,rdi; mov rcx,rax;
    // call <lookup>; mov [rdi+58h],rax".
    try {
        g_state = new State();
    } catch (...) {
        return false;
    }
    g_registrySlot = host.data(sym::trigger_registry);
    g_verifyOnly = mode == Mode::Verify;
    Undo undo;
    host.redirect(g_siteA, sym::trigger_intern_call_a, &SB_TriggerIntern);
    undo.add(g_siteA);
    host.redirect(g_siteB, sym::trigger_intern_call_b, &SB_TriggerInternB);
    undo.add(g_siteB);
    g_original = g_siteA.next_pointer();
    g_originalB = g_siteB.next_pointer();
    undo.keep();
    InterlockedExchange(&EU4SB_TriggerInternStats.mode, g_verifyOnly ? 2 : 1);
    InterlockedExchange(&EU4SB_TriggerInternStats.initialized, 1);
    return true;
}

void ReportTriggerIntern() {
    LogStats("trigger_intern", EU4SB_TriggerInternStats);
    Log(ANVIL_LOG_INFO, "trigger_intern: map size %lld, indexed %lld; first to last call %.1f ms (game loop included), "
        "of which original %.1f ms",
        EU4SB_TriggerInternMap.requests, EU4SB_TriggerInternMap.hits, TicksToMs(CallSpan()), TicksToMs(g_ticksOriginal));
    bench::SetCounters("trigger_intern", EU4SB_TriggerInternStats);
    if (g_state == nullptr) return;
    bench::Get().set("trigger_intern.map_size", EU4SB_TriggerInternMap.requests);
    bench::Get().set("trigger_intern.indexed", EU4SB_TriggerInternMap.hits);
    // The game is ready, so the index is no longer used: release its memory
    // (tens of MB with a non-empty map). Later calls go to the original.
    InterlockedExchange(&EU4SB_TriggerInternStats.readyDisabled, 1);
    AcquireSRWLockExclusive(&g_lock);
    *g_state = State{};
    ReleaseSRWLockExclusive(&g_lock);
}

}  // namespace sb
