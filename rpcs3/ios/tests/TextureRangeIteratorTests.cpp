#include <cassert>
#include <cstdint>
#include <iterator>
#include <unordered_set>
#include <vector>
using u32 = uint32_t;
#define AUDIT(x) assert(x)
struct address_range32 { u32 start = 0, end = 0; };
enum section_bounds { full_range, locked_range };
struct Section {
    unsigned id;
    address_range32 range;
    bool valid = true, locked = true;
    bool valid_range() const { return valid; }
    bool is_locked() const { return locked; }
    bool overlaps(address_range32 r, section_bounds) const {
        return range.start <= r.end && r.start <= range.end;
    }
};
struct Block {
    std::vector<Section> owned;
    std::unordered_set<Section*> unowned;
    Block* next = nullptr;
    u32 start = 0, end_address = 15;
    auto begin() { return owned.begin(); }
    auto end() { return owned.end(); }
    auto unowned_begin() { return unowned.begin(); }
    auto unowned_end() { return unowned.end(); }
    auto next_block() { return next; }
    u32 get_start() const { return start; }
    u32 get_end() const { return end_address; }
    unsigned get_locked_count() const {
        unsigned n = 0; for (const auto& s : owned) n += s.locked; return n;
    }
};
struct Storage { Block* first; Block& block_for(u32) { return *first; } };
struct UnderTest {
#include "TextureRangeIteratorUnderTest.h"
};
using Iterator = UnderTest::range_iterator_tmpl<Section,
    std::unordered_set<Section*>::iterator, std::vector<Section>::iterator, Block, Storage>;
int main() {
    Section before{1, {0, 31}}, invalid{2, {0,31}, false}, unlocked{3, {0,31}, true, false};
    Block a, b;
    a.next = &b; b.start = 16; b.end_address = 31;
    a.owned = {{4, {0,7}}, {5, {8,15}, true, false}};
    b.owned = {{6, {16,31}}};
    Storage storage{&a};
    Iterator end;
    // Visit sections from earlier blocks, owned entries, then later blocks.
    a.unowned = {&before, &invalid, &unlocked};
    Iterator it(storage, {0,31}, full_range, true);
    assert(it != end && it->id == 1);
    ++it; assert(it->id == 4);
    it++; assert(it->id == 6);
    // Former prefix increment binds a reference through a null end object.
    ++it; assert(it == end);
    // An initially empty range and all-filtered entries produce canonical end.
    a.unowned.clear(); a.owned.clear(); b.owned.clear();
    assert(Iterator(storage, {0,31}, full_range, true) == end);
    a.owned = {{7, {0,15}, true, false}};
    assert(Iterator(storage, {0,31}, full_range, true) == end);
    // Extending a live iterator still reaches the next block.
    a.owned = {{8, {0,15}}}; b.owned = {{9, {16,31}}};
    it = Iterator(storage, {0,15}, full_range, true);
    it.set_end(31); it++; assert(it->id == 9);
    // Shrinking behind the current block must clear the current object too.
    it.set_end(15); assert(it == end);
    it.set_end(0); assert(it == end);
    // Standard prefix/postfix semantics retain the previous iterator.
    it = Iterator(storage, {0,31}, full_range, true);
    auto previous = it; it++; assert(previous->id == 8 && it->id == 9);
    ++it; assert(it == end);
}
