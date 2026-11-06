// port_table: CollectPorts and Fill against the game's loop written out as it stands in 0x350B80, on small
// worlds built at the game's offsets, so a change of order or of a condition shows as a different table.
#include "../src/modules/port_table_core.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <vector>

using namespace sb::port;

namespace {

template <class T>
void Put(unsigned char* base, std::size_t offset, T value) {
    std::memcpy(base + offset, &value, sizeof(T));
}

std::uint64_t Id(std::int16_t tag, std::uint8_t type = 0, std::uint8_t live = 1) {
    return (static_cast<std::uint64_t>(live) << 56) | (static_cast<std::uint64_t>(type) << 48) |
           (static_cast<std::uint64_t>(static_cast<std::uint16_t>(tag)) << 32);
}

struct World {
    static constexpr std::size_t kCountrySize = 0x3000;
    std::vector<unsigned char> root = std::vector<unsigned char>(0x2000);
    std::vector<unsigned char> provinces, classes;
    std::vector<std::vector<unsigned char>> countries;
    std::vector<unsigned char*> countryPointers;
    std::vector<std::vector<std::int32_t>> owned;
    std::vector<std::vector<unsigned char>> relations, rows, lists;
    std::vector<std::vector<std::uint64_t>> friends;
    std::vector<std::uint32_t> access;
    std::vector<std::int16_t> table;
    std::vector<void*> ids;
    std::vector<std::unique_ptr<unsigned char[]>> nodes;
    std::vector<int> distances;
    int provinceCount = 0, countryCount = 0;

    unsigned char* Province(int p) { return provinces.data() + p * kProvinceStride; }

    World(int provinceTotal, int countryTotal, std::mt19937& random) : provinceCount(provinceTotal), countryCount(countryTotal) {
        provinces.resize(provinceTotal * kProvinceStride);
        classes.resize(0x400 * 2);
        Put(classes.data() + 0x400, kClassFlags, std::uint8_t{kClassClosed});
        ids.resize(provinceTotal);
        Put(root.data(), kRootProvinceIdsBegin, static_cast<void*>(ids.data()));
        Put(root.data(), kRootProvinceIdsEnd, static_cast<void*>(ids.data() + provinceTotal));
        Put(root.data(), kRootProvinces, static_cast<void*>(provinces.data()));
        countries.assign(countryTotal, std::vector<unsigned char>(kCountrySize));
        owned.resize(countryTotal);
        access.assign(4, 0);
        for (auto& word : access) word = random();
        friends.resize(provinceTotal);
        for (int p = 0; p < provinceTotal; ++p) {
            unsigned char* q = Province(p);
            const int owner = static_cast<int>(random() % countryTotal);
            owned[owner].push_back(p);
            Put(q, kProvinceSelf, static_cast<std::int16_t>(p));
            Put(q, kProvinceClass, static_cast<void*>(classes.data() + (random() % 8 == 0 ? 0x400 : 0)));
            Put(q, kProvinceOwner, Id(static_cast<std::int16_t>(owner), 0, random() % 8 != 0));
            Put(q, kProvinceController, Id(static_cast<std::int16_t>(random() % 8 == 0 ? (owner + 1) % countryTotal : owner)));
            Put(q, kProvinceBuilding, reinterpret_cast<void*>(static_cast<std::uintptr_t>(random() % 8 != 0)));
            Put(q, kProvinceFlags, static_cast<std::uint8_t>(random() & (kFlagPort | kFlagBlocked)));
            Put(q, kProvinceFlagsHigh, static_cast<std::uint8_t>(random() % 2 ? kFlagHighRestricted : 0));
            Put(q, kProvinceAccess, static_cast<void*>(access.data()));
            for (int f = 0, n = static_cast<int>(random() % 4); f < n; ++f) friends[p].push_back(Id(static_cast<std::int16_t>(random() % countryTotal)));
            if (random() % 2) friends[p].push_back(Id(static_cast<std::int16_t>(owner)));
            Put(q, kProvinceFriends, static_cast<void*>(friends[p].data()));
            Put(q, kProvinceFriendsEnd, static_cast<void*>(friends[p].data() + friends[p].size()));
        }
        for (int c = 0; c < countryTotal; ++c) {
            unsigned char* country = countries[c].data();
            countryPointers.push_back(country);
            Put(country, kCountryId, Id(static_cast<std::int16_t>(c), random() % 8 == 0 ? kTypeWithoutProvinces : 0, random() % 8 != 0));
            Put(country, kCountryProvinces, static_cast<void*>(owned[c].data()));
            Put(country, kCountryProvinceCount, static_cast<std::int32_t>(owned[c].size()));
            Put(country, kCountryPortCount, std::int32_t{1});
        }
        Put(root.data(), kRootCountries, static_cast<void*>(countryPointers.data()));
        rows.assign(countryTotal, std::vector<unsigned char>(countryTotal * kRowStride));
        for (int c = 0; c < countryTotal; ++c) {
            for (int r = 0; r < countryTotal; ++r) {
                relations.emplace_back(0x200);
                unsigned char* relation = relations.back().data();
                unsigned char* row = rows[c].data() + r * kRowStride;
                Put(row, 0, static_cast<void*>(relation));
                Put(row, kRowId, Id(static_cast<std::int16_t>(r)));
                for (std::size_t list : {kListA, kListB}) {
                    lists.emplace_back(4 * kEntrySize);
                    unsigned char* entries = lists.back().data();
                    int count = static_cast<int>(random() % 4);
                    for (int e = 0; e < count; ++e) {
                        Put(entries + e * kEntrySize, 0, static_cast<std::int32_t>(random() % 2 ? r : (r + 1) % countryTotal));
                        if (random() % 2) {
                            auto node = std::make_unique<unsigned char[]>(0x40);
                            std::memset(node.get(), 0, 0x40);
                            Put(node.get(), kEntryObjectId, Id(static_cast<std::int16_t>(random() % 4 ? r : (r + 1) % countryTotal)));
                            Put(entries + e * kEntrySize, kEntryPointer, static_cast<void*>(node.get()));
                            nodes.push_back(std::move(node));
                        }
                    }
                    Put(relation, list, static_cast<void*>(entries));
                    Put(relation, list == kListA ? kListACount : kListBCount, static_cast<std::int32_t>(count));
                }
            }
            Put(countries[c].data(), kCountryRows, static_cast<void*>(rows[c].data()));
            Put(countries[c].data(), kCountryRowsEnd, static_cast<void*>(rows[c].data() + rows[c].size()));
        }
        distances.resize(provinceTotal * provinceTotal);
        for (auto& d : distances) d = 1 + static_cast<int>(random() % 50);  // small, so ties are common
        table.assign(provinceTotal, -1);
    }

    int Distance(Bytes from, Bytes to) {
        const auto a = (from - provinces.data()) / static_cast<std::ptrdiff_t>(kProvinceStride);
        const auto b = (to - provinces.data()) / static_cast<std::ptrdiff_t>(kProvinceStride);
        return distances[a * provinceCount + b];
    }
};

// The game's loop, every walk inside the province loop, as in the decompiled 0x350B80.
void GameLoop(World& w, unsigned char* c, std::int16_t* table, int range) {
    const std::uint64_t id = At<std::uint64_t>(c, kCountryId);
    const std::int16_t tag = TagOf(id);
    const bool open = OpenToAllProvinces(c);
    for (int p = 1; p < w.provinceCount; ++p) {
        unsigned char* target = w.Province(p);
        if (!TargetCounts(target, id, open)) continue;
        table[p] = 0;
        int best = kFarAway;
        Bytes nearest = nullptr;
        const auto consider = [&](Bytes q) {
            const int d = w.Distance(q, target);
            if (d < best) {
                best = d;
                nearest = q;
            }
        };
        const auto* own = At<const std::int32_t*>(c, kCountryProvinces);
        for (int i = 0, n = At<std::int32_t>(c, kCountryProvinceCount); i < n; ++i) {
            Bytes q = w.Province(own[i]);
            const auto flags = At<std::uint8_t>(q, kProvinceFlags);
            const auto owner = At<std::uint64_t>(q, kProvinceOwner);
            if ((flags & kFlagPort) != 0 && TagOf(At<std::uint64_t>(q, kProvinceController)) == TagOf(owner) && (owner >> 56) != 0 &&
                At<const void*>(q, kProvinceBuilding) != nullptr) {
                for (auto e = At<const std::uint64_t*>(q, kProvinceFriends); e != At<const std::uint64_t*>(q, kProvinceFriendsEnd); ++e) {
                    if (TagOf(*e) != tag) continue;
                    if ((flags & kFlagBlocked) == 0) consider(q);
                    break;
                }
            }
        }
        const auto rows = At<Bytes>(c, kCountryRows);
        const auto rowCount = (At<Bytes>(c, kCountryRowsEnd) - rows) / static_cast<std::ptrdiff_t>(kRowStride);
        for (std::ptrdiff_t r = 0; r < rowCount; ++r) {
            if (!RowLendsPorts(rows + r * kRowStride)) continue;
            Bytes other = w.countryPointers[r];
            const auto* list = At<const std::int32_t*>(other, kCountryProvinces);
            for (int i = 0, n = At<std::int32_t>(other, kCountryProvinceCount); i < n; ++i) {
                Bytes q = w.Province(list[i]);
                const auto flags = At<std::uint8_t>(q, kProvinceFlags);
                const auto owner = At<std::uint64_t>(q, kProvinceOwner);
                if ((flags & kFlagPort) == 0 || TagOf(At<std::uint64_t>(q, kProvinceController)) != TagOf(owner) || (owner >> 56) == 0 ||
                    At<const void*>(q, kProvinceBuilding) == nullptr) {
                    continue;
                }
                for (auto e = At<const std::uint64_t*>(q, kProvinceFriends); e != At<const std::uint64_t*>(q, kProvinceFriendsEnd); ++e) {
                    if (TagOf(*e) != TagOf(owner)) continue;
                    if ((flags & kFlagBlocked) == 0) consider(q);
                    break;
                }
            }
        }
        if (nearest != nullptr) {
            const auto self = At<std::int16_t>(nearest, kProvinceSelf);
            table[p] = range < best ? static_cast<std::int16_t>(-self) : self;
        }
    }
}

void TestAgainstGame() {
    std::mt19937 random(20261106);
    long differentRanges = 0;
    for (int round = 0; round < 40; ++round) {
        World w(60, 6, random);
        for (int c = 0; c < w.countryCount; ++c) {
            unsigned char* country = w.countries[c].data();
            std::vector<std::int16_t> game(w.provinceCount, -1), ours(w.provinceCount, -1);
            const int range = static_cast<int>(random() % 60);
            GameLoop(w, country, game.data(), range);
            std::vector<Bytes> ports;
            CollectPorts(w.root.data(), country, ports);
            int rangeCalls = 0;
            Fill(
                w.root.data(), country, ports, ours.data(), [&](Bytes from, Bytes to) { return w.Distance(from, to); },
                [&] {
                    ++rangeCalls;
                    return range;
                });
            assert(game == ours);
            assert(rangeCalls <= 1);
            for (auto v : ours) differentRanges += v < 0 && v != -1;
        }
    }
    assert(differentRanges > 0);  // the sign of out-of-range provinces was exercised
}

void TestApplies() {
    std::mt19937 random(7);
    World w(8, 2, random);
    unsigned char* country = w.countries[0].data();
    std::vector<std::int16_t> table(8);
    Put(country, kCountryTable, static_cast<void*>(table.data()));
    Put(country, kCountryTableEnd, static_cast<void*>(table.data() + 8));
    Put(country, kCountryProvinceCount, std::int32_t{1});
    assert(Applies(w.root.data(), country));
    Put(country, kCountryPortCount, std::int32_t{0});
    assert(!Applies(w.root.data(), country));
    Put(country, kCountryPortCount, std::int32_t{1});
    Put(country, kCountryTableEnd, static_cast<void*>(table.data() + 7));  // not the map's size
    assert(!Applies(w.root.data(), country));
    Put(country, kCountryTableEnd, static_cast<void*>(table.data()));  // the first build: the game resizes it
    assert(!Applies(w.root.data(), country));
    Put(country, kCountryTableEnd, static_cast<void*>(table.data() + 8));
    Put(country, kCountryProvinceCount, std::int32_t{0});
    Put(country, kCountryType, std::uint8_t{0});
    assert(!Applies(w.root.data(), country));
    Put(country, kCountryType, kTypeWithoutProvinces);
    assert(Applies(w.root.data(), country));
}

void TestWarState() {
    std::vector<unsigned char> country(0x3000), data(0x80), node(0x20), data2(0x80), node2(0x20);
    assert(!OpenToAllProvinces(country.data()));
    Put(country.data(), kCountryWars, static_cast<void*>(node.data()));
    Put(node.data(), 0, static_cast<void*>(data.data()));
    Put(node.data(), kWarNodeNext, static_cast<void*>(node2.data()));
    Put(node2.data(), 0, static_cast<void*>(data2.data()));
    for (std::int32_t state : {0, 1, 2, 4}) {
        Put(data.data(), kWarStateOffset, state);
        assert(!OpenToAllProvinces(country.data()));
    }
    Put(data2.data(), kWarStateOffset, kWarState);  // the second node
    assert(OpenToAllProvinces(country.data()));
}

}  // namespace

int main() {
    TestAgainstGame();
    TestApplies();
    TestWarState();
    std::printf("port_table ok\n");
    return 0;
}
