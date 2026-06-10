#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "agent.h"
#include "tt.h"

#define R 10
#define C 17

constexpr float INF = 1000000.0f;

struct SearchConfig {
    int rows = R;
    int cols = C;
    int max_depth = 64;
    int quiescence_depth = 8;
    int tactical_swing_threshold = 2;
    int tactical_move_limit = 12;
    size_t tt_capacity = TT_CAPACITY;
};

class Evaluator {
public:
    virtual ~Evaluator() = default;
    virtual float evaluate(const State& state, int player) const = 0;
};

class MovePrioritizer {
public:
    virtual ~MovePrioritizer() = default;
    virtual float priority(const State& state, const Move& move,
                           std::optional<Move> tt_best_move, int depth,
                           int ply, bool is_root) const = 0;
};

class TimeBudgeter {
public:
    virtual ~TimeBudgeter() = default;
    virtual int compute_ms(int my_time_ms, int current_ply,
                           int legal_move_count) const = 0;
};

class ScoreDiffEvaluator final : public Evaluator {
public:
    float evaluate(const State& state, int player) const override;
};

class AreaMovePrioritizer final : public MovePrioritizer {
public:
    float priority(const State& state, const Move& move,
                   std::optional<Move> tt_best_move, int depth, int ply,
                   bool is_root) const override;
};

class AdaptiveTimeBudgeter final : public TimeBudgeter {
public:
    AdaptiveTimeBudgeter(int expected_game_plies = 38,
                         int min_remaining_plies = 6,
                         int clock_reserve_ms = 150,
                         int min_move_budget_ms = 40,
                         int max_move_budget_ms = 700,
                         int time_buffer_ms = 20,
                         int heavy_branch_threshold = 60,
                         int medium_branch_threshold = 30,
                         int light_branch_threshold = 8);

    int compute_ms(int my_time_ms, int current_ply,
                   int legal_move_count) const override;

private:
    int expected_game_plies_;
    int min_remaining_plies_;
    int clock_reserve_ms_;
    int min_move_budget_ms_;
    int max_move_budget_ms_;
    int time_buffer_ms_;
    int heavy_branch_threshold_;
    int medium_branch_threshold_;
    int light_branch_threshold_;
};

class NegamaxAgent : public Agent {
public:
    explicit NegamaxAgent(
        SearchConfig config = {},
        std::unique_ptr<Evaluator> evaluator =
            std::make_unique<ScoreDiffEvaluator>(),
        std::unique_ptr<MovePrioritizer> move_prioritizer =
            std::make_unique<AreaMovePrioritizer>(),
        std::unique_ptr<TimeBudgeter> time_budgeter =
            std::make_unique<AdaptiveTimeBudgeter>());

    void set_side(bool plays_first) override;
    void initialize(const std::vector<std::vector<int>>& board) override;
    Move choose_move(int my_time_ms, int opp_time_ms) override;
    void apply_opponent_move(Move move) override;

private:
    void ensure_initialized() const;
    void order_moves(const State& state, std::vector<Move>& moves,
                     std::optional<Move> tt_best_move, int depth, int ply,
                     bool is_root, std::optional<Move> last_move) const;
    float quiescence(State& state, float alpha, float beta, int player,
                     std::chrono::steady_clock::time_point deadline,
                     bool& timed_out, int remaining_depth,
                     std::optional<Move> last_move);
    std::pair<float, Move> negamax(
        State& state, int depth, float alpha, float beta, int player,
        std::chrono::steady_clock::time_point deadline, bool& timed_out,
        int ply = 0, bool is_root = false,
        std::optional<Move> last_move = std::nullopt);
    std::pair<float, Move> iterative_deepening(State& state, int max_depth,
                                               int time_budget_ms);
    std::vector<Move> tactical_moves(const State& state, int player,
                                     std::optional<Move> last_move) const;
    int move_swing(const State& state, const Move& move, int player) const;
    uint64_t search_key(const State& state,
                        std::optional<Move> last_move) const;
    static bool overlaps(const Move& lhs, const Move& rhs);

    SearchConfig config_;
    std::unique_ptr<Evaluator> evaluator_;
    std::unique_ptr<MovePrioritizer> move_prioritizer_;
    std::unique_ptr<TimeBudgeter> time_budgeter_;
    int my_side_;
    std::optional<State> state_;
    TranspositionTable tt_;
};
