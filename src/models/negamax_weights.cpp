#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <array>
#include <fstream>
#include <memory>
#include <vector>

#include "../negamax_agent.h"

class WeightedDiffEvaluator final : public Evaluator {
public: 
    explicit WeightedDiffEvaluator(std::array<float, R * C> weight_map)
        : weight_map_(weight_map)
    {
    }

    float evaluate(const State& state, int player) const override;

private:
    std::array<float, R * C> weight_map_;
};

float WeightedDiffEvaluator::evaluate(const State& state, int player) const
{
    float value = 0.0f;
    if(state.is_terminal() || state.undo_depth() > 15)
    {
        auto scores = state.scores();
        value = scores.first - scores.second;
    }
    else {
        for (int r = 0; r < state.rows(); ++r) {
            for (int c = 0; c < state.cols(); ++c) {
                const int owner = state.cell_owner(r, c);
                if (owner == -1) {
                    continue;
                }

                const float weight = weight_map_[r * C + c];
                value += owner == 0 ? weight : -weight;
            }
        }
    }


    if (player == 1) {
        value = -value;
    }
    return value;
}

std::array<float, R * C> load_weight_map(const char* path)
{
    std::ifstream input(path);
    if (!input) {
        fprintf(stderr, "Failed to open weight map: %s\n", path);
        exit(1);
    }

    std::array<float, R * C> weight_map{};
    for (float& weight : weight_map) {
        if (!(input >> weight)) {
            fprintf(stderr, "Weight map must contain %d floats\n", R * C);
            exit(1);
        }
    }

    float extra;
    if (input >> extra) {
        fprintf(stderr, "Weight map contains more than %d floats\n", R * C);
        exit(1);
    }

    return weight_map;
}

int main(int argc, char* argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <weight_map.txt>\n", argv[0]);
        return 1;
    }

    std::array<float, R * C> weight_map = load_weight_map(argv[1]);
    std::unique_ptr<Agent> ai = std::make_unique<NegamaxAgent>(
        SearchConfig{},
        std::make_unique<WeightedDiffEvaluator>(weight_map));

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
            ai->set_side(strcmp(turn, "FIRST") == 0);
            printf("OK\n");
            fflush(stdout);
            continue;
        }

        if (strcmp(command, "INIT") == 0)
        {
            char *token = strtok(line, " ");
            std::vector<std::vector<int>> board;
            for (int i = 0; i < R; i++)
            {
                std::vector<int> temp;
                token = strtok(NULL, " ");
                for (int j = 0; j < C; j++) temp.push_back(token[j] - '0');
                board.push_back(temp);
            }
            ai->initialize(board);
            continue;
        }

        if (strcmp(command, "TIME") == 0)
        {
            // My turn: calculate and execute move
            int my_time, opp_time;
            sscanf(line, "%*s %d %d", &my_time, &opp_time);
            Move best_move = ai->choose_move(my_time, opp_time);

            auto [r1, c1, r2, c2] = best_move;
            printf("%d %d %d %d\n", r1, c1, r2, c2);
            fflush(stdout);
            continue;
        }

        if (strcmp(command, "OPP") == 0)
        {
            // Apply opponent's turn
            int r1, c1, r2, c2, time;
            sscanf(line, "%*s %d %d %d %d %d", &r1, &c1, &r2, &c2, &time);
            Move move = {r1, c1, r2, c2};
            ai->apply_opponent_move(move);
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
