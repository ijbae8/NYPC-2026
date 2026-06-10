#include "state.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <utility>
bool Move::is_pass() const
{
    return r1 == -1 && c1 == -1 && r2 == -1 && c2 == -1;
}

bool Move::operator==(const Move& other) const
{
    return r1 == other.r1 && c1 == other.c1 && r2 == other.r2 &&
           c2 == other.c2;
}

namespace {

uint64_t splitmix64(uint64_t x)
{
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

}  // namespace

uint64_t State::zobrist_cell_value(int pos, uint8_t value)
{
    return splitmix64(0x243f6a8885a308d3ull ^
                      (static_cast<uint64_t>(pos) << 8) ^ value);
}

uint64_t State::zobrist_cell_owner(int pos, int8_t owner)
{
    return splitmix64(0x13198a2e03707344ull ^
                      (static_cast<uint64_t>(pos) << 2) ^
                      static_cast<uint64_t>(owner + 1));
}

uint64_t State::zobrist_player(int player)
{
    return splitmix64(0xa4093822299f31d0ull ^ static_cast<uint64_t>(player));
}

uint64_t State::zobrist_passes(int passes)
{
    return splitmix64(0x082efa98ec4e6c89ull ^
                      static_cast<uint64_t>(std::min(passes, 2)));
}

uint64_t State::zobrist_dimensions(int rows, int cols)
{
    return splitmix64(0x452821e638d01377ull ^
                      (static_cast<uint64_t>(rows) << 32) ^
                      static_cast<uint64_t>(cols));
}

State::State(int rows, int cols, const std::vector<std::vector<int>>& init_grid)
    : rows_(rows),
      cols_(cols),
      grid_(),
      owners_(),
      current_player_(0),
      consecutive_passes_(0),
      zobrist_key_(0),
      legal_moves_(),
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
    legal_moves_ = generate_legal_moves_intersecting(0, 0, rows_ - 1, cols_ - 1);
    legal_moves_.push_back(PASS_MOVE);
}

State State::clone() const
{
    State copy(*this);
    copy.undo_stack_.clear();
    return copy;
}

void State::apply(Move m)
{
    UndoRecord record;
    record.move = m;
    record.prev_consecutive_passes = consecutive_passes_;
    record.prev_player = current_player_;
    record.prev_hash = zobrist_key_;
    record.prev_legal_moves = legal_moves_;

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

    std::vector<Move> next_moves;
    next_moves.reserve(legal_moves_.size());
    for (const Move& old_move : legal_moves_) {
        if (old_move.is_pass() || !intersects(old_move, m)) {
            next_moves.push_back(old_move);
        }
    }

    std::vector<Move> regenerated =
        generate_legal_moves_intersecting(m.r1, m.c1, m.r2, m.c2);
    next_moves.insert(next_moves.end(), regenerated.begin(), regenerated.end());
    legal_moves_ = std::move(next_moves);

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
    legal_moves_ = std::move(record.prev_legal_moves);
}

int State::undo_depth() const
{
    return static_cast<int>(undo_stack_.size());
}

std::vector<Move> State::legal_moves() const
{
    return legal_moves_;
}

std::vector<Move> State::generate_legal_moves_intersecting(int affected_r1,
                                                           int affected_c1,
                                                           int affected_r2,
                                                           int affected_c2) const
{
    std::vector<Move> moves;
    std::vector<int> col_sum(cols_, 0);

    for (int r1 = 0; r1 < rows_ && r1 <= affected_r2; ++r1) {
        std::fill(col_sum.begin(), col_sum.end(), 0);

        for (int r2 = r1; r2 < rows_; ++r2) {
            for (int c = 0; c < cols_; ++c) {
                col_sum[c] += grid_[idx(r2, c)];
            }
            if (r2 < affected_r1) {
                continue;
            }

            int c1 = 0;
            int sum = 0;
            for (int c2 = 0; c2 < cols_; ++c2) {
                sum += col_sum[c2];

                while (c1 <= c2 && sum > 10) {
                    sum -= col_sum[c1];
                    ++c1;
                }

                if (sum == 10) {
                    for (int left = c1; left <= c2; ++left) {
                        if (left <= affected_c2 && c2 >= affected_c1 &&
                            border_ok(r1, left, r2, c2)) {
                            moves.push_back({r1, left, r2, c2});
                        }
                        if (col_sum[left] != 0) {
                            break;
                        }
                    }
                }

                if (c1 > affected_c2) {
                    break;
                }
            }
        }
    }

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

    int sum = 0;
    for (int r = m.r1; r <= m.r2; ++r) {
        for (int c = m.c1; c <= m.c2; ++c) {
            sum += grid_[idx(r, c)];
        }
    }

    return sum == 10 && border_ok(m.r1, m.c1, m.r2, m.c2);
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

bool State::border_ok(int r1, int c1, int r2, int c2) const
{
    bool top = false;
    bool bottom = false;
    bool left = false;
    bool right = false;

    for (int c = c1; c <= c2; ++c) {
        top = top || grid_[idx(r1, c)] >= 1;
        bottom = bottom || grid_[idx(r2, c)] >= 1;
    }
    for (int r = r1; r <= r2; ++r) {
        left = left || grid_[idx(r, c1)] >= 1;
        right = right || grid_[idx(r, c2)] >= 1;
    }

    return top && bottom && left && right;
}

bool State::intersects(const Move& lhs, const Move& rhs) const
{
    if (lhs.is_pass() || rhs.is_pass()) {
        return false;
    }
    return lhs.r1 <= rhs.r2 && lhs.r2 >= rhs.r1 && lhs.c1 <= rhs.c2 &&
           lhs.c2 >= rhs.c1;
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
