#!/usr/bin/env python3

import json
import math
import random
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


ROWS = 10
COLS = 17
WEIGHT_COUNT = ROWS * COLS
DNA_ROWS = (ROWS + 1) // 2
DNA_COLS = (COLS + 1) // 2
DNA_COUNT = DNA_ROWS * DNA_COLS

POPULATION_SIZE = 10
ELITE_COUNT = 4
ROUNDS = 12
MATCH_RETRIES = 3
MUTATION_RATE = 0.12
MUTATION_SIGMA = 0.35
RANDOM_SEED = None

ROOT_DIR = Path(__file__).resolve().parents[2]
SRC_DIR = ROOT_DIR / "src"
GWF_DIR = SRC_DIR / "genetic_weight_finding"
OUT_DIR = GWF_DIR / "results"
BIN_DIR = OUT_DIR / "bin"
RUN_DIR = OUT_DIR / "runs"
WEIGHT_DIR = OUT_DIR / "weights"
HISTORY_PATH = OUT_DIR / "weight_history.jsonl"
SUMMARY_PATH = OUT_DIR / "round_summary.jsonl"
SETTING_PATH = GWF_DIR / "setting.ini"

WEIGHTED_EXE = BIN_DIR / "negamax_weights.exe"
NOWEIGHT_EXE = BIN_DIR / "negmax_noweights.exe"


@dataclass
class Candidate:
    name: str
    weights: list[float] | None
    wins: int = 0
    losses: int = 0
    draws: int = 0
    score_diff: int = 0
    cells: int = 0

    @property
    def is_baseline(self) -> bool:
        return self.weights is None

    @property
    def points(self) -> float:
        return self.wins + self.draws * 0.5


def quote_command(parts: Iterable[Path | str]) -> str:
    return subprocess.list2cmdline([str(part) for part in parts])


def ensure_dirs() -> None:
    for path in (BIN_DIR, RUN_DIR, WEIGHT_DIR):
        path.mkdir(parents=True, exist_ok=True)


def build_models() -> None:
    ensure_dirs()
    common_sources = [
        SRC_DIR / "negamax_agent.cpp",
        SRC_DIR / "state.cpp",
        SRC_DIR / "tt.cpp",
    ]
    builds = [
        (SRC_DIR / "models" / "negamax_weights.cpp", WEIGHTED_EXE),
        (SRC_DIR / "models" / "negmax_noweights.cpp", NOWEIGHT_EXE),
    ]

    for model_source, output in builds:
        command = [
            "g++",
            "-std=c++20",
            "-O2",
            "-Wall",
            "-Wextra",
            str(model_source),
            *(str(source) for source in common_sources),
            "-o",
            str(output),
        ]
        subprocess.run(command, cwd=ROOT_DIR, check=True)


def expand_dna(dna: list[float]) -> list[float]:
    if len(dna) != DNA_COUNT:
        raise ValueError(f"DNA must contain {DNA_COUNT} floats.")

    weights = [0.0] * WEIGHT_COUNT
    for row in range(ROWS):
        dna_row = min(row, ROWS - 1 - row)
        for col in range(COLS):
            dna_col = min(col, COLS - 1 - col)
            weights[row * COLS + col] = dna[dna_row * DNA_COLS + dna_col]
    return weights


def normalize(dna: list[float]) -> list[float]:
    full_weights = expand_dna(dna)
    rms = math.sqrt(sum(weight * weight for weight in full_weights) / len(full_weights))
    if rms <= 1e-12:
        return [1.0] * len(dna)
    return [weight / rms for weight in dna]


def random_weights() -> list[float]:
    return normalize([random.uniform(0.25, 1.75) for _ in range(DNA_COUNT)])


def crossover(parent_a: list[float], parent_b: list[float]) -> list[float]:
    child = []
    for a, b in zip(parent_a, parent_b):
        if random.random() < 0.5:
            child.append(a)
        else:
            child.append((a + b) * 0.5)
    return child


def mutate(weights: list[float]) -> list[float]:
    mutated = []
    for weight in weights:
        if random.random() < MUTATION_RATE:
            weight += random.gauss(0.0, MUTATION_SIGMA)
        mutated.append(weight)
    return normalize(mutated)


def write_weight_file(candidate: Candidate, round_dir: Path) -> Path:
    if candidate.weights is None:
        raise ValueError("Baseline candidate has no weight file.")

    path = round_dir / f"{candidate.name}.txt"
    full_weights = expand_dna(normalize(candidate.weights))
    path.write_text(
        "\n".join(f"{weight:.9f}" for weight in full_weights) + "\n",
        encoding="utf-8",
    )
    return path


def generate_input(path: Path) -> None:
    rows = []
    for _ in range(ROWS):
        rows.append("".join(str(random.randint(1, 9)) for _ in range(COLS)))
    path.write_text("\n".join(rows) + "\n", encoding="utf-8")


def write_settings(input_path: Path, log_path: Path, exec1: str, exec2: str) -> None:
    SETTING_PATH.write_text(
        "\n".join(
            [
                f"INPUT={input_path}",
                f"LOG={log_path}",
                f"EXEC1={exec1}",
                f"EXEC2={exec2}",
            ]
        )
        + "\n",
        encoding="utf-8",
    )


def command_for(candidate: Candidate, weight_files: dict[str, Path]) -> str:
    if candidate.is_baseline:
        return quote_command([NOWEIGHT_EXE])
    return quote_command([WEIGHTED_EXE, weight_files[candidate.name]])


def parse_result(log_path: Path) -> tuple[int, int]:
    first = second = None
    for line in log_path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("ABORT"):
            raise RuntimeError(line)
        if line.startswith("SCOREFIRST"):
            first = int(line.split()[1])
        elif line.startswith("SCORESECOND"):
            second = int(line.split()[1])

    if first is None or second is None:
        raise RuntimeError("No complete score in match log.")
    return first, second


def append_attempt_record(
    main_log_path: Path,
    attempt: int,
    input_path: Path,
    attempt_log_path: Path,
    completed: subprocess.CompletedProcess[str] | None = None,
    error: Exception | None = None,
) -> None:
    input_text = input_path.read_text(encoding="utf-8", errors="replace")
    attempt_log_text = ""
    if attempt_log_path.exists():
        attempt_log_text = attempt_log_path.read_text(encoding="utf-8", errors="replace")

    with main_log_path.open("a", encoding="utf-8") as logger:
        logger.write(f"===== ATTEMPT {attempt} INPUT =====\n")
        logger.write(input_text)
        if not input_text.endswith("\n"):
            logger.write("\n")

        if completed is not None:
            logger.write(f"===== ATTEMPT {attempt} TOOL =====\n")
            logger.write(f"RETURN_CODE {completed.returncode}\n")
            if completed.stdout:
                logger.write("STDOUT\n")
                logger.write(completed.stdout)
                if not completed.stdout.endswith("\n"):
                    logger.write("\n")
            if completed.stderr:
                logger.write("STDERR\n")
                logger.write(completed.stderr)
                if not completed.stderr.endswith("\n"):
                    logger.write("\n")

        if error is not None:
            logger.write(f"ERROR {error}\n")

        logger.write(f"===== ATTEMPT {attempt} MATCH LOG =====\n")
        if attempt_log_text:
            logger.write(attempt_log_text)
            if not attempt_log_text.endswith("\n"):
                logger.write("\n")
        else:
            logger.write("(no attempt log written)\n")
        logger.write(f"===== END ATTEMPT {attempt} =====\n")


def run_match(
    first: Candidate,
    second: Candidate,
    weight_files: dict[str, Path],
    round_dir: Path,
    match_id: int,
) -> tuple[int, int]:
    log_path = round_dir / f"match_{match_id:04d}_{first.name}_vs_{second.name}.log"
    last_error = None
    log_path.write_text("", encoding="utf-8")
    for attempt in range(1, MATCH_RETRIES + 1):
        input_path = round_dir / f"match_{match_id:04d}_attempt_{attempt}_input.txt"
        attempt_log_path = round_dir / (
            f"match_{match_id:04d}_{first.name}_vs_{second.name}_attempt_{attempt}.log"
        )
        generate_input(input_path)
        write_settings(
            input_path,
            attempt_log_path,
            command_for(first, weight_files),
            command_for(second, weight_files),
        )

        completed = None
        try:
            completed = subprocess.run(
                [sys.executable, "testing_tool.py"],
                cwd=GWF_DIR,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=45,
            )
            append_attempt_record(log_path, attempt, input_path, attempt_log_path, completed)
            if completed.returncode != 0:
                raise RuntimeError(
                    f"testing_tool.py exited {completed.returncode}: "
                    f"{completed.stderr.strip() or completed.stdout.strip()}"
                )
            scores = parse_result(attempt_log_path)
            return scores
        except Exception as exc:
            last_error = exc
            if completed is None:
                append_attempt_record(log_path, attempt, input_path, attempt_log_path, error=exc)
            with log_path.open("a", encoding="utf-8") as logger:
                logger.write(
                    f"RETRY {attempt} failed: {exc}\n"
                )
            time.sleep(0.25 * attempt)
        finally:
            input_path.unlink(missing_ok=True)
            attempt_log_path.unlink(missing_ok=True)

    raise RuntimeError(
        f"Match {first.name} vs {second.name} failed after "
        f"{MATCH_RETRIES} attempts: {last_error}"
    )


def record_result(first: Candidate, second: Candidate, first_score: int, second_score: int) -> None:
    first.cells += first_score
    second.cells += second_score
    first.score_diff += first_score - second_score
    second.score_diff += second_score - first_score

    if first_score > second_score:
        first.wins += 1
        second.losses += 1
    elif second_score > first_score:
        second.wins += 1
        first.losses += 1
    else:
        first.draws += 1
        second.draws += 1


def reset_scores(candidates: list[Candidate]) -> None:
    for candidate in candidates:
        candidate.wins = 0
        candidate.losses = 0
        candidate.draws = 0
        candidate.score_diff = 0
        candidate.cells = 0


def ranked(candidates: list[Candidate]) -> list[Candidate]:
    return sorted(
        candidates,
        key=lambda candidate: (candidate.points, candidate.score_diff, candidate.cells),
        reverse=True,
    )


def write_history(round_no: int, population: list[Candidate], standings: list[Candidate]) -> None:
    baseline = next(candidate for candidate in standings if candidate.is_baseline)
    with HISTORY_PATH.open("a", encoding="utf-8") as history:
        for candidate in population:
            history.write(
                json.dumps(
                    {
                        "round": round_no,
                        "name": candidate.name,
                        "dna": normalize(candidate.weights or []),
                        "weights": expand_dna(normalize(candidate.weights or [])),
                    }
                )
                + "\n"
            )

    with SUMMARY_PATH.open("a", encoding="utf-8") as summary:
        summary.write(
            json.dumps(
                {
                    "round": round_no,
                    "standings": [
                        {
                            "name": candidate.name,
                            "points": candidate.points,
                            "wins": candidate.wins,
                            "draws": candidate.draws,
                            "losses": candidate.losses,
                            "score_diff": candidate.score_diff,
                            "cells": candidate.cells,
                        }
                        for candidate in standings
                    ],
                    "baseline_points": baseline.points,
                }
            )
            + "\n"
        )


def print_elite(round_no: int, candidate: Candidate) -> None:
    print(f"Round {round_no} elite: {candidate.name}")
    if candidate.is_baseline:
        print("noweight")
        return

    print("DNA:")
    dna = normalize(candidate.weights or [])
    for row in range(DNA_ROWS):
        start = row * DNA_COLS
        end = start + DNA_COLS
        print(" ".join(f"{weight:.6f}" for weight in dna[start:end]))

    print("FULL SYMMETRIC WEIGHT MAP:")
    weights = expand_dna(dna)
    for row in range(ROWS):
        start = row * COLS
        end = start + COLS
        print(" ".join(f"{weight:.6f}" for weight in weights[start:end]))


def next_generation(round_no: int, standings: list[Candidate]) -> list[Candidate]:
    weighted = [candidate for candidate in standings if not candidate.is_baseline]
    elites = [Candidate(f"r{round_no + 1:03d}_elite_{idx}", normalize(candidate.weights or []))
              for idx, candidate in enumerate(weighted[:ELITE_COUNT])]

    children = elites[:]
    parent_pool = weighted[: max(ELITE_COUNT, 2)]
    while len(children) < POPULATION_SIZE:
        parent_a, parent_b = random.sample(parent_pool, 2)
        child_weights = mutate(crossover(parent_a.weights or [], parent_b.weights or []))
        children.append(Candidate(f"r{round_no + 1:03d}_child_{len(children)}", child_weights))
    return children


def run_round(round_no: int, population: list[Candidate]) -> list[Candidate]:
    round_dir = RUN_DIR / f"round_{round_no:03d}"
    if round_dir.exists():
        shutil.rmtree(round_dir)
    round_dir.mkdir(parents=True)

    normalized_population = [
        Candidate(candidate.name, normalize(candidate.weights or []))
        for candidate in population
    ]
    baseline = Candidate("noweight", None)
    competitors = normalized_population + [baseline]
    reset_scores(competitors)

    weight_files = {
        candidate.name: write_weight_file(candidate, round_dir)
        for candidate in normalized_population
    }

    match_id = 0
    for i, first in enumerate(competitors):
        for second in competitors[i + 1:]:
            score_first, score_second = run_match(first, second, weight_files, round_dir, match_id)
            record_result(first, second, score_first, score_second)
            match_id += 1

            score_second, score_first = run_match(second, first, weight_files, round_dir, match_id)
            record_result(second, first, score_second, score_first)
            match_id += 1

    standings = ranked(competitors)
    write_history(round_no, normalized_population, standings)

    print(f"Round {round_no} standings:")
    for place, candidate in enumerate(standings, start=1):
        print(
            f"  {place:2d}. {candidate.name:18s} "
            f"pts={candidate.points:4.1f} "
            f"W-D-L={candidate.wins}-{candidate.draws}-{candidate.losses} "
            f"diff={candidate.score_diff:+d}"
        )
    print_elite(round_no, standings[0])
    return next_generation(round_no, standings)


def main() -> None:
    if RANDOM_SEED is not None:
        random.seed(RANDOM_SEED)

    ensure_dirs()
    HISTORY_PATH.write_text("", encoding="utf-8")
    SUMMARY_PATH.write_text("", encoding="utf-8")
    build_models()

    population = [Candidate(f"r000_seed_{idx}", random_weights()) for idx in range(POPULATION_SIZE)]
    for round_no in range(ROUNDS):
        population = run_round(round_no, population)

    print(f"Weight history: {HISTORY_PATH}")
    print(f"Round summaries: {SUMMARY_PATH}")


if __name__ == "__main__":
    main()
