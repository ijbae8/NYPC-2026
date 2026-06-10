#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <memory>
#include <vector>

#include "../negamax_agent.h"

int main()
{
    std::unique_ptr<Agent> ai = std::make_unique<NegamaxAgent>(
        SearchConfig{},
        std::make_unique<ScoreDiffEvaluator>(),
        std::make_unique<AreaMovePrioritizer>(),
        std::make_unique<PhaseAwareTimeBudgeter>());

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
