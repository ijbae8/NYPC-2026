#include "tt.h"

#include <algorithm>
#include <stdexcept>

TranspositionTable::TranspositionTable(size_t capacity)
    : entries_(capacity), used_(0)
{
    if (capacity == 0) {
        throw std::invalid_argument("TT capacity must be positive");
    }
}

const TranspositionEntry* TranspositionTable::probe(uint64_t key) const
{
    const TranspositionEntry& entry = entries_[index(key)];
    if (entry.occupied && entry.key == key) {
        return &entry;
    }
    return nullptr;
}

void TranspositionTable::store(uint64_t key, int depth, float value,
                               Move best_move, BoundType bound)
{
    TranspositionEntry& entry = entries_[index(key)];
    if (!entry.occupied) {
        ++used_;
    }
    entry = {key, depth, value, best_move, bound, true};
}

size_t TranspositionTable::size() const
{
    return used_;
}

size_t TranspositionTable::capacity() const
{
    return entries_.size();
}

void TranspositionTable::clear()
{
    std::fill(entries_.begin(), entries_.end(), TranspositionEntry{});
    used_ = 0;
}

size_t TranspositionTable::index(uint64_t key) const
{
    return static_cast<size_t>(key % entries_.size());
}
