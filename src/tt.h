#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "state.h"

constexpr size_t TT_CAPACITY = 1 << 20;

enum class BoundType {
    Exact,
    Lower,
    Upper,
};

struct TranspositionEntry {
    uint64_t key;
    int depth;
    int value;
    Move best_move;
    BoundType bound;
    bool occupied;
};

class TranspositionTable {
public:
    explicit TranspositionTable(size_t capacity = TT_CAPACITY);
    const TranspositionEntry* probe(uint64_t key) const;
    void store(uint64_t key, int depth, int value, Move best_move,
               BoundType bound);
    size_t size() const;
    size_t capacity() const;
    void clear();

private:
    std::vector<TranspositionEntry> entries_;
    size_t used_;

    size_t index(uint64_t key) const;
};
