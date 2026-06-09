#include <iostream>
#include <vector>
#include <string>
#include <sstream>

using namespace std;

// Class that manages game state
class Game
{
private:
    vector<vector<int>> board;   // Remaining numbers
    vector<vector<int>> owner;   // 0 = none, 1 = mine, 2 = opponent
    bool first;
    bool passed;

public:
    Game() = default;

    Game(const vector<vector<int>> &board, bool first)
        : board(board), first(first), passed(false)
    {
        owner.assign(
            board.size(),
            vector<int>(board[0].size(), 0)
        );
    }

    // Checks if rectangle (r1, c1) ~ (r2, c2) is valid
    bool isValid(int r1, int c1, int r2, int c2)
    {
        int sums = 0;
        bool r1fit = false;
        bool r2fit = false;
        bool c1fit = false;
        bool c2fit = false;

        for (int r = r1; r <= r2; r++)
        {
            for (int c = c1; c <= c2; c++)
            {
                if (board[r][c] != 0)
                {
                    sums += board[r][c];

                    if (r == r1) r1fit = true;
                    if (r == r2) r2fit = true;
                    if (c == c1) c1fit = true;
                    if (c == c2) c2fit = true;
                }
            }
        }

        return sums == 10 &&
               r1fit &&
               r2fit &&
               c1fit &&
               c2fit;
    }

    // Choose move maximizing territory gain
    vector<int> calculateMove(int myTime, int oppTime)
    {
        int myScore = 0;
        int enemyScore = 0;
        for(int r=0; r<owner.size(); r++)
        {
            for(int c=0; c<owner[0].size(); c++)
            {
                if(owner[r][c] == 1) myScore++;
                else if(owner[r][c] == 2) enemyScore++;
            }
        }
        if(passed)
        {
            if(myScore > enemyScore)
                return {-1,-1,-1,-1}; // 나도 패스
        }
       
        int R = board.size();
        int C = board[0].size();

        vector<vector<int>> ps(
            R + 1,
            vector<int>(C + 1, 0)
        );

        for (int r = 0; r < R; r++)
        {
            for (int c = 0; c < C; c++)
            {
                ps[r + 1][c + 1] =
                    ps[r][c + 1]
                    + ps[r + 1][c]
                    - ps[r][c]
                    + board[r][c];
            }
        }

        auto getSum =
            [&](const vector<vector<int>>& b,
                int r1,int c1,int r2,int c2)
        {
            vector<vector<int>> p(
                R + 1,
                vector<int>(C + 1, 0)
            );

            for(int r=0;r<R;r++)
            {
                for(int c=0;c<C;c++)
                {
                    p[r+1][c+1] =
                        p[r][c+1]
                        + p[r+1][c]
                        - p[r][c]
                        + b[r][c];
                }
            }

            return p[r2+1][c2+1]
                - p[r1][c2+1]
                - p[r2+1][c1]
                + p[r1][c1];
        };

        int bestScore = -1000000000;
        int bestArea = -1;

        vector<int> bestMove = {-1,-1,-1,-1};

        for(int r1=0;r1<R;r1++)
        {
            for(int c1=0;c1<C;c1++)
            {
                for(int r2=r1;r2<R;r2++)
                {
                    for(int c2=c1;c2<C;c2++)
                    {
                        if(ps[r2+1][c2+1]
                        - ps[r1][c2+1]
                        - ps[r2+1][c1]
                        + ps[r1][c1] != 10)
                            continue;

                        if(!isValid(r1,c1,r2,c2))
                            continue;

                        int gain = 0;

                        for(int r=r1;r<=r2;r++)
                        {
                            for(int c=c1;c<=c2;c++)
                            {
                                if(owner[r][c] == 0)
                                    gain += 1;
                                else if(owner[r][c] == 2)
                                    gain += 2;
                            }
                        }

                        auto tempBoard = board;
                        auto tempOwner = owner;

                        for(int r=r1;r<=r2;r++)
                        {
                            for(int c=c1;c<=c2;c++)
                            {
                                tempBoard[r][c] = 0;
                                tempOwner[r][c] = 1;
                            }
                        }

                        int bestCounter = 0;

                        for(int rr1=0;rr1<R;rr1++)
                        {
                            for(int cc1=0;cc1<C;cc1++)
                            {
                                for(int rr2=rr1;rr2<R;rr2++)
                                {
                                    for(int cc2=cc1;cc2<C;cc2++)
                                    {
                                        bool overlap =
                                            !(rr2 < r1 ||
                                            rr1 > r2 ||
                                            cc2 < c1 ||
                                            cc1 > c2);

                                        if(!overlap)
                                            continue;

                                        int sum = 0;
                                        bool top=false;
                                        bool bottom=false;
                                        bool left=false;
                                        bool right=false;

                                        for(int r=rr1;r<=rr2;r++)
                                        {
                                            for(int c=cc1;c<=cc2;c++)
                                            {
                                                if(tempBoard[r][c] != 0)
                                                {
                                                    sum += tempBoard[r][c];

                                                    if(r==rr1) top=true;
                                                    if(r==rr2) bottom=true;
                                                    if(c==cc1) left=true;
                                                    if(c==cc2) right=true;
                                                }
                                            }
                                        }

                                        if(sum != 10)
                                            continue;

                                        if(!(top&&bottom&&left&&right))
                                            continue;

                                        int counterGain = 0;

                                        for(int r=rr1;r<=rr2;r++)
                                        {
                                            for(int c=cc1;c<=cc2;c++)
                                            {
                                                if(tempOwner[r][c] == 1)
                                                    counterGain += 2;
                                                else if(tempOwner[r][c] == 0)
                                                    counterGain += 1;
                                            }
                                        }

                                        bestCounter =
                                            max(bestCounter,
                                                counterGain);
                                    }
                                }
                            }
                        }

                        int score =
                            gain - bestCounter;

                        int area =
                            (r2-r1+1) *
                            (c2-c1+1);

                        if(score > bestScore ||
                        (score == bestScore &&
                        area > bestArea))
                        {
                            bestScore = score;
                            bestArea = area;
                            bestMove = {r1,c1,r2,c2};
                        }
                    }
                }
            }
        }
        if (bestScore != -1000000000 && bestScore < 0 && myScore > enemyScore)
            return {-1, -1, -1, -1};
        return bestMove;
    }

    // Apply opponent move
    void updateOpponentAction(
        const vector<int> &action,
        int time)
    {
        updateMove(
            action[0],
            action[1],
            action[2],
            action[3],
            false
        );
    }

    // Apply move and update territory
    void updateMove(
        int r1,
        int c1,
        int r2,
        int c2,
        bool isMyMove)
    {
        if (r1 == -1 &&
            c1 == -1 &&
            r2 == -1 &&
            c2 == -1)
        {
            passed = true;
            return;
        }

        int newOwner = isMyMove ? 1 : 2;

        for (int r = r1; r <= r2; r++)
        {
            for (int c = c1; c <= c2; c++)
            {
                board[r][c] = 0;
                owner[r][c] = newOwner;
            }
        }

        passed = false;
    }
};

// Main function
int main()
{
    Game game;
    bool first = false;

    while (true)
    {
        string line;
        getline(cin, line);

        istringstream iss(line);
        string command;

        if (!(iss >> command))
            continue;

        if (command == "READY")
        {
            string turn;
            iss >> turn;

            first = (turn == "FIRST");

            cout << "OK" << endl;
            continue;
        }

        if (command == "INIT")
        {
            vector<vector<int>> board;
            string row;

            while (iss >> row)
            {
                vector<int> boardRow;

                for (char c : row)
                    boardRow.push_back(c - '0');

                board.push_back(boardRow);
            }

            game = Game(board, first);
            continue;
        }

        if (command == "TIME")
        {
            int myTime, oppTime;
            iss >> myTime >> oppTime;

            vector<int> ret =
                game.calculateMove(
                    myTime,
                    oppTime
                );

            game.updateMove(
                ret[0],
                ret[1],
                ret[2],
                ret[3],
                true
            );

            cout << ret[0] << " "
                 << ret[1] << " "
                 << ret[2] << " "
                 << ret[3] << endl;

            continue;
        }

        if (command == "OPP")
        {
            int r1, c1, r2, c2, time;

            iss >> r1
                >> c1
                >> r2
                >> c2
                >> time;

            game.updateOpponentAction(
                {r1, c1, r2, c2},
                time
            );

            continue;
        }

        if (command == "FINISH")
        {
            break;
        }

        cerr << "Invalid command: "
             << command << endl;

        return 1;
    }

    return 0;
}