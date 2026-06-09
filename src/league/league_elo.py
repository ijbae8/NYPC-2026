import argparse
import contextlib
import importlib
import io
import json
import math
import os
import random
import re
import shutil
import sys
from datetime import datetime
from pathlib import Path


BASE_DIR = Path(__file__).resolve().parent
COMPETITORS_FILE = BASE_DIR / "competants.txt"
SETTING_FILE = BASE_DIR / "setting.ini"
INPUT_FILE = BASE_DIR / "input.txt"
RUNNER_MODULE = "testing_tool"
ELO_FILE = BASE_DIR / "elo_ratings.json"
HISTORY_FILE = BASE_DIR / "match_history.jsonl"
LOG_DIR = BASE_DIR / "logs"

START_RATING = 1500.0
K_FACTOR = 32.0
DEFAULT_MAP_ROWS = 10
DEFAULT_MAP_COLS = 17
MAX_ATTEMPTS = 3


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


def load_ratings(competitors):
    if ELO_FILE.exists():
        data = json.loads(ELO_FILE.read_text())
    else:
        data = {}

    ratings = {}
    for competitor_id in competitors:
        ratings[competitor_id] = float(data.get(competitor_id, START_RATING))
    return ratings


def save_ratings(ratings):
    payload = {
        competitor_id: round(rating, 2)
        for competitor_id, rating in sorted(ratings.items(), key=lambda item: (-item[1], item[0]))
    }
    ELO_FILE.write_text(json.dumps(payload, indent=2) + "\n")


def expected_score(rating_a, rating_b):
    return 1.0 / (1.0 + math.pow(10.0, (rating_b - rating_a) / 400.0))


def update_elo(rating_a, rating_b, actual_a):
    expected_a = expected_score(rating_a, rating_b)
    expected_b = 1.0 - expected_a
    actual_b = 1.0 - actual_a
    return (
        rating_a + K_FACTOR * (actual_a - expected_a),
        rating_b + K_FACTOR * (actual_b - expected_b),
    )


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
    LOG_DIR.mkdir(exist_ok=True)
    return LOG_DIR / f"{match_id}_{leg_name}_attempt{attempt}.txt"


def setting_path(path):
    return ".\\" + str(path.relative_to(BASE_DIR)).replace("/", "\\")


def archive_attempt_log(log_path, ok):
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


def play_two_leg_match(competitors, ratings, first_id, second_id):
    if first_id == second_id:
        raise ValueError("A competitor cannot play against itself.")
    if first_id not in competitors:
        raise ValueError(f"Unknown competitor ID: {first_id}")
    if second_id not in competitors:
        raise ValueError(f"Unknown competitor ID: {second_id}")

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    match_id = f"{timestamp}_{first_id}_vs_{second_id}"

    before_a = ratings[first_id]
    before_b = ratings[second_id]

    leg1 = run_leg(first_id, competitors[first_id], second_id, competitors[second_id], "leg1", match_id)
    leg2 = run_leg(second_id, competitors[second_id], first_id, competitors[first_id], "leg2", match_id)
    legs = [leg1, leg2]

    wins_a = sum(1 for leg in legs if leg["winner"] == first_id)
    wins_b = sum(1 for leg in legs if leg["winner"] == second_id)

    if wins_a == 2:
        result_a = 1.0
        result = f"{first_id} wins"
    elif wins_b == 2:
        result_a = 0.0
        result = f"{second_id} wins"
    else:
        result_a = 0.5
        result = "draw"

    after_a, after_b = update_elo(before_a, before_b, result_a)
    ratings[first_id] = after_a
    ratings[second_id] = after_b
    save_ratings(ratings)

    history = {
        "match_id": match_id,
        "time": datetime.now().isoformat(timespec="seconds"),
        "competitors": [first_id, second_id],
        "result": result,
        "wins": {first_id: wins_a, second_id: wins_b},
        "rating_before": {first_id: round(before_a, 2), second_id: round(before_b, 2)},
        "rating_after": {first_id: round(after_a, 2), second_id: round(after_b, 2)},
        "legs": legs,
    }
    with HISTORY_FILE.open("a") as file:
        file.write(json.dumps(history) + "\n")

    return history


def choose_auto_pair(ratings):
    TEMPERATURE = 100

    ranked = sorted(ratings.items(), key=lambda item: item[1])
    pairs = list(zip(ranked, ranked[1:]))

    weights = [
        math.exp(-abs(pair[0][1] - pair[1][1]) / TEMPERATURE)
        for pair in pairs
    ]

    chosen_pair = random.choices(pairs, weights=weights, k=1)[0]

    (a_id, a_rating), (b_id, b_rating) = chosen_pair
    return (a_id, b_id)

def print_standings(ratings):
    for rank, (competitor_id, rating) in enumerate(
        sorted(ratings.items(), key=lambda item: (-item[1], item[0])),
        start=1,
    ):
        print(f"{rank:2}. {competitor_id:30} {rating:7.2f}")


def run_match_batch(args, competitors, ratings):
    if args.players:
        if len(args.players) != 2:
            raise ValueError("Manual matches require exactly two IDs.")
        pair = tuple(args.players)
    else:
        pair = choose_auto_pair(ratings)

    for match_number in range(1, args.count + 1):
        first_id, second_id = pair
        if not args.players and match_number > 1:
            first_id, second_id = choose_auto_pair(ratings)

        history = play_two_leg_match(competitors, ratings, first_id, second_id)
        print(
            f"{history['match_id']}: {history['result']} "
            f"({history['rating_before'][first_id]:.2f}->{history['rating_after'][first_id]:.2f}, "
            f"{history['rating_before'][second_id]:.2f}->{history['rating_after'][second_id]:.2f})"
        )


def build_parser():
    parser = argparse.ArgumentParser(description="Persistent two-leg ELO league runner.")
    subparsers = parser.add_subparsers(dest="command")

    subparsers.add_parser("standings", help="Show current ratings.")
    subparsers.add_parser("init", help="Create or refresh the ELO file for current competitors.")

    match_parser = subparsers.add_parser("match", help="Run one or more two-leg matches.")
    match_parser.add_argument("players", nargs="*", help="Optional manual competitor IDs: ID1 ID2")
    match_parser.add_argument("-n", "--count", type=int, default=1, help="Number of two-leg matches to run.")

    return parser


def main():
    args = build_parser().parse_args()
    competitors = load_competitors()
    ratings = load_ratings(competitors)

    if args.command == "standings":
        save_ratings(ratings)
        print_standings(ratings)
    elif args.command == "init":
        save_ratings(ratings)
        print(f"Initialized {ELO_FILE.name} with {len(ratings)} competitors.")
    elif args.command == "match":
        if args.count < 1:
            raise ValueError("--count must be at least 1.")
        run_match_batch(args, competitors, ratings)
    else:
        print_standings(ratings)


if __name__ == "__main__":
    main()
