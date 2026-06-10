import contextlib
import importlib
import io
import json
import os
import random
import re
import shutil
import sys
from datetime import datetime
from itertools import combinations
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parent
COMPETITORS_FILE = BASE_DIR / "competants.txt"
SETTING_FILE = BASE_DIR / "setting.ini"
INPUT_FILE = BASE_DIR / "input.txt"
RUNNER_MODULE = "testing_tool"
HISTORY_FILE = BASE_DIR / "match_history.jsonl"
LOG_DIR = BASE_DIR / "logs"
TEMP_LOG_FILE = BASE_DIR / "log.txt"

DEFAULT_MAP_ROWS = 10
DEFAULT_MAP_COLS = 17
MAX_ATTEMPTS = 3
LEAGUE_MATCH_ITERATIONS = 100
ENABLE_LOGGING = False


def runner_module():
    if str(BASE_DIR) not in sys.path:
        sys.path.insert(0, str(BASE_DIR))
    return importlib.import_module(RUNNER_MODULE)


def map_size():
    module = runner_module()
    return (
        int(getattr(module, "R", DEFAULT_MAP_ROWS)),
        int(getattr(module, "C", DEFAULT_MAP_COLS)),
    )


def stable_id_from_info(info, index):
    parts = info.split()
    stems = []
    for part in parts[:2]:
        stem = Path(part.strip('"')).stem
        if stem:
            stems.append(stem)
    if not stems:
        stems = [f"competitor_{index}"]
    raw_id = "__".join(stems)
    clean_id = re.sub(r"[^A-Za-z0-9_.-]+", "_", raw_id).strip("_")
    return clean_id or f"competitor_{index}"


def load_competitors():
    competitors = {}
    for line_number, raw_line in enumerate(COMPETITORS_FILE.read_text().splitlines(), start=1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue

        if "=" in line:
            competitor_id, info = line.split("=", 1)
            competitor_id = competitor_id.strip()
            info = info.strip()
        else:
            info = line
            competitor_id = stable_id_from_info(info, line_number)

        if not competitor_id:
            raise ValueError(f"Missing competitor ID on line {line_number}")
        if not info:
            raise ValueError(f"Missing competitor info for {competitor_id}")
        if competitor_id in competitors:
            raise ValueError(f"Duplicate competitor ID: {competitor_id}")

        competitors[competitor_id] = info

    if len(competitors) < 2:
        raise ValueError("At least two competitors are required.")
    return competitors


def generate_map():
    rows_count, cols_count = map_size()
    rows = []
    for _ in range(rows_count):
        rows.append("".join(str(random.randint(1, 9)) for _ in range(cols_count)))
    INPUT_FILE.write_text("\n".join(rows) + "\n")


def set_runner_config(first_info, second_info, log_path):
    lines = SETTING_FILE.read_text().splitlines()
    updated = []
    seen_log = False
    seen_exec1 = False
    seen_exec2 = False

    for line in lines:
        if line.startswith("LOG="):
            updated.append(f"LOG={log_path}")
            seen_log = True
        elif line.startswith("EXEC1="):
            updated.append(f"EXEC1={first_info}")
            seen_exec1 = True
        elif line.startswith("EXEC2="):
            updated.append(f"EXEC2={second_info}")
            seen_exec2 = True
        else:
            updated.append(line)

    if not seen_log:
        updated.append(f"LOG={log_path}")
    if not seen_exec1:
        updated.append(f"EXEC1={first_info}")
    if not seen_exec2:
        updated.append(f"EXEC2={second_info}")

    SETTING_FILE.write_text("\n".join(updated) + "\n")


def parse_scores(log_text):
    first_match = re.search(r"^SCOREFIRST\s+(-?\d+)\s*$", log_text, re.MULTILINE)
    second_match = re.search(r"^SCORESECOND\s+(-?\d+)\s*$", log_text, re.MULTILINE)
    if not first_match or not second_match:
        return None
    return int(first_match.group(1)), int(second_match.group(1))


def make_attempt_log_path(match_id, leg_name, attempt):
    if not ENABLE_LOGGING:
        return TEMP_LOG_FILE
    LOG_DIR.mkdir(exist_ok=True)
    return LOG_DIR / f"{match_id}_{leg_name}_attempt{attempt}.txt"


def setting_path(path):
    return ".\\" + str(path.relative_to(BASE_DIR)).replace("/", "\\")


def archive_attempt_log(log_path, ok):
    if not ENABLE_LOGGING:
        if log_path.exists():
            log_path.unlink()
        return None

    status = "ok" if ok else "failed"
    destination = log_path.with_name(f"{log_path.stem}_{status}{log_path.suffix}")
    if log_path.exists():
        shutil.move(str(log_path), str(destination))
    else:
        destination.write_text("")
    return destination.relative_to(BASE_DIR).as_posix()


def run_testing_tool():
    module = runner_module()
    output = io.StringIO()
    previous_cwd = Path.cwd()
    try:
        os.chdir(BASE_DIR)
        with contextlib.redirect_stdout(output):
            module.main()
        return 0, output.getvalue()
    except SystemExit as exc:
        code = exc.code if isinstance(exc.code, int) else 1
        return code, output.getvalue()
    except Exception as exc:
        print(f"Runner exception: {exc}", file=output)
        return 1, output.getvalue()
    finally:
        os.chdir(previous_cwd)


def run_leg(first_id, first_info, second_id, second_info, leg_name, match_id):
    last_failure = None
    for attempt in range(1, MAX_ATTEMPTS + 1):
        log_path = make_attempt_log_path(match_id, leg_name, attempt)
        if log_path.exists():
            log_path.unlink()

        generate_map()
        set_runner_config(first_info, second_info, setting_path(log_path))

        returncode, runner_output = run_testing_tool()

        log_text = log_path.read_text() if log_path.exists() else ""
        scores = parse_scores(log_text)
        ok = returncode == 0 and scores is not None
        log_name = archive_attempt_log(log_path, ok)
        last_failure = {
            "attempt": attempt,
            "returncode": returncode,
            "log": log_name,
            "runner_output": runner_output.strip(),
        }

        if ok:
            score_first, score_second = scores
            if score_first > score_second:
                winner = first_id
            elif score_second > score_first:
                winner = second_id
            else:
                winner = None
            return {
                "leg": leg_name,
                "attempt": attempt,
                "first": first_id,
                "second": second_id,
                "score_first": score_first,
                "score_second": score_second,
                "winner": winner,
                "log": log_name,
            }

    raise RuntimeError(
        f"{leg_name} failed after {MAX_ATTEMPTS} attempts: {first_id} vs {second_id}. "
        f"Last attempt: {last_failure}"
    )


def play_two_leg_match(competitors, first_id, second_id, iteration):
    if first_id == second_id:
        raise ValueError("A competitor cannot play against itself.")
    if first_id not in competitors:
        raise ValueError(f"Unknown competitor ID: {first_id}")
    if second_id not in competitors:
        raise ValueError(f"Unknown competitor ID: {second_id}")

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    match_id = f"{timestamp}_{first_id}_vs_{second_id}_iter{iteration}"

    leg1 = run_leg(first_id, competitors[first_id], second_id, competitors[second_id], "leg1", match_id)
    leg2 = run_leg(second_id, competitors[second_id], first_id, competitors[first_id], "leg2", match_id)
    legs = [leg1, leg2]

    wins_a = sum(1 for leg in legs if leg["winner"] == first_id)
    wins_b = sum(1 for leg in legs if leg["winner"] == second_id)

    if wins_a == 2:
        result = f"{first_id} wins"
    elif wins_b == 2:
        result = f"{second_id} wins"
    else:
        result = "draw"

    history = {
        "match_id": match_id,
        "time": datetime.now().isoformat(timespec="seconds"),
        "iteration": iteration,
        "competitors": [first_id, second_id],
        "result": result,
        "wins": {first_id: wins_a, second_id: wins_b},
        "legs": legs,
    }
    if ENABLE_LOGGING:
        with HISTORY_FILE.open("a") as file:
            file.write(json.dumps(history) + "\n")

    return history


def iter_all_pairs(competitors):
    return list(combinations(sorted(competitors), 2))


def create_empty_record():
    return {
        "matches": 0,
        "match_wins": 0,
        "draws": 0,
        "match_losses": 0,
        "leg_wins": 0,
        "leg_losses": 0,
        "score_for": 0,
        "score_against": 0,
    }


def update_summary_records(records, entry):
    competitor_ids = entry.get("competitors", [])
    wins = entry.get("wins", {})
    legs = entry.get("legs", [])
    if len(competitor_ids) != 2:
        return

    first_id, second_id = competitor_ids
    if first_id not in records or second_id not in records:
        return

    first_wins = int(wins.get(first_id, 0))
    second_wins = int(wins.get(second_id, 0))

    first_record = records[first_id]
    second_record = records[second_id]

    first_record["matches"] += 1
    second_record["matches"] += 1
    first_record["leg_wins"] += first_wins
    first_record["leg_losses"] += second_wins
    second_record["leg_wins"] += second_wins
    second_record["leg_losses"] += first_wins

    for leg in legs:
        score_first = int(leg.get("score_first", 0))
        score_second = int(leg.get("score_second", 0))
        leg_first = leg.get("first")
        leg_second = leg.get("second")

        if leg_first in records:
            records[leg_first]["score_for"] += score_first
            records[leg_first]["score_against"] += score_second
        if leg_second in records:
            records[leg_second]["score_for"] += score_second
            records[leg_second]["score_against"] += score_first

    if first_wins > second_wins:
        first_record["match_wins"] += 1
        second_record["match_losses"] += 1
    elif second_wins > first_wins:
        second_record["match_wins"] += 1
        first_record["match_losses"] += 1
    else:
        first_record["draws"] += 1
        second_record["draws"] += 1


def compute_competitor_summary(competitors, results):
    records = {
        competitor_id: create_empty_record()
        for competitor_id in competitors
    }

    for entry in results:
        update_summary_records(records, entry)
    return records


def build_pair_summary(results):
    pair_summary = {}
    for entry in results:
        first_id, second_id = entry["competitors"]
        pair_key = (first_id, second_id)
        if pair_key not in pair_summary:
            pair_summary[pair_key] = {
                "matches": 0,
                "draws": 0,
                "match_wins": {first_id: 0, second_id: 0},
                "leg_wins": {first_id: 0, second_id: 0},
                "score_for": {first_id: 0, second_id: 0},
                "score_against": {first_id: 0, second_id: 0},
            }

        summary = pair_summary[pair_key]
        summary["matches"] += 1
        first_wins = int(entry["wins"].get(first_id, 0))
        second_wins = int(entry["wins"].get(second_id, 0))
        summary["leg_wins"][first_id] += first_wins
        summary["leg_wins"][second_id] += second_wins

        if first_wins > second_wins:
            summary["match_wins"][first_id] += 1
        elif second_wins > first_wins:
            summary["match_wins"][second_id] += 1
        else:
            summary["draws"] += 1

        for leg in entry["legs"]:
            score_first = int(leg["score_first"])
            score_second = int(leg["score_second"])
            leg_first = leg["first"]
            leg_second = leg["second"]
            summary["score_for"][leg_first] += score_first
            summary["score_against"][leg_first] += score_second
            summary["score_for"][leg_second] += score_second
            summary["score_against"][leg_second] += score_first

    return pair_summary


def print_match_result(history):
    first_id, second_id = history["competitors"]
    print(
        f"[MATCH] {history['match_id']} -> {history['result']} "
        f"(iteration {history['iteration']}, "
        f"leg wins {first_id} {history['wins'][first_id]} - {history['wins'][second_id]} {second_id})"
    )
    for leg in history["legs"]:
        winner = leg["winner"] or "draw"
        log_part = f" | log: {leg['log']}" if leg["log"] else ""
        print(
            f"        {leg['leg']}: {leg['first']} {leg['score_first']} - "
            f"{leg['score_second']} {leg['second']} | winner: {winner} | "
            f"attempt: {leg['attempt']}{log_part}"
        )


def print_pair_summary(pair_summary):
    print("\n=== Pair Summary ===")
    for (first_id, second_id), summary in sorted(pair_summary.items()):
        first_score_diff = summary["score_for"][first_id] - summary["score_against"][first_id]
        second_score_diff = summary["score_for"][second_id] - summary["score_against"][second_id]
        print(
            f"{first_id} vs {second_id} | matches: {summary['matches']} | draws: {summary['draws']} | "
            f"match wins: {first_id} {summary['match_wins'][first_id]} - "
            f"{summary['match_wins'][second_id]} {second_id}"
        )
        print(
            f"    leg wins: {first_id} {summary['leg_wins'][first_id]} - "
            f"{summary['leg_wins'][second_id]} {second_id}"
        )
        print(
            f"    score totals: {first_id} {summary['score_for'][first_id]} "
            f"(against {summary['score_against'][first_id]}, diff {first_score_diff:+}) | "
            f"{second_id} {summary['score_for'][second_id]} "
            f"(against {summary['score_against'][second_id]}, diff {second_score_diff:+})"
        )


def print_competitor_summary(competitors, results):
    summary = compute_competitor_summary(competitors, results)
    ranked = sorted(
        summary.items(),
        key=lambda item: (
            -item[1]["match_wins"],
            -item[1]["draws"],
            -(item[1]["leg_wins"] - item[1]["leg_losses"]),
            -(item[1]["score_for"] - item[1]["score_against"]),
            -item[1]["leg_wins"],
            item[0],
        ),
    )

    print("\n=== Final Competitor Summary ===")
    print(
        f"{'RK':>2} {'ID':30} {'MP':>3} {'W':>3} {'D':>3} {'L':>3} "
        f"{'LW':>3} {'LL':>3} {'LD':>4} {'SF':>5} {'SA':>5} {'SD':>5}"
    )
    for rank, (competitor_id, record) in enumerate(ranked, start=1):
        leg_diff = record["leg_wins"] - record["leg_losses"]
        score_diff = record["score_for"] - record["score_against"]
        print(
            f"{rank:2} {competitor_id:30} {record['matches']:3} "
            f"{record['match_wins']:3} {record['draws']:3} {record['match_losses']:3} "
            f"{record['leg_wins']:3} {record['leg_losses']:3} {leg_diff:4} "
            f"{record['score_for']:5} {record['score_against']:5} {score_diff:5}"
        )


def run_full_evaluation(competitors):
    pairs = iter_all_pairs(competitors)
    total_matches = len(pairs) * LEAGUE_MATCH_ITERATIONS
    results = []

    print("=== League Evaluation ===")
    print(f"Competitors: {len(competitors)}")
    print(f"Pairs: {len(pairs)}")
    print(f"Two-leg matches per pair: {LEAGUE_MATCH_ITERATIONS}")
    print(f"Total two-leg matches to run: {total_matches}")
    print(f"Logging enabled: {ENABLE_LOGGING}")

    if ENABLE_LOGGING:
        HISTORY_FILE.write_text("")
    elif TEMP_LOG_FILE.exists():
        TEMP_LOG_FILE.unlink()

    completed_matches = 0
    for pair_index, (first_id, second_id) in enumerate(pairs, start=1):
        print(f"\n--- Pair {pair_index}/{len(pairs)}: {first_id} vs {second_id} ---")
        for iteration in range(1, LEAGUE_MATCH_ITERATIONS + 1):
            history = play_two_leg_match(competitors, first_id, second_id, iteration)
            results.append(history)
            completed_matches += 1
            print(f"Progress: {completed_matches}/{total_matches} matches completed")
            print_match_result(history)

    return results


def main():
    if LEAGUE_MATCH_ITERATIONS < 1:
        raise ValueError("LEAGUE_MATCH_ITERATIONS must be at least 1.")

    competitors = load_competitors()
    results = run_full_evaluation(competitors)
    print_pair_summary(build_pair_summary(results))
    print_competitor_summary(competitors, results)
    if ENABLE_LOGGING:
        print(f"\nDetailed match history written to {HISTORY_FILE.name}")


if __name__ == "__main__":
    main()
