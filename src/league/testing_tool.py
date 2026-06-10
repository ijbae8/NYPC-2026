#!/usr/bin/env python3

import queue
import csv
import re
import sys
import subprocess
import time
import threading
from typing import List, TextIO, Tuple


R = 10
C = 17
import threading

class Player:
    def __init__(self, exec: str):
        self.exec = exec
        try:
            self.process = subprocess.Popen(
                self.exec,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                shell=True
            )
        except Exception as e:
            print(f'Error: Failed to start process: {e}')
            sys.exit(1)
        self.reads = queue.Queue()
        self.writes = queue.Queue()
        self.logs = queue.Queue()

        self.stdin_thread = threading.Thread(target=self.__handle_stdin)
        self.stdout_thread = threading.Thread(target=self.__handle_stdout)
        self.stderr_thread = threading.Thread(target=self.__handle_stderr)
        self.stdin_thread.daemon = True
        self.stdout_thread.daemon = True
        self.stderr_thread.daemon = True
        self.stdin_thread.start()
        self.stdout_thread.start()
        self.stderr_thread.start()

    def __handle_stdin(self):
        while True:
            try:
                self.process.stdin.write(self.writes.get())
                self.process.stdin.flush()
            except:
                pass

    def __handle_stdout(self):
        while True:
            try:
                self.reads.put(self.process.stdout.readline())
            except:
                pass

    def __handle_stderr(self):
        while True:
            try:
                line = self.process.stderr.readline()
                if line == "":
                    break
                self.logs.put(line)
            except:
                break

    def print(self, message: str):
        self.writes.put(f'{message}\n')

    def readline(self, timeout: float) -> Tuple[float, str] | None:
        try:
            start = time.time()
            content = self.reads.get(timeout=timeout)
            return (time.time() - start, content)
        except queue.Empty:
            return None

    @classmethod
    def readAll(cls, selfs: List['Player'], timeout: float) -> str:

        def __readline_thread(p: 'Player', timeout: float, idx: int, arr: List[Tuple[float, str]|None]):
            arr[idx] = p.readline(timeout)

        readline_threads = []
        returns = [None] * len(selfs)
        for i, p in enumerate(selfs):
            readline_threads.append(threading.Thread(target=__readline_thread, args=(p, timeout, i, returns)))

        for thread in readline_threads:
            thread.start()

        for thread in readline_threads:
            thread.join()

        return returns


class EngineCsvLogger:
    FIELDNAMES = [
        "move",
        "depth",
        "score",
        "best",
        "nodes",
        "assigned_ms",
        "used_ms",
        "depth_time_ms",
        "nps",
        "tt_probes",
        "tt_hits",
        "tt_cutoffs",
        "beta",
        "fm_beta",
        "fm_beta_rate",
        "avg_beta_idx",
        "alpha_raises",
        "pv_depth",
        "pv_line",
        "iter_depths",
        "iter_times_ms",
        "iter_nodes",
        "iter_nps",
        "raw",
    ]

    def __init__(self, file: TextIO):
        self.writer = csv.DictWriter(file, fieldnames=self.FIELDNAMES)
        self.writer.writeheader()
        self.current = None

    def consume(self, line: str):
        line = line.rstrip()
        if line.startswith("SEARCH "):
            self.flush()
            self.current = {"raw": line}
            self.current.update(self.__parse_key_values(line[len("SEARCH "):]))
            return

        if self.current is None:
            self.current = {"raw": line}
        else:
            self.current["raw"] = f'{self.current.get("raw", "")} | {line}'

        if line.startswith("TT "):
            values = self.__parse_key_values(line[len("TT "):])
            self.current["tt_probes"] = values.get("probes", "")
            self.current["tt_hits"] = values.get("hits", "")
            self.current["tt_cutoffs"] = values.get("cutoffs", "")
        elif line.startswith("AB "):
            self.current.update(self.__parse_key_values(line[len("AB "):]))
        elif line.startswith("PV "):
            values = self.__parse_key_values(line[len("PV "):])
            self.current["pv_depth"] = values.get("depth", "")
            self.current["pv_line"] = values.get("line", "")
        elif line.startswith("ITER "):
            values = self.__parse_key_values(line[len("ITER "):])
            self.current["iter_depths"] = values.get("depths", "")
            self.current["iter_times_ms"] = values.get("times_ms", "")
            self.current["iter_nodes"] = values.get("nodes", "")
            self.current["iter_nps"] = values.get("nps", "")
            self.flush()

    def flush(self):
        if self.current is None:
            return
        row = {field: self.current.get(field, "") for field in self.FIELDNAMES}
        self.writer.writerow(row)
        self.current = None

    @staticmethod
    def __parse_key_values(text: str):
        return dict(re.findall(r"(\w+)=([^=]*?)(?=\s+\w+=|$)", text))


def flush_engine_logs(players: List[Player], loggers: List[EngineCsvLogger]):
    for i, player in enumerate(players):
        while True:
            try:
                line = player.logs.get_nowait()
            except queue.Empty:
                break
            loggers[i].consume(line)


def read_settings():
    settings = {}
    try:
        with open("setting.ini", "r") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                if "=" in line:
                    key, value = line.split("=", 1)
                    settings[key.strip()] = value.strip()
    except FileNotFoundError:
        print("Error: setting.ini not found.\n" +
              "setting.ini should have following structure:\n" +
              "INPUT=<path to input file>\n" +
              "LOG=<path to log file>\n" +
              "EXEC1=<first player shell command>\n" +
              "EXEC2=<second player shell command>\n" +
              "\n" +
              "Example:\n" +
              "INPUT=./input.txt\n" +
              "LOG=./log.txt\n" +
              "EXEC1=./main\n" +
              "EXEC2=python3 main.py --test")
        sys.exit(1)

    required = ["INPUT", "LOG", "LOG1", "LOG2", "EXEC1", "EXEC2"]
    for key in required:
        if key not in settings:
            print(f"Error: {key} not found in setting.ini")
            sys.exit(1)

    return settings

def read_input(input_file: str) -> List[List[int]]:
    try:
        with open(input_file, "r") as f:
            ret = [list(map(int, line.strip())) for line in f]
            ret = [row for row in ret if len(row) != 0]
            assert len(ret) == R, f'{R} rows expected.'
            assert all(len(row) == C for row in ret), f'{C} columns expected.'
    except Exception as e:
        print(f'Error: {input_file} is not a valid input file.')
        print(f'       {e}')
        sys.exit(1)

    return ret


def read_from_process(process: subprocess.Popen) -> str:
    return process.stdout.readline().decode().strip()

def main():
    settings = read_settings()
    board = read_input(settings["INPUT"])

    user = [Player(settings["EXEC1"]), Player(settings["EXEC2"])]

    def run(user):
        with open(settings["LOG"], "w") as logger, \
             open(settings["LOG1"], "w", newline="") as log1, \
             open(settings["LOG2"], "w", newline="") as log2:
            engine_loggers = [EngineCsvLogger(log1), EngineCsvLogger(log2)]
            user[0].print("READY FIRST")
            user[1].print("READY SECOND")
            lines = Player.readAll(user, 3.0)
            flush_engine_logs(user, engine_loggers)
            aborted = False
            for i, line in enumerate(lines):
                if line is None or line[1] != "OK\n":
                    print(f'ABORT {i} TLE', file=logger)
                    aborted = True
            if aborted:
                flush_engine_logs(user, engine_loggers)
                return

            board_str = ' '.join(''.join(map(str, row)) for row in board)
            for u in user:
                u.print(f'INIT {board_str}')
            print(f'INIT {board_str}', file=logger)

            timeout = [10000, 10000]
            passed = False
            for i in range(999):
                u = i % 2
                name = ['FIRST', 'SECOND'][u]
                user[u].print(f'TIME {timeout[u]} {timeout[1 - u]}')
                read = user[u].readline(timeout[u])
                if read is None:
                    print(f'ABORT {u} TLE', file=logger)
                    flush_engine_logs(user, engine_loggers)
                    return

                readTime, readStr = read
                flush_engine_logs(user, engine_loggers)
                readTime = min(int(readTime*1000), timeout[u])
                timeout[u] -= readTime

                try:
                    r1,c1,r2,c2=map(int, readStr.split())
                except:
                    print(f'ABORT {u} Parse failed', file=logger)
                    flush_engine_logs(user, engine_loggers)
                    return

                if r1 == -1 and c1 == -1 and r2 == -1 and c2 == -1:
                    user[1-u].print(f'OPP {r1} {c1} {r2} {c2} {readTime}')
                    print(f'{name} {r1} {c1} {r2} {c2} {readTime}', file=logger)
                    flush_engine_logs(user, engine_loggers)
                    if passed:
                        break
                    passed = True
                else:
                    passed = False
                    if not (0 <= r1 <= r2 <  R and 0 <= c1 <= c2 < C):
                        print(f'ABORT {u} Out of range', file=logger)
                        flush_engine_logs(user, engine_loggers)
                        return

                    sum = 0
                    for i in range(r1, r2+1):
                        for j in range(c1, c2+1):
                            if board[i][j] > 0:
                                sum += board[i][j]
                    if sum != 10:
                        print(f'ABORT {u} Sum not equals to 10', file=logger)
                        flush_engine_logs(user, engine_loggers)
                        return

                    top, down, left, right = False, False, False, False
                    for i in range(r1, r2+1):
                        if board[i][c1] > 0:
                            left = True
                        if board[i][c2] > 0:
                            right = True
                    for i in range(c1, c2+1):
                        if board[r1][i] > 0:
                            top = True
                        if board[r2][i] > 0:
                            down = True
                    if not (left and right and top and down):
                        print(f'ABORT {u} Not fit', file=logger)
                        flush_engine_logs(user, engine_loggers)
                        return

                    for i in range(r1, r2+1):
                        for j in range(c1, c2+1):
                            board[i][j] = -u-1

                    user[1-u].print(f'OPP {r1} {c1} {r2} {c2} {int(readTime)}')
                    print(f'{name} {r1} {c1} {r2} {c2} {readTime}', file=logger)
                    flush_engine_logs(user, engine_loggers)

            score = [0, 0]
            for row in board:
                for num in row:
                    if num == -1:
                        score[0] += 1
                    elif num == -2:
                        score[1] += 1

            print(f'FINISH', file=logger)
            print(f'SCOREFIRST {score[0]}', file=logger)
            print(f'SCORESECOND {score[1]}', file=logger)
            flush_engine_logs(user, engine_loggers)
            for engine_logger in engine_loggers:
                engine_logger.flush()

    run(user)
    user[0].print("FINISH")
    user[1].print("FINISH")
    time.sleep(0.1)
    return


if __name__ == "__main__":
    main()
