#include "../src/state.h"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                     \
            std::cerr << "CHECK failed at " << __FILE__ << ':' << __LINE__     \
                      << ": " #condition << '\n';                             \
            std::exit(1);                                                       \
        }                                                                      \
    } while (false)

namespace {

std::string move_to_string(const Move& move)
{
    if (move.is_pass()) {
        return "PASS";
    }

    std::ostringstream out;
    out << '(' << move.r1 << ',' << move.c1 << ")-(" << move.r2 << ','
        << move.c2 << ')';
    return out.str();
}

std::string moves_to_string(const std::vector<Move>& moves)
{
    std::ostringstream out;
    for (size_t i = 0; i < moves.size(); ++i) {
        if (i != 0) {
            out << ' ';
        }
        out << move_to_string(moves[i]);
    }
    return out.str();
}

bool contains_move(const std::vector<Move>& moves, const Move& target)
{
    for (const Move& move : moves) {
        if (move == target) {
            return true;
        }
    }
    return false;
}

void check_no_duplicate_moves(const std::vector<Move>& moves)
{
    for (size_t i = 0; i < moves.size(); ++i) {
        for (size_t j = i + 1; j < moves.size(); ++j) {
            if (moves[i] == moves[j]) {
                std::cerr << "Duplicate move: " << move_to_string(moves[i])
                          << "\nin: " << moves_to_string(moves) << '\n';
                std::exit(1);
            }
        }
    }
}

void check_moves_equal(const std::vector<Move>& actual,
                       const std::vector<Move>& expected)
{
    if (actual != expected) {
        std::cerr << "Move vectors differ\nexpected: "
                  << moves_to_string(expected) << "\nactual:   "
                  << moves_to_string(actual) << '\n';
        std::exit(1);
    }
}

struct Snapshot {
    std::string text;
    uint64_t hash;
    std::pair<int, int> scores;
    int current_player;
    int undo_depth;
    bool terminal;
    std::vector<Move> legal_moves;
};

Snapshot snapshot(const State& state)
{
    check_no_duplicate_moves(state.legal_moves());
    return {state.to_string(),
            state.hash(),
            state.scores(),
            state.current_player(),
            state.undo_depth(),
            state.is_terminal(),
            state.legal_moves()};
}

void check_restored(const State& state, const Snapshot& before)
{
    CHECK(state.to_string() == before.text);
    CHECK(state.hash() == before.hash);
    CHECK(state.scores() == before.scores);
    CHECK(state.current_player() == before.current_player);
    CHECK(state.undo_depth() == before.undo_depth);
    CHECK(state.is_terminal() == before.terminal);
    check_moves_equal(state.legal_moves(), before.legal_moves);
}

void test_newly_emerging_move_after_apply()
{
    State state(3, 3, {{1, 1, 1}, {1, 10, 1}, {1, 3, 1}});

    const Move center{1, 1, 1, 1};
    const Move full_board{0, 0, 2, 2};
    const Snapshot before = snapshot(state);

    CHECK(state.is_legal(center));
    CHECK(!contains_move(before.legal_moves, full_board));

    state.apply(center);

    CHECK(state.current_player() == 1);
    CHECK(state.cell_value(1, 1) == 0);
    CHECK(state.cell_owner(1, 1) == 0);
    CHECK(state.is_legal(full_board));
    CHECK(contains_move(state.legal_moves(), full_board));

    state.undo();
    check_restored(state, before);
}

void test_apply_updates_cells_and_undo_restores()
{
    State state(2, 3, {{1, 2, 3}, {4, 0, 0}});
    const Move full{0, 0, 1, 2};
    const Snapshot before = snapshot(state);

    CHECK(state.is_legal(full));

    state.apply(full);

    CHECK(state.current_player() == 1);
    CHECK(state.scores() == std::make_pair(6, 0));
    for (int r = 0; r < state.rows(); ++r) {
        for (int c = 0; c < state.cols(); ++c) {
            CHECK(state.cell_value(r, c) == 0);
            CHECK(state.cell_owner(r, c) == 0);
        }
    }

    check_moves_equal(state.legal_moves(), std::vector<Move>{PASS_MOVE});

    state.undo();
    check_restored(state, before);
}

void test_pass_terminal_and_undo_restore()
{
    State state(1, 3, {{1, 2, 3}});
    const Snapshot before = snapshot(state);

    state.apply(PASS_MOVE);
    CHECK(state.current_player() == 1);
    CHECK(!state.is_terminal());
    CHECK(state.undo_depth() == 1);

    state.apply(PASS_MOVE);
    CHECK(state.current_player() == 0);
    CHECK(state.is_terminal());
    CHECK(state.undo_depth() == 2);

    state.undo();
    CHECK(state.current_player() == 1);
    CHECK(!state.is_terminal());
    CHECK(state.undo_depth() == 1);

    state.undo();
    check_restored(state, before);
}

void dfs_stability(State& state, int depth, int& visited_nodes)
{
    const Snapshot before = snapshot(state);
    ++visited_nodes;

    if (depth == 0 || state.is_terminal()) {
        return;
    }

    for (const Move& move : before.legal_moves) {
        State clone = state.clone();

        state.apply(move);
        clone.apply(move);

        CHECK(state == clone);
        CHECK(state.hash() == clone.hash());
        check_moves_equal(state.legal_moves(), clone.legal_moves());

        dfs_stability(state, depth - 1, visited_nodes);

        state.undo();
        check_restored(state, before);
    }
}

void test_deep_tree_apply_generate_undo_stability()
{
    State state(4,
                4,
                {{1, 2, 7, 1},
                 {3, 4, 1, 2},
                 {6, 1, 2, 1},
                 {1, 5, 1, 3}});

    int visited_nodes = 0;
    dfs_stability(state, 6, visited_nodes);

    CHECK(visited_nodes > 50);
}

void test_emerging_move_inside_deep_tree()
{
    State state(3, 3, {{1, 1, 1}, {1, 10, 1}, {1, 3, 1}});
    const Move center{1, 1, 1, 1};
    const Move full_board{0, 0, 2, 2};

    state.apply(center);
    CHECK(contains_move(state.legal_moves(), full_board));

    state.apply(full_board);
    CHECK(state.scores() == std::make_pair(0, 9));
    check_moves_equal(state.legal_moves(), std::vector<Move>{PASS_MOVE});

    state.undo();
    CHECK(contains_move(state.legal_moves(), full_board));

    state.undo();
    CHECK(!contains_move(state.legal_moves(), full_board));
}

}  // namespace

int main()
{
    test_newly_emerging_move_after_apply();
    test_apply_updates_cells_and_undo_restores();
    test_pass_terminal_and_undo_restore();
    test_deep_tree_apply_generate_undo_stability();
    test_emerging_move_inside_deep_tree();

    std::cout << "All state tests passed\n";
    return 0;
}
