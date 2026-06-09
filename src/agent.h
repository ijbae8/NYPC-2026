#pragma once

#include <vector>

#include "state.h"

class Agent {
public:
    virtual ~Agent() = default;
    virtual void set_side(bool plays_first) = 0;
    virtual void initialize(const std::vector<std::vector<int>>& board) = 0;
    virtual Move choose_move(int my_time_ms, int opp_time_ms) = 0;
    virtual void apply_opponent_move(Move move) = 0;
};
