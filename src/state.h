#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

struct Move {
    int r1, c1, r2, c2;

    bool is_pass() const;
    bool operator==(const Move& other) const;
};

inline constexpr Move PASS_MOVE = {-1, -1, -1, -1};

class State {
public:
    State(int rows, int cols, const std::vector<std::vector<int>>& init_grid);
    State(const State&) = default;

    State clone() const;

    void apply(Move m);
    void undo();
    int undo_depth() const;

    std::vector<Move> legal_moves() const;
    bool is_legal(const Move& m) const;
    bool is_terminal() const;
    std::pair<int, int> scores() const;
    int winner() const;
    double reward(int player) const;

    int current_player() const;
    int rows() const;
    int cols() const;
    int8_t cell_value(int r, int c) const;
    int8_t cell_owner(int r, int c) const;

    bool operator==(const State& other) const;
    uint64_t hash() const;

    std::string to_string() const;

private:
    struct UndoRecord {
        Move move;
        int prev_consecutive_passes;
        int prev_player;
        uint64_t prev_hash;

        struct CellDelta {
            int idx;
            int8_t old_val;
            int8_t old_owner;
        };

        std::vector<CellDelta> deltas;
    };

    int rows_;
    int cols_;
    std::vector<int8_t> grid_;
    std::vector<int8_t> owners_;
    int current_player_;
    int consecutive_passes_;
    uint64_t zobrist_key_;

    std::vector<UndoRecord> undo_stack_;

    static uint64_t zobrist_cell_value(int pos, uint8_t value);
    static uint64_t zobrist_cell_owner(int pos, int8_t owner);
    static uint64_t zobrist_player(int player);
    static uint64_t zobrist_passes(int passes);
    static uint64_t zobrist_dimensions(int rows, int cols);

    bool border_ok(int r1, int c1, int r2, int c2) const;
    int idx(int r, int c) const;
    uint64_t compute_zobrist_key() const;
};
