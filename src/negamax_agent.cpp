#include "negamax_agent.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

float ScoreDiffEvaluator::evaluate(const State& state, int player) const
{
    auto scores = state.scores();
    float value = static_cast<float>(scores.first - scores.second);
    if (player == 1) {
        value = -value;
    }
    return value;
}

float AreaMovePrioritizer::priority(const State& /*state*/, const Move& move,
                                    std::optional<Move> /*tt_best_move*/,
                                    int /*depth*/, int /*ply*/,
                                    bool /*is_root*/) const
{
    if (move.is_pass()) {
        return -1.0f;
    }

    const int height = move.r2 - move.r1 + 1;
    const int width = move.c2 - move.c1 + 1;
    return static_cast<float>(height * width);
}

AdaptiveTimeBudgeter::AdaptiveTimeBudgeter(
    int expected_game_plies, int min_remaining_plies, int clock_reserve_ms,
    int min_move_budget_ms, int max_move_budget_ms, int time_buffer_ms,
    int heavy_branch_threshold, int medium_branch_threshold,
    int light_branch_threshold)
    : expected_game_plies_(expected_game_plies),
      min_remaining_plies_(min_remaining_plies),
      clock_reserve_ms_(clock_reserve_ms),
      min_move_budget_ms_(min_move_budget_ms),
      max_move_budget_ms_(max_move_budget_ms),
      time_buffer_ms_(time_buffer_ms),
      heavy_branch_threshold_(heavy_branch_threshold),
      medium_branch_threshold_(medium_branch_threshold),
      light_branch_threshold_(light_branch_threshold)
{
}

int AdaptiveTimeBudgeter::compute_ms(int my_time_ms, int current_ply,
                                     int legal_move_count) const
{
    const int usable_time_ms = std::max(1, my_time_ms - clock_reserve_ms_);
    const int remaining_plies = std::max(0, expected_game_plies_ - current_ply);
    const int remaining_my_turns = std::max(3, (remaining_plies + 1) / 2);

    int budget = usable_time_ms / remaining_my_turns;

    if (legal_move_count > heavy_branch_threshold_) {
        budget = budget * 3 / 2;
    } else if (legal_move_count > medium_branch_threshold_) {
        budget = budget * 5 / 4;
    } else if (legal_move_count < light_branch_threshold_) {
        budget = budget * 3 / 4;
    }

    budget = std::clamp(budget, min_move_budget_ms_, max_move_budget_ms_);
    return std::min(budget, std::max(1, my_time_ms - time_buffer_ms_));
}

NegamaxAgent::NegamaxAgent(
    SearchConfig config, std::unique_ptr<Evaluator> evaluator,
    std::unique_ptr<MovePrioritizer> move_prioritizer,
    std::unique_ptr<TimeBudgeter> time_budgeter)
    : config_(config),
      evaluator_(std::move(evaluator)),
      move_prioritizer_(std::move(move_prioritizer)),
      time_budgeter_(std::move(time_budgeter)),
      my_side_(0),
      state_(),
      tt_(config.tt_capacity)
{
    if (evaluator_ == nullptr || move_prioritizer_ == nullptr ||
        time_budgeter_ == nullptr) {
        throw std::invalid_argument("NegamaxAgent policies must not be null");
    }
}

void NegamaxAgent::set_side(bool plays_first)
{
    my_side_ = plays_first ? 0 : 1;
}

void NegamaxAgent::initialize(const std::vector<std::vector<int>>& board)
{
    state_.emplace(config_.rows, config_.cols, board);
    tt_.clear();
}

Move NegamaxAgent::choose_move(int my_time_ms, int /*opp_time_ms*/)
{
    ensure_initialized();

    const int legal_move_count = static_cast<int>(state_->legal_moves().size());
    const int time_budget_ms = time_budgeter_->compute_ms(
        my_time_ms, state_->undo_depth(), legal_move_count);
    Move best_move =
        iterative_deepening(*state_, config_.max_depth, time_budget_ms).second;
    state_->apply(best_move);
    return best_move;
}

void NegamaxAgent::apply_opponent_move(Move move)
{
    ensure_initialized();
    if (state_->current_player() == 1 - my_side_) {
        state_->apply(move);
    }
}

void NegamaxAgent::ensure_initialized() const
{
    if (!state_.has_value()) {
        throw std::logic_error("AI state has not been initialized");
    }
}

void NegamaxAgent::order_moves(const State& state, std::vector<Move>& moves,
                               std::optional<Move> tt_best_move, int depth,
                               int ply, bool is_root,
                               std::optional<Move> last_move) const
{
    std::sort(moves.begin(), moves.end(),
              [&](const Move& lhs, const Move& rhs) {
                  auto priority = [&](const Move& move) {
                      float value = move_prioritizer_->priority(
                          state, move, tt_best_move, depth, ply, is_root);
                      if (last_move.has_value() &&
                          overlaps(move, *last_move)) {
                          const int swing =
                              move_swing(state, move,
                                         state.current_player());
                          if (swing >= config_.tactical_swing_threshold) {
                              value += 10000.0f + 100.0f * swing;
                          }
                      }
                      return value;
                  };

                  return priority(lhs) > priority(rhs);
              });

    if (tt_best_move.has_value()) {
        auto tt_move = std::find(moves.begin(), moves.end(), *tt_best_move);
        if (tt_move != moves.end()) {
            std::iter_swap(moves.begin(), tt_move);
        }
    }
}

bool NegamaxAgent::overlaps(const Move& lhs, const Move& rhs)
{
    if (lhs.is_pass() || rhs.is_pass()) {
        return false;
    }
    return lhs.r1 <= rhs.r2 && lhs.r2 >= rhs.r1 && lhs.c1 <= rhs.c2 &&
           lhs.c2 >= rhs.c1;
}

int NegamaxAgent::move_swing(const State& state, const Move& move,
                             int player) const
{
    if (move.is_pass()) {
        return 0;
    }

    int swing = 0;
    for (int r = move.r1; r <= move.r2; ++r) {
        for (int c = move.c1; c <= move.c2; ++c) {
            const int owner = state.cell_owner(r, c);
            if (owner == 1 - player) {
                swing += 2;
            } else if (owner == -1) {
                swing += 1;
            }
        }
    }
    return swing;
}

std::vector<Move> NegamaxAgent::tactical_moves(
    const State& state, int player, std::optional<Move> last_move) const
{
    std::vector<Move> moves;
    if (!last_move.has_value() || last_move->is_pass()) {
        return moves;
    }

    for (const Move& move : state.legal_moves()) {
        if (move.is_pass() || !overlaps(move, *last_move)) {
            continue;
        }

        const int swing = move_swing(state, move, player);
        if (swing >= config_.tactical_swing_threshold) {
            moves.push_back(move);
        }
    }

    std::sort(moves.begin(), moves.end(), [&](const Move& lhs,
                                              const Move& rhs) {
        const int lhs_swing = move_swing(state, lhs, player);
        const int rhs_swing = move_swing(state, rhs, player);
        if (lhs_swing != rhs_swing) {
            return lhs_swing > rhs_swing;
        }
        const int lhs_area = (lhs.r2 - lhs.r1 + 1) * (lhs.c2 - lhs.c1 + 1);
        const int rhs_area = (rhs.r2 - rhs.r1 + 1) * (rhs.c2 - rhs.c1 + 1);
        return lhs_area > rhs_area;
    });

    if (static_cast<int>(moves.size()) > config_.tactical_move_limit) {
        moves.resize(config_.tactical_move_limit);
    }

    return moves;
}

uint64_t NegamaxAgent::search_key(const State& state,
                                  std::optional<Move> last_move) const
{
    uint64_t key = state.hash();
    if (!last_move.has_value()) {
        return key;
    }

    const Move& move = *last_move;
    uint64_t move_key = 0x9e3779b97f4a7c15ull;
    move_key ^= static_cast<uint64_t>(move.r1 + 2) * 0xbf58476d1ce4e5b9ull;
    move_key ^= static_cast<uint64_t>(move.c1 + 2) * 0x94d049bb133111ebull;
    move_key ^= static_cast<uint64_t>(move.r2 + 2) * 0xd6e8feb86659fd93ull;
    move_key ^= static_cast<uint64_t>(move.c2 + 2) * 0xa5a3564e27f3fb1full;
    return key ^ move_key;
}

float NegamaxAgent::quiescence(
    State& state, float alpha, float beta, int player,
    std::chrono::steady_clock::time_point deadline, bool& timed_out,
    int remaining_depth, std::optional<Move> last_move)
{
    if (timed_out || std::chrono::steady_clock::now() >= deadline) {
        timed_out = true;
        return 0.0f;
    }

    const float stand_pat = evaluator_->evaluate(state, player);
    if (remaining_depth <= 0 || state.is_terminal()) {
        return stand_pat;
    }
    if (stand_pat >= beta) {
        return stand_pat;
    }
    alpha = std::max(alpha, stand_pat);

    std::vector<Move> moves = tactical_moves(state, player, last_move);
    if (moves.empty()) {
        return stand_pat;
    }

    float best_value = stand_pat;
    for (const Move& move : moves) {
        state.apply(move);
        const float child_value =
            -quiescence(state, -beta, -alpha, 1 - player, deadline,
                        timed_out, remaining_depth - 1, move);
        state.undo();

        if (timed_out) {
            return 0.0f;
        }
        if (child_value > best_value) {
            best_value = child_value;
        }
        alpha = std::max(alpha, best_value);
        if (alpha >= beta) {
            break;
        }
    }

    return best_value;
}

std::pair<float, Move> NegamaxAgent::negamax(
    State& state, int depth, float alpha, float beta, int player,
    std::chrono::steady_clock::time_point deadline, bool& timed_out, int ply,
    bool is_root, std::optional<Move> last_move)
{
    if (timed_out || std::chrono::steady_clock::now() >= deadline) {
        timed_out = true;
        return std::make_pair(0.0f, PASS_MOVE);
    }

    if (state.is_terminal()) {
        return std::make_pair(evaluator_->evaluate(state, player), PASS_MOVE);
    }
    if (depth == 0) {
        return std::make_pair(
            quiescence(state, alpha, beta, player, deadline, timed_out,
                       config_.quiescence_depth, last_move),
            PASS_MOVE);
    }

    const float original_alpha = alpha;
    const float original_beta = beta;
    const uint64_t key = search_key(state, last_move);
    const TranspositionEntry* tt_entry = tt_.probe(key);
    if (tt_entry != nullptr && tt_entry->depth >= depth) {
        if (tt_entry->bound == BoundType::Exact) {
            return std::make_pair(tt_entry->value, tt_entry->best_move);
        }
        if (tt_entry->bound == BoundType::Lower) {
            alpha = std::max(alpha, tt_entry->value);
        } else if (tt_entry->bound == BoundType::Upper) {
            beta = std::min(beta, tt_entry->value);
        }
        if (alpha >= beta) {
            return std::make_pair(tt_entry->value, tt_entry->best_move);
        }
    }

    auto moves = state.legal_moves();
    order_moves(state, moves,
                tt_entry != nullptr ? std::optional<Move>(tt_entry->best_move)
                                    : std::nullopt,
                depth, ply, is_root, last_move);

    float value = -INF;
    Move best_move = PASS_MOVE;
    for (auto move : moves) {
        state.apply(move);
        float child_value = -negamax(state, depth - 1, -beta, -alpha,
                                     1 - player, deadline, timed_out, ply + 1,
                                     false, move)
                                 .first;
        state.undo();
        if (timed_out) {
            return std::make_pair(0.0f, PASS_MOVE);
        }
        if (child_value > value) {
            value = child_value;
            best_move = move;
        }
        alpha = std::max(alpha, value);
        if (alpha >= beta) {
            break;
        }
    }

    BoundType bound = BoundType::Exact;
    if (value <= original_alpha) {
        bound = BoundType::Upper;
    } else if (value >= original_beta) {
        bound = BoundType::Lower;
    }
    tt_.store(key, depth, value, best_move, bound);
    return std::make_pair(value, best_move);
}

std::pair<float, Move> NegamaxAgent::iterative_deepening(State& state,
                                                         int max_depth,
                                                         int time_budget_ms)
{
    const int safe_budget_ms = std::max(1, time_budget_ms);
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(safe_budget_ms);

    std::pair<float, Move> best_completed = std::make_pair(-INF, PASS_MOVE);
    for (int depth = 1; depth <= max_depth; ++depth) {
        bool timed_out = false;
        auto result = negamax(state, depth, -INF, INF, state.current_player(),
                              deadline, timed_out, 0, true, std::nullopt);
        if (timed_out) {
            break;
        }
        best_completed = result;
    }
    return best_completed;
}
