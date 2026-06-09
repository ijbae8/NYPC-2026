#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <optional>
#include <vector>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

#define R 10
#define C 17

struct Move {
    int r1, c1, r2, c2;

    bool is_pass() const
    {
        return r1 == -1 && c1 == -1 && r2 == -1 && c2 == -1;
    }

    bool operator==(const Move& other) const
    {
        return r1 == other.r1 && c1 == other.c1 && r2 == other.r2 &&
               c2 == other.c2;
    }
};

inline const Move PASS_MOVE = {-1, -1, -1, -1};

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
    int rows_, cols_;
    std::vector<int8_t> grid_;
    std::vector<int8_t> owners_;
    int current_player_;
    int consecutive_passes_;
    uint64_t zobrist_key_;

    mutable std::vector<int32_t> psum_val_;
    mutable std::vector<int32_t> psum_mush_;
    mutable bool is_dirty_;

    std::vector<UndoRecord> undo_stack_;

    void rebuild_prefix_sums() const;
    int rect_sum(int r1, int c1, int r2, int c2) const;
    int rect_mush(int r1, int c1, int r2, int c2) const;
    bool border_ok(int r1, int c1, int r2, int c2) const;
    int idx(int r, int c) const;
    uint64_t compute_zobrist_key() const;
};

namespace {

uint64_t splitmix64(uint64_t x)
{
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

uint64_t zobrist_cell_value(int pos, uint8_t value)
{
    return splitmix64(0x243f6a8885a308d3ull ^
                      (static_cast<uint64_t>(pos) << 8) ^ value);
}

uint64_t zobrist_cell_owner(int pos, int8_t owner)
{
    return splitmix64(0x13198a2e03707344ull ^
                      (static_cast<uint64_t>(pos) << 2) ^
                      static_cast<uint64_t>(owner + 1));
}

uint64_t zobrist_player(int player)
{
    return splitmix64(0xa4093822299f31d0ull ^ static_cast<uint64_t>(player));
}

uint64_t zobrist_passes(int passes)
{
    return splitmix64(0x082efa98ec4e6c89ull ^
                      static_cast<uint64_t>(std::min(passes, 2)));
}

uint64_t zobrist_dimensions(int rows, int cols)
{
    return splitmix64(0x452821e638d01377ull ^
                      (static_cast<uint64_t>(rows) << 32) ^
                      static_cast<uint64_t>(cols));
}

} // namespace

State::State(int rows, int cols, const std::vector<std::vector<int>>& init_grid)
    : rows_(rows),
      cols_(cols),
      grid_(),
      owners_(),
      current_player_(0),
      consecutive_passes_(0),
      zobrist_key_(0),
      psum_val_(),
      psum_mush_(),
      is_dirty_(true),
      undo_stack_()
{
    if (rows_ <= 0 || cols_ <= 0) {
        throw std::invalid_argument("State dimensions must be positive");
    }
    if (static_cast<int>(init_grid.size()) != rows_) {
        throw std::invalid_argument("Initial grid row count does not match rows");
    }

    grid_.reserve(static_cast<size_t>(rows_) * static_cast<size_t>(cols_));
    for (int r = 0; r < rows_; ++r) {
        if (static_cast<int>(init_grid[r].size()) != cols_) {
            throw std::invalid_argument("Initial grid column count does not match cols");
        }
        for (int c = 0; c < cols_; ++c) {
            if (init_grid[r][c] < -128 || init_grid[r][c] > 127) {
                throw std::invalid_argument("Cell value does not fit in int8_t");
            }
            grid_.push_back(static_cast<int8_t>(init_grid[r][c]));
        }
    }
    owners_.assign(grid_.size(), static_cast<int8_t>(-1));
    zobrist_key_ = compute_zobrist_key();
}

State State::clone() const
{
    State copy(*this);
    copy.undo_stack_.clear();
    copy.psum_val_.clear();
    copy.psum_mush_.clear();
    copy.is_dirty_ = true;
    return copy;
}

void State::apply(Move m)
{
    if (!is_legal(m)) {
        throw std::invalid_argument("Illegal move");
    }

    UndoRecord record;
    record.move = m;
    record.prev_consecutive_passes = consecutive_passes_;
    record.prev_player = current_player_;
    record.prev_hash = zobrist_key_;

    if (m.is_pass()) {
        zobrist_key_ ^= zobrist_player(current_player_);
        zobrist_key_ ^= zobrist_passes(consecutive_passes_);
        ++consecutive_passes_;
        current_player_ = 1 - current_player_;
        zobrist_key_ ^= zobrist_player(current_player_);
        zobrist_key_ ^= zobrist_passes(consecutive_passes_);
        undo_stack_.push_back(std::move(record));
        return;
    }

    for (int r = m.r1; r <= m.r2; ++r) {
        for (int c = m.c1; c <= m.c2; ++c) {
            const int pos = idx(r, c);
            const int8_t new_val = 0;
            const int8_t new_owner = static_cast<int8_t>(current_player_);
            if (grid_[pos] != new_val || owners_[pos] != new_owner) {
                record.deltas.push_back({pos, grid_[pos], owners_[pos]});
            }
        }
    }

    for (const UndoRecord::CellDelta& delta : record.deltas) {
        zobrist_key_ ^= zobrist_cell_value(delta.idx, static_cast<uint8_t>(delta.old_val));
        zobrist_key_ ^= zobrist_cell_owner(delta.idx, delta.old_owner);
        grid_[delta.idx] = 0;
        owners_[delta.idx] = static_cast<int8_t>(current_player_);
        zobrist_key_ ^= zobrist_cell_value(delta.idx, 0);
        zobrist_key_ ^= zobrist_cell_owner(delta.idx, static_cast<int8_t>(current_player_));
    }

    zobrist_key_ ^= zobrist_player(current_player_);
    zobrist_key_ ^= zobrist_passes(consecutive_passes_);
    consecutive_passes_ = 0;
    current_player_ = 1 - current_player_;
    zobrist_key_ ^= zobrist_player(current_player_);
    zobrist_key_ ^= zobrist_passes(consecutive_passes_);
    is_dirty_ = true;
    undo_stack_.push_back(std::move(record));
}

void State::undo()
{
    if (undo_stack_.empty()) {
        throw std::out_of_range("No move to undo");
    }

    UndoRecord record = std::move(undo_stack_.back());
    undo_stack_.pop_back();

    for (const UndoRecord::CellDelta& delta : record.deltas) {
        grid_[delta.idx] = delta.old_val;
        owners_[delta.idx] = delta.old_owner;
    }
    consecutive_passes_ = record.prev_consecutive_passes;
    current_player_ = record.prev_player;
    zobrist_key_ = record.prev_hash;
    is_dirty_ = true;
}

int State::undo_depth() const
{
    return static_cast<int>(undo_stack_.size());
}

std::vector<Move> State::legal_moves() const
{
    rebuild_prefix_sums();
    std::vector<Move> moves;

    // The board sizes targeted here are small enough for O((RC)^2)
    // enumeration. Early-exit pruning is a natural future optimisation.
    for (int r1 = 0; r1 < rows_; ++r1) {
        for (int c1 = 0; c1 < cols_; ++c1) {
            for (int r2 = r1; r2 < rows_; ++r2) {
                for (int c2 = c1; c2 < cols_; ++c2) {
                    if (rect_sum(r1, c1, r2, c2) == 10 &&
                        border_ok(r1, c1, r2, c2)) {
                        moves.push_back({r1, c1, r2, c2});
                    }
                }
            }
        }
    }

    moves.push_back(PASS_MOVE);
    return moves;
}

bool State::is_legal(const Move& m) const
{
    if (m.is_pass()) {
        return true;
    }

    if (m.r1 < 0 || m.c1 < 0 || m.r2 < 0 || m.c2 < 0 || m.r1 > m.r2 ||
        m.c1 > m.c2 || m.r2 >= rows_ || m.c2 >= cols_) {
        return false;
    }

    return rect_sum(m.r1, m.c1, m.r2, m.c2) == 10 &&
           border_ok(m.r1, m.c1, m.r2, m.c2);
}

bool State::is_terminal() const
{
    return consecutive_passes_ >= 2;
}

std::pair<int, int> State::scores() const
{
    int p0 = 0;
    int p1 = 0;
    for (int8_t owner : owners_) {
        if (owner == 0) {
            ++p0;
        } else if (owner == 1) {
            ++p1;
        }
    }
    return {p0, p1};
}

int State::winner() const
{
    const std::pair<int, int> score = scores();
    if (score.first > score.second) {
        return 0;
    }
    if (score.second > score.first) {
        return 1;
    }
    return -1;
}

double State::reward(int player) const
{
    if (player != 0 && player != 1) {
        throw std::invalid_argument("Player must be 0 or 1");
    }
    const int result = winner();
    if (result == -1) {
        return 0.5;
    }
    return result == player ? 1.0 : 0.0;
}

int State::current_player() const
{
    return current_player_;
}

int State::rows() const
{
    return rows_;
}

int State::cols() const
{
    return cols_;
}

int8_t State::cell_value(int r, int c) const
{
    if (r < 0 || c < 0 || r >= rows_ || c >= cols_) {
        throw std::out_of_range("Cell coordinates are out of bounds");
    }
    return grid_[idx(r, c)];
}

int8_t State::cell_owner(int r, int c) const
{
    if (r < 0 || c < 0 || r >= rows_ || c >= cols_) {
        throw std::out_of_range("Cell coordinates are out of bounds");
    }
    return owners_[idx(r, c)];
}

bool State::operator==(const State& other) const
{
    return rows_ == other.rows_ && cols_ == other.cols_ &&
           grid_ == other.grid_ && owners_ == other.owners_ &&
           current_player_ == other.current_player_ &&
           consecutive_passes_ == other.consecutive_passes_;
}

uint64_t State::hash() const
{
    return zobrist_key_;
}

std::string State::to_string() const
{
    std::ostringstream out;
    out << "player=" << current_player_ << " passes=" << consecutive_passes_
        << '\n';
    for (int r = 0; r < rows_; ++r) {
        for (int c = 0; c < cols_; ++c) {
            const int pos = idx(r, c);
            if (grid_[pos] > 0) {
                out << static_cast<int>(grid_[pos]);
            } else if (owners_[pos] == -1) {
                out << '.';
            } else {
                out << static_cast<char>('A' + owners_[pos]);
            }
            if (c + 1 < cols_) {
                out << ' ';
            }
        }
        if (r + 1 < rows_) {
            out << '\n';
        }
    }
    return out.str();
}

void State::rebuild_prefix_sums() const
{
    if (!is_dirty_) {
        return;
    }

    const int prow = rows_ + 1;
    const int pcol = cols_ + 1;
    psum_val_.assign(static_cast<size_t>(prow) * static_cast<size_t>(pcol), 0);
    psum_mush_.assign(static_cast<size_t>(prow) * static_cast<size_t>(pcol), 0);

    for (int r = 0; r < rows_; ++r) {
        for (int c = 0; c < cols_; ++c) {
            const int pidx = (r + 1) * pcol + (c + 1);
            const int above = r * pcol + (c + 1);
            const int left = (r + 1) * pcol + c;
            const int diag = r * pcol + c;
            const int source = idx(r, c);

            psum_val_[pidx] = psum_val_[above] + psum_val_[left] -
                              psum_val_[diag] + grid_[source];
            psum_mush_[pidx] = psum_mush_[above] + psum_mush_[left] -
                               psum_mush_[diag] +
                               (grid_[source] >= 1 ? 1 : 0);
        }
    }

    is_dirty_ = false;
}

int State::rect_sum(int r1, int c1, int r2, int c2) const
{
    rebuild_prefix_sums();
    const int stride = cols_ + 1;
    const int a = r1 * stride + c1;
    const int b = r1 * stride + (c2 + 1);
    const int c = (r2 + 1) * stride + c1;
    const int d = (r2 + 1) * stride + (c2 + 1);
    return psum_val_[d] - psum_val_[b] - psum_val_[c] + psum_val_[a];
}

int State::rect_mush(int r1, int c1, int r2, int c2) const
{
    rebuild_prefix_sums();
    const int stride = cols_ + 1;
    const int a = r1 * stride + c1;
    const int b = r1 * stride + (c2 + 1);
    const int c = (r2 + 1) * stride + c1;
    const int d = (r2 + 1) * stride + (c2 + 1);
    return psum_mush_[d] - psum_mush_[b] - psum_mush_[c] + psum_mush_[a];
}

bool State::border_ok(int r1, int c1, int r2, int c2) const
{
    return rect_mush(r1, c1, r1, c2) > 0 &&
           rect_mush(r2, c1, r2, c2) > 0 &&
           rect_mush(r1, c1, r2, c1) > 0 &&
           rect_mush(r1, c2, r2, c2) > 0;
}

int State::idx(int r, int c) const
{
    return r * cols_ + c;
}

uint64_t State::compute_zobrist_key() const
{
    uint64_t key = zobrist_dimensions(rows_, cols_);
    for (int pos = 0; pos < static_cast<int>(grid_.size()); ++pos) {
        key ^= zobrist_cell_value(pos, static_cast<uint8_t>(grid_[pos]));
        key ^= zobrist_cell_owner(pos, owners_[pos]);
    }
    key ^= zobrist_player(current_player_);
    key ^= zobrist_passes(consecutive_passes_);
    return key;
}

int cmp(Move m1, Move m2)
{
    return (m1.r2 - m1.r1 + 1) * (m1.c2 - m1.c1 + 1) > (m2.r2 - m2.r1 + 1) * (m2.c2 - m2.c1 + 1);
}

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
};

using TranspositionTable = std::unordered_map<uint64_t, TranspositionEntry>;

std::pair<int, Move> negamax(State& state, int depth, int alpha, int beta,
                             int player, TranspositionTable& tt)
{
    if((depth == 0) || state.is_terminal())
    {
        auto scores = state.scores();
        int value = scores.first-scores.second;
        if (player == 1) value = -value;
        return std::make_pair(value, PASS_MOVE);
    }

    const int original_alpha = alpha;
    const int original_beta = beta;
    const uint64_t key = state.hash();
    auto tt_it = tt.find(key);
    if (tt_it != tt.end()) {
        const TranspositionEntry& entry = tt_it->second;
        if (entry.key == key && entry.depth >= depth) {
            if (entry.bound == BoundType::Exact) {
                return std::make_pair(entry.value, entry.best_move);
            }
            if (entry.bound == BoundType::Lower) {
                alpha = std::max(alpha, entry.value);
            } else if (entry.bound == BoundType::Upper) {
                beta = std::min(beta, entry.value);
            }
            if (alpha >= beta) {
                return std::make_pair(entry.value, entry.best_move);
            }
        }
    }

    auto moves = state.legal_moves();
    std::sort(moves.begin(), moves.end(), cmp);
    int value = -1000;
    Move best_move = PASS_MOVE;
    for (auto move : moves)
    {
        state.apply(move);
        int child_value = -negamax(state, depth-1, -beta, -alpha, 1-player, tt).first;
        if(child_value > value)
        {
            value = child_value;
            best_move = move;
        }
        alpha = std::max(alpha, value);
        state.undo();
        if(alpha >= beta) break;
    }

    BoundType bound = BoundType::Exact;
    if (value <= original_alpha) {
        bound = BoundType::Upper;
    } else if (value >= original_beta) {
        bound = BoundType::Lower;
    }
    tt[key] = {key, depth, value, best_move, bound};
    return std::make_pair(value, best_move);
}

int main()
{
    int my_side;
    std::optional<State> state;

    char line[1024];
    char command[32];
    while (fgets(line, sizeof(line), stdin))
    {
        if (sscanf(line, "%s", command) != 1)
            continue;

        if (strcmp(command, "READY") == 0)
        {
            // Check if this player goes first
            char turn[32];
            sscanf(line, "%*s %s", turn);
            if (strcmp(turn, "FIRST") == 0) my_side = 0;
            else my_side = 1;
            printf("OK\n");
            fflush(stdout);
            continue;
        }

        if (strcmp(command, "INIT") == 0)
        {
            // Initialize the board
            char *token = strtok(line, " ");
            std::vector<std::vector<int>> board;
            for (int i = 0; i < R; i++)
            {
                std::vector<int> temp;
                token = strtok(NULL, " ");
                for (int j = 0; j < C; j++) temp.push_back(token[j] - '0');
                board.push_back(temp);
            }
            state.emplace(R, C, board);
            continue;
        }

        if (strcmp(command, "TIME") == 0)
        {
            // My turn: calculate and execute move
            int my_time, opp_time;
            sscanf(line, "%*s %d %d", &my_time, &opp_time);
            TranspositionTable tt;
            tt.reserve(1 << 18);
            auto result = negamax(*state, 5, -1000, 1000, (*state).current_player(), tt);
            Move best_move = result.second;

            auto [r1, c1, r2, c2] = best_move;
            printf("%d %d %d %d\n", r1, c1, r2, c2);

            (*state).apply(best_move);
            fflush(stdout);
            continue;
        }

        if (strcmp(command, "OPP") == 0)
        {
            // Apply opponent's turn
            int r1, c1, r2, c2, time;
            sscanf(line, "%*s %d %d %d %d %d", &r1, &c1, &r2, &c2, &time);
            Move move = {r1, c1, r2, c2};
            if((*state).current_player() == 1 - my_side) (*state).apply(move);
            continue;
        }

        if (strcmp(command, "FINISH") == 0)
            break;

        // Handle invalid command
        fprintf(stderr, "Invalid command: %s\n", command);
        return 1;
    }

    return 0;
}
