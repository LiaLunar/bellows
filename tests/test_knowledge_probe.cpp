// knowledge_probe core: container hashes follow the elements and not the buffers, a torn container is
// refused, and the word diff finds what changed.
#include "../src/modules/knowledge_probe_core.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            std::exit(1);                                                    \
        }                                                                    \
    } while (0)

namespace kn = sb::knowledge;

template <typename T> void Put(unsigned char* p, std::size_t at, T v) { std::memcpy(p + at, &v, sizeof v); }

struct Node {
    std::uint64_t value;
    Node* next;
};

int main() {
    std::vector<unsigned char> obj(256, 0);
    // A vector at +0: two copies of the same elements in other buffers hash the same.
    std::vector<int> a{1, 2, 3}, b{1, 2, 3}, c{1, 2, 4};
    const kn::Container vec{0, kn::kVector, 0, 0, 0};
    std::uint64_t ha = 0, hb = 0, hc = 0;
    Put(obj.data(), 0, a.data());
    Put(obj.data(), 8, a.data() + a.size());
    CHECK(kn::HashContainer(obj.data(), vec, ha));
    Put(obj.data(), 0, b.data());
    Put(obj.data(), 8, b.data() + b.size());
    CHECK(kn::HashContainer(obj.data(), vec, hb));
    Put(obj.data(), 0, c.data());
    Put(obj.data(), 8, c.data() + c.size());
    CHECK(kn::HashContainer(obj.data(), vec, hc));
    CHECK(ha == hb && ha != hc);
    // End before begin: not a container.
    Put(obj.data(), 0, c.data() + 2);
    Put(obj.data(), 8, c.data());
    CHECK(!kn::HashContainer(obj.data(), vec, hc));
    // An empty vector.
    Put<const void*>(obj.data(), 0, nullptr);
    Put<const void*>(obj.data(), 8, nullptr);
    CHECK(kn::HashContainer(obj.data(), vec, hc));

    // A pdx array at +32: data +8, count +20, element size 4.
    const kn::Container pdx{32, kn::kPdxArray, 4, 0, 0};
    Put(obj.data(), 32 + 8, a.data());
    Put<std::int32_t>(obj.data(), 32 + 20, 3);
    std::uint64_t hp = 0, hp2 = 0;
    CHECK(kn::HashContainer(obj.data(), pdx, hp));
    Put<std::int32_t>(obj.data(), 32 + 20, 2);
    CHECK(kn::HashContainer(obj.data(), pdx, hp2) && hp != hp2);
    Put<std::int32_t>(obj.data(), 32 + 20, -1);
    CHECK(!kn::HashContainer(obj.data(), pdx, hp2));

    // A list at +64: head +0, count +16; node: value at 0 (8 bytes), next at 8.
    Node n2{7, nullptr}, n1{5, &n2};
    const kn::Container list{64, kn::kList, 8, 0, 8};
    Put(obj.data(), 64, &n1);
    Put<std::int32_t>(obj.data(), 64 + 16, 2);
    std::uint64_t hl = 0, hl2 = 0;
    CHECK(kn::HashContainer(obj.data(), list, hl));
    n2.value = 8;
    CHECK(kn::HashContainer(obj.data(), list, hl2) && hl != hl2);
    // A count that does not match the nodes.
    Put<std::int32_t>(obj.data(), 64 + 16, 3);
    CHECK(!kn::HashContainer(obj.data(), list, hl2));

    // The word diff of a whole country-sized object.
    std::vector<unsigned char> before(kn::kCountrySize, 0), after(kn::kCountrySize, 0);
    kn::Change none;
    kn::DiffWords(before.data(), after.data(), none);
    CHECK(none.None());
    after[0x1510 + 3] = 1;
    after[0x9B70] = 2;
    kn::Change two;
    kn::DiffWords(before.data(), after.data(), two);
    CHECK(two.words == 2 && two.word[0] == 0x1510 / 8 && two.word[1] == 0x9B70 / 8);
    kn::Change same;
    kn::DiffWords(before.data(), after.data(), same);
    CHECK(same.key == two.key);
    // The table covers the object and is sorted.
    for (std::size_t i = 0; i < kn::kContainerCount; ++i) {
        CHECK(kn::kContainers[i].offset + 24 <= kn::kCountrySize);
        if (i > 0) CHECK(kn::kContainers[i - 1].offset < kn::kContainers[i].offset);
    }
    std::printf("knowledge_probe ok\n");
    return 0;
}
