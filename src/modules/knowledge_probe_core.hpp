// knowledge_probe: hashing a country's containers and recording what one call changed (pure logic,
// tested in tests/test_knowledge_probe.cpp).
#pragma once

#include "knowledge_probe_layout.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sb::knowledge {

constexpr std::size_t kMaxContainerBytes = std::size_t{64} << 20;  // larger means a torn or foreign object
constexpr std::int64_t kMaxListNodes = 1 << 20;
constexpr std::size_t kWords = kCountrySize / 8;
constexpr std::size_t kContainerCount = sizeof kContainers / sizeof kContainers[0];

inline std::uint64_t Mix(std::uint64_t h, std::uint64_t w) noexcept {
    h ^= w;
    h *= 0x9E3779B97F4A7C15ULL;
    return h ^ (h >> 29);
}

inline std::uint64_t HashBytes(const unsigned char* p, std::size_t n, std::uint64_t h) noexcept {
    std::size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        std::uint64_t w;
        std::memcpy(&w, p + i, 8);
        h = Mix(h, w);
    }
    std::uint64_t tail = 0;
    std::memcpy(&tail, p + i, n - i);
    return Mix(Mix(h, tail), n);
}

template <typename T> T Load(const unsigned char* p, std::size_t at) noexcept {
    T v;
    std::memcpy(&v, p + at, sizeof v);
    return v;
}

// An owning pointer: the bytes of the object it owns (a rebuilt list owns new objects with the same bytes).
inline std::uint64_t HashOwned(const unsigned char* object, std::size_t size, std::uint64_t h) noexcept {
    return object != nullptr ? HashBytes(object, size, h) : Mix(h, 0);
}

// Hash of the elements of one container of the object at obj (not of its pointers, so a buffer the game
// moves keeps its hash). False when the container's fields do not look like a container.
inline bool HashContainer(const unsigned char* obj, const Container& c, std::uint64_t& out) noexcept {
    const unsigned char* at = obj + c.offset;
    std::uint64_t h = 0xCBF29CE484222325ULL ^ c.offset;
    switch (c.shape) {
    case kVector:
    case kBitset: {
        const auto* begin = Load<const unsigned char*>(at, 0);
        const auto* end = Load<const unsigned char*>(at, 8);
        if (end < begin || static_cast<std::size_t>(end - begin) > kMaxContainerBytes || (begin == nullptr) != (end == nullptr)) return false;
        h = begin != nullptr ? HashBytes(begin, static_cast<std::size_t>(end - begin), h) : Mix(h, 0);
        if (c.shape == kBitset) h = Mix(h, Load<std::uint64_t>(at, 24));
        break;
    }
    case kPdxArray: {
        const auto* data = Load<const unsigned char*>(at, 8);
        const auto size = Load<std::int32_t>(at, 20);
        if (size < 0 || static_cast<std::size_t>(size) * c.a > kMaxContainerBytes || (data == nullptr && size != 0)) return false;
        if (c.owned != 0) {
            for (std::int32_t i = 0; i < size; ++i) h = HashOwned(Load<const unsigned char*>(data, static_cast<std::size_t>(i) * 8), c.owned, h);
            h = Mix(h, static_cast<std::uint64_t>(size));
        } else {
            h = size != 0 ? HashBytes(data, static_cast<std::size_t>(size) * c.a, h) : Mix(h, 0);
        }
        break;
    }
    case kList: {
        const auto size = Load<std::int32_t>(at, 16);
        if (size < 0 || size > kMaxListNodes) return false;
        const unsigned char* node = Load<const unsigned char*>(at, 0);
        std::int32_t n = 0;
        for (; node != nullptr && n <= size; ++n) {
            h = c.owned != 0 ? HashOwned(Load<const unsigned char*>(node, c.b), c.owned, h) : HashBytes(node + c.b, c.c, h);
            node = Load<const unsigned char*>(node, c.a);
        }
        if (n != size) return false;
        h = Mix(h, static_cast<std::uint64_t>(size));
        break;
    }
    default:
        return false;
    }
    out = h;
    return true;
}

// What one call changed: words of the object (offset / 8) and containers (index in kContainers), the first
// kKept of each, and a key over all of them.
struct Change {
    static constexpr int kKept = 12;
    int words = 0, containers = 0;
    std::uint32_t word[kKept]{};
    std::uint16_t container[kKept]{};
    std::uint64_t key = 0x84222325CBF29CE4ULL;

    void AddWord(std::uint32_t w) noexcept {
        if (words < kKept) word[words] = w;
        ++words;
        key = Mix(key, w);
    }
    void AddContainer(std::uint16_t i) noexcept {
        if (containers < kKept) container[containers] = i;
        ++containers;
        key = Mix(key, 0x10000ULL + i);
    }
    bool None() const noexcept { return words == 0 && containers == 0; }
};

// The words of the object that hold a container's buffer pointers and capacity. A refresh that rebuilds a
// list or a vector with the same elements gets new nodes or a new buffer; the container's hash covers its
// elements, so these words are left out of the word diff.
struct HeaderMask {
    bool word[kWords] = {};
    HeaderMask() noexcept {
        for (const Container& c : kContainers) {
            const std::size_t w = c.offset / 8;
            switch (c.shape) {
            case kVector:
            case kBitset:  // begin, end, capacity end
                Set(w), Set(w + 1), Set(w + 2);
                break;
            case kPdxArray:  // data, then capacity and count (the count is in the hash)
                Set(w + 1), Set(w + 2);
                break;
            case kList:  // head, tail (the count is in the hash)
                Set(w), Set(w + 1);
                break;
            }
        }
    }
    void Set(std::size_t w) noexcept {
        if (w < kWords) word[w] = true;
    }
};

// Compares the object's bytes before (copy) and after (live) a call, word by word, without the masked
// words; masked is set when one of those changed.
inline void DiffWords(const unsigned char* before, const unsigned char* after, const HeaderMask& mask, Change& change,
                      bool& masked) noexcept {
    masked = false;
    for (std::size_t i = 0; i < kWords; ++i) {
        if (std::memcmp(before + i * 8, after + i * 8, 8) == 0) continue;
        if (mask.word[i]) {
            masked = true;
        } else {
            change.AddWord(static_cast<std::uint32_t>(i));
        }
    }
}

}  // namespace sb::knowledge
