#include "negamax_agent.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

#ifdef NEGAMAX_LOGGING
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#endif

#ifdef NEGAMAX_LOGGING
namespace {

std::string square_name(int row, int col)
{
    std::string file;
    int value = col;
    do {
        file.insert(file.begin(), static_cast<char>('a' + (value % 26)));
        value = value / 26 - 1;
    } while (value >= 0);

    return file + std::to_string(row + 1);
}

std::string move_name(const Move& move)
{
    if (move.is_pass()) {
        return "pass";
    }
    return square_name(move.r1, move.c1) + square_name(move.r2, move.c2);
}

std::string pv_name(const State& root, const std::vector<Move>& pv)
{
    std::ostringstream out;
    int player = root.current_player();
    for (size_t i = 0; i < pv.size(); ++i) {
        if (i > 0) {
            out << ' ';
        }
        out << (player == 0 ? "FIRST:" : "SECOND:") << move_name(pv[i]);
        player = 1 - player;
    }
    return out.str();
}

std::string join_ints(const std::vector<int>& values)
{
    std::ostringstream out;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << ',';
        }
        out << values[i];
    }
    return out.str();
}

std::string join_long_longs(const std::vector<long long>& values)
{
    std::ostringstream out;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << ',';
        }
        out << values[i];
    }
    return out.str();
}

}  // namespace
#endif

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
    if(move.is_pass()) return 1000.0;

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

PhaseAwareTimeBudgeter::PhaseAwareTimeBudgeter(
    int expected_game_plies, int min_remaining_my_turns, int clock_reserve_ms,
    int min_move_budget_ms, int max_move_budget_ms, int time_buffer_ms,
    int opening_ply_limit, int endgame_ply_start)
    : expected_game_plies_(expected_game_plies),
      min_remaining_my_turns_(min_remaining_my_turns),
      clock_reserve_ms_(clock_reserve_ms),
      min_move_budget_ms_(min_move_budget_ms),
      max_move_budget_ms_(max_move_budget_ms),
      time_buffer_ms_(time_buffer_ms),
      opening_ply_limit_(opening_ply_limit),
      endgame_ply_start_(endgame_ply_start)
{
}

int PhaseAwareTimeBudgeter::compute_ms(int my_time_ms, int current_ply,
                                       int legal_move_count) const
{
    const int usable_time_ms = std::max(1, my_time_ms - clock_reserve_ms_);
    const int remaining_plies = std::max(0, expected_game_plies_ - current_ply);
    const int remaining_my_turns = std::max(min_remaining_my_turns_, (remaining_plies + 1) / 2);

    double multiplier = 1.0;

    if (current_ply < opening_ply_limit_) {
        multiplier *= 0.5;
    } else if (current_ply >= endgame_ply_start_) {
        multiplier *= 0.75;
    } else {
        multiplier *= 1.8;
    }

    if (legal_move_count > 45) {
        multiplier *= 1.60;
    } else if (legal_move_count > 30) {
        multiplier *= 1.40;
    } else if (legal_move_count > 14) {
        multiplier *= 1.20;
    } else if (legal_move_count >= 8) {
        multiplier *= 0.9;
    } else {
        multiplier *= 0.65;
    }

    if (my_time_ms < 1500) {
        multiplier *= 0.65;
    } else if (my_time_ms < 3000) {
        multiplier *= 0.8;
    }

    int budget = static_cast<int>(
        static_cast<double>(usable_time_ms) / remaining_my_turns * multiplier);
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
#ifdef NEGAMAX_LOGGING
      ,
      move_number_(0),
      current_search_stats_()
#endif
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
#ifdef NEGAMAX_LOGGING
    move_number_ = 0;
    current_search_stats_ = SearchStats{};
#endif
}

Move NegamaxAgent::choose_move(int my_time_ms, int /*opp_time_ms*/)
{
    ensure_initialized();

#ifdef NEGAMAX_LOGGING
    ++move_number_;
#endif

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
                               int ply, bool is_root) const
{
    std::sort(moves.begin(), moves.end(),
              [&](const Move& lhs, const Move& rhs) {
                  return move_prioritizer_->priority(
                             state, lhs, tt_best_move, depth, ply, is_root) >
                         move_prioritizer_->priority(
                             state, rhs, tt_best_move, depth, ply, is_root);
              });

    if (tt_best_move.has_value()) {
        auto tt_move = std::find(moves.begin(), moves.end(), *tt_best_move);
        if (tt_move != moves.end()) {
            std::iter_swap(moves.begin(), tt_move);
        }
    }
}

std::pair<float, Move> NegamaxAgent::negamax(
    State& state, int depth, float alpha, float beta, int player,
    std::chrono::steady_clock::time_point deadline, bool& timed_out,
    bool& reached_depth_limit, int ply, bool is_root)
{
    if (timed_out || std::chrono::steady_clock::now() >= deadline) {
        timed_out = true;
        return std::make_pair(0.0f, PASS_MOVE);
    }

#ifdef NEGAMAX_LOGGING
    ++current_search_stats_.nodes;
#endif

    if (state.is_terminal()) {
        return std::make_pair(evaluator_->evaluate(state, player), PASS_MOVE);
    }
    if (depth == 0) {
        reached_depth_limit = true;
        return std::make_pair(evaluator_->evaluate(state, player), PASS_MOVE);
    }

    const float original_alpha = alpha;
    const float original_beta = beta;
    const uint64_t key = state.hash();
#ifdef NEGAMAX_LOGGING
    ++current_search_stats_.tt_probes;
#endif
    const TranspositionEntry* tt_entry = tt_.probe(key);
#ifdef NEGAMAX_LOGGING
    if (tt_entry != nullptr) {
        ++current_search_stats_.tt_hits;
    }
#endif
    if (tt_entry != nullptr && tt_entry->depth >= depth) {
        if (tt_entry->bound == BoundType::Exact) {
#ifdef NEGAMAX_LOGGING
            ++current_search_stats_.tt_cutoffs;
#endif
            reached_depth_limit = reached_depth_limit ||
                                  tt_entry->reached_depth_limit;
            return std::make_pair(tt_entry->value, tt_entry->best_move);
        }
        if (tt_entry->bound == BoundType::Lower) {
            alpha = std::max(alpha, tt_entry->value);
        } else if (tt_entry->bound == BoundType::Upper) {
            beta = std::min(beta, tt_entry->value);
        }
        if (alpha >= beta) {
#ifdef NEGAMAX_LOGGING
            ++current_search_stats_.tt_cutoffs;
#endif
            reached_depth_limit = reached_depth_limit ||
                                  tt_entry->reached_depth_limit;
            return std::make_pair(tt_entry->value, tt_entry->best_move);
        }
    }

    auto moves = state.legal_moves();
    order_moves(state, moves,
                tt_entry != nullptr ? std::optional<Move>(tt_entry->best_move)
                                    : std::nullopt,
                depth, ply, is_root);

    float value = -INF;
    Move best_move = PASS_MOVE;
    bool subtree_reached_depth_limit = false;
    for (size_t move_index = 0; move_index < moves.size(); ++move_index) {
        auto move = moves[move_index];
        bool child_reached_depth_limit = false;
        state.apply(move);
        float child_value = -negamax(state, depth - 1, -beta, -alpha,
                                     1 - player, deadline, timed_out,
                                     child_reached_depth_limit, ply + 1, false)
                                 .first;
        state.undo();
        if (timed_out) {
            return std::make_pair(0.0f, PASS_MOVE);
        }
        subtree_reached_depth_limit =
            subtree_reached_depth_limit || child_reached_depth_limit;
        if (child_value > value) {
            value = child_value;
            best_move = move;
        }
#ifdef NEGAMAX_LOGGING
        const float previous_alpha = alpha;
#endif
        alpha = std::max(alpha, value);
        if (alpha >= beta) {
#ifdef NEGAMAX_LOGGING
            ++current_search_stats_.beta_cutoffs;
            current_search_stats_.beta_cutoff_index_sum +=
                static_cast<long long>(move_index);
            if (move_index == 0) {
                ++current_search_stats_.first_move_beta_cutoffs;
            }
#endif
            break;
        }
#ifdef NEGAMAX_LOGGING
        if (alpha > previous_alpha) {
            ++current_search_stats_.alpha_raises;
        }
#endif
    }

    BoundType bound = BoundType::Exact;
    if (value <= original_alpha) {
        bound = BoundType::Upper;
    } else if (value >= original_beta) {
        bound = BoundType::Lower;
    }
    reached_depth_limit =
        reached_depth_limit || subtree_reached_depth_limit;
    tt_.store(key, depth, value, best_move, bound, subtree_reached_depth_limit);
    return std::make_pair(value, best_move);
}

#ifdef NEGAMAX_LOGGING
std::vector<Move> NegamaxAgent::principal_variation(const State& state,
                                                    int depth) const
{
    State pv_state = state.clone();
    std::vector<Move> pv;
    pv.reserve(static_cast<size_t>(depth));

    for (int ply = 0; ply < depth && !pv_state.is_terminal(); ++ply) {
        const TranspositionEntry* entry = tt_.probe(pv_state.hash());
        if (entry == nullptr || !pv_state.is_legal(entry->best_move)) {
            break;
        }
        pv.push_back(entry->best_move);
        pv_state.apply(entry->best_move);
    }

    return pv;
}
#endif

bool NegamaxAgent::should_try_next_depth(int remaining_ms,
                                         int last_depth_time_ms,
                                         int previous_depth_time_ms,
                                         int legal_move_count) const
{
    if (remaining_ms <= 10) {
        return false;
    }
    if (last_depth_time_ms <= 0) {
        return true;
    }

    double growth = 1.0;
    if (previous_depth_time_ms > 0) {
        growth = static_cast<double>(last_depth_time_ms) /
                 static_cast<double>(previous_depth_time_ms);
        growth = std::clamp(growth, 1.5, 4.8);
    } else if (legal_move_count > 30) {
        growth = 3.0;
    } else if (legal_move_count > 10) {
        growth = 2.4;
    } else {
        growth = 1.6;
    }

    const double estimated_next_ms =
        static_cast<double>(last_depth_time_ms) * growth;
    return static_cast<double>(remaining_ms) >= estimated_next_ms * 1.15 + 8.0;
}

std::pair<float, Move> NegamaxAgent::iterative_deepening(State& state,
                                                         int max_depth,
                                                         int time_budget_ms)
{
    const int safe_budget_ms = std::max(1, time_budget_ms);
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(safe_budget_ms);

    std::pair<float, Move> best_completed = std::make_pair(-INF, PASS_MOVE);
    const int root_legal_move_count = static_cast<int>(state.legal_moves().size());
    int previous_depth_time_ms = 0;
    int last_depth_time_ms = 0;
#ifdef NEGAMAX_LOGGING
    SearchStats last_stats;
    std::vector<Move> last_pv;
    const auto search_start = std::chrono::steady_clock::now();
    std::vector<int> completed_depths;
    std::vector<int> depth_times_ms;
    std::vector<long long> depth_nodes;
    std::vector<long long> depth_nps;
    int last_depth = 0;
    long long last_nps = 0;
#endif
    for (int depth = 1; depth <= max_depth; ++depth) {
        bool timed_out = false;
        bool reached_depth_limit = false;
        const auto depth_start = std::chrono::steady_clock::now();
#ifdef NEGAMAX_LOGGING
        current_search_stats_ = SearchStats{};
#endif
        auto result = negamax(state, depth, -INF, INF, state.current_player(),
                              deadline, timed_out, reached_depth_limit, 0,
                              true);
        const auto depth_end = std::chrono::steady_clock::now();
        if (timed_out) {
            break;
        }
        best_completed = result;
        const auto elapsed = depth_end - depth_start;
        previous_depth_time_ms = last_depth_time_ms;
        last_depth_time_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed)
                .count());
#ifdef NEGAMAX_LOGGING
        last_stats = current_search_stats_;
        last_depth = depth;
        const double elapsed_seconds =
            std::chrono::duration<double>(elapsed).count();
        last_nps =
            elapsed_seconds > 0.0
                ? static_cast<long long>(current_search_stats_.nodes /
                                         elapsed_seconds)
                : 0;
        completed_depths.push_back(depth);
        depth_times_ms.push_back(last_depth_time_ms);
        depth_nodes.push_back(last_stats.nodes);
        depth_nps.push_back(last_nps);
        last_pv = principal_variation(state, depth);
#endif
        if (!reached_depth_limit) {
            break;
        }
        const int remaining_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now())
                .count());
        if (!should_try_next_depth(remaining_ms, last_depth_time_ms,
                                   previous_depth_time_ms,
                                   root_legal_move_count)) {
            break;
        }
    }
#ifdef NEGAMAX_LOGGING
    if (last_depth > 0) {
        const int used_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - search_start)
                .count());
        const double fm_beta_rate =
            last_stats.beta_cutoffs > 0
                ? 100.0 * last_stats.first_move_beta_cutoffs /
                      last_stats.beta_cutoffs
                : 0.0;
        const double avg_beta_idx =
            last_stats.beta_cutoffs > 0
                ? static_cast<double>(last_stats.beta_cutoff_index_sum) /
                      last_stats.beta_cutoffs
                : 0.0;

        std::cerr << "SEARCH move=" << move_number_ << " depth=" << last_depth
                  << " score=" << best_completed.first
                  << " best=" << move_name(best_completed.second)
                  << " nodes=" << last_stats.nodes
                  << " assigned_ms=" << safe_budget_ms
                  << " used_ms=" << used_ms
                  << " depth_time_ms=" << last_depth_time_ms
                  << " nps=" << last_nps << '\n';
        std::cerr << "TT probes=" << last_stats.tt_probes
                  << " hits=" << last_stats.tt_hits
                  << " cutoffs=" << last_stats.tt_cutoffs << '\n';
        std::cerr << "AB beta=" << last_stats.beta_cutoffs
                  << " fm_beta=" << last_stats.first_move_beta_cutoffs
                  << " fm_beta_rate=" << std::fixed << std::setprecision(1)
                  << fm_beta_rate << "% avg_beta_idx=" << std::setprecision(2)
                  << avg_beta_idx
                  << " alpha_raises=" << last_stats.alpha_raises
                  << std::defaultfloat << '\n';
        std::cerr << "PV move=" << move_number_ << " depth=" << last_depth
                  << " line=" << pv_name(state, last_pv) << '\n';
        std::cerr << "ITER depths=" << join_ints(completed_depths)
                  << " times_ms=" << join_ints(depth_times_ms)
                  << " nodes=" << join_long_longs(depth_nodes)
                  << " nps=" << join_long_longs(depth_nps) << '\n';
    }
#endif
    return best_completed;
}
