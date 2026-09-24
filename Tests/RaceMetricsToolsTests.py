#!/usr/bin/env python3
"""Check tools/run_race_metrics.sh and tools/analyze_race_metrics.py against a stub game."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

# Stands in for CroMagRally: logs the arguments it got and prints METRICS lines like
# a --smoke-metrics race (Source/Headers/race_metrics.h). Track 9 crashes; with three
# cars, the last CPU car sits still for most of the race.
STUB = r'''#!/usr/bin/env python3
import os, sys
args = sys.argv[1:]
value = lambda flag: args[args.index(flag) + 1]
with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "calls.log"), "a") as calls:
    calls.write(" ".join(args) + "\n")
track, cars = int(value("--track")), int(value("--smoke-cars"))
if track == 9:
    print("Game Fatal Alert: stub crash")
    sys.exit(1)
racetime = 100.0 * track
print("SMOKE: practice track %d player 1 finished after 6000 frames" % track)
print("METRICS race track=%d cars=%d why=player1-finished racetime=%.1f ckpts=20" % (track, cars, racetime))
for p in range(cars):
    stuck = 0.6 * racetime if cars == 3 and p == cars - 1 else 0.1 * racetime
    print("METRICS car p=%d cpu=%d veh=%d place=%d lap=2 ckpt=%d done=%d fin=-1.0 finorder=-1"
          " lap0=2.0 ck5=10.0 lap1=%.1f lap2=%.1f stuck=%.1f wrong=0.0"
          " bumps=10 hard=%d blasted=1 pickups=5 uses=3 nbr=0.50"
          % (p, p > 0, p, p, 5 + p, p == 0, 30.0 + 10 * p, 60.0 + 20 * p, stuck, cars))
'''


def run(args: list[str], expect: int = 0) -> str:
    result = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=60)
    if result.returncode != expect:
        raise AssertionError(f"{args} exited {result.returncode}, expected {expect}:\n{result.stdout}")
    return result.stdout


def check(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def summary_line(report: str, metric: str) -> str:
    return next((line for line in report.splitlines() if line.startswith(metric + " ")), "")


def main() -> None:
    root = Path(sys.argv[1]).resolve(strict=True)
    runner = str(root / "tools" / "run_race_metrics.sh")
    analyzer = [sys.executable, str(root / "tools" / "analyze_race_metrics.py")]

    with tempfile.TemporaryDirectory(prefix="cmr-metrics-tools-",
                                     dir=os.environ.get("TMPDIR") or "/var/tmp") as scratch:
        build, out = Path(scratch, "build"), Path(scratch, "out")
        build.mkdir()
        (build / "CroMagRally").write_text(STUB)
        (build / "CroMagRally").chmod(0o755)
        env = dict(os.environ, TMPDIR=scratch)

        run(["bash", runner, str(build)], expect=2)                         # usage
        run(["bash", runner, str(Path(scratch, "missing")), str(out)], expect=2)

        # Every (track, cars, fps) race runs once; a crash fails the soak but keeps its full log.
        output = subprocess.run(["bash", runner, str(build), str(out), "1 2 9", "2 3", "50 60", "3"],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env,
                                timeout=60)
        check(output.returncode != 0, f"a crashed race must fail the soak:\n{output.stdout}")
        check(output.stdout.count("FAILED") == 4, output.stdout)
        logs = sorted(p.name for p in out.glob("*.log"))
        check(logs == [f"t{t}_n{n}_f{f}.log" for t in (1, 2) for n in (2, 3) for f in (50, 60)], str(logs))
        check(len(list((out / "full").glob("*.log"))) == 12, "every race keeps its full log")
        calls = (build / "calls.log").read_text().splitlines()
        check(len(calls) == 12, "\n".join(calls))
        for call in calls:
            words = call.split()
            fps = int(words[words.index("--smoke-fixed-fps") + 1])
            for expected in ("--no-vsync", "--smoke-autopilot", "--smoke-until-finish", "--smoke-metrics",
                             f"--smoke-test-frames {fps * 900}", "--smoke-seed 12345"):
                check(expected in call, f"{expected} missing from {call}")
        check(not list(Path(scratch).glob("cmr-metrics-*")), "per-race preferences are removed")

        # A rerun only retries the failed races; SEED, FRAMES and GAME_ARGS reach the game.
        env.update(SEED="7", FRAMES="1234", GAME_ARGS="--car 2")
        rerun = subprocess.run(["bash", runner, str(build), str(out), "1 9", "2", "50"],
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env, timeout=60)
        check(rerun.returncode != 0 and "t1_n2_f50: done earlier" in rerun.stdout, rerun.stdout)
        calls = (build / "calls.log").read_text().splitlines()[12:]
        check(len(calls) == 1 and calls[0].startswith("--track 9 ")
              and "--smoke-test-frames 1234" in calls[0] and "--smoke-seed 7" in calls[0]
              and calls[0].endswith("--car 2"), "\n".join(calls))

        # Two car counts: per-race rows, per-track comparison and stranded CPU cars.
        results = Path(scratch, "results.json")
        report = run([*analyzer, str(out), "--json", str(results)])
        rows = json.loads(results.read_text())
        check(len(rows) == 8 and all(r["ok"] for r in rows), str(rows))
        check(sum(r["stranded"] for r in rows) == 4, str(rows))              # the 3-car races
        check("per-track means (2 cars -> 3 cars)" in report, report)
        check("Desert        cpu_lap2= 40.00-> 45.00" in report, report)      # mean of CPU lap-2 times
        check("races=2/2 stranded 0->2" in report, report)
        check(summary_line(report, "hard_per_min").endswith("change  +50.0%"), report)

        # Baseline mode matches races by track, cars and tag; legacy EXP lines still parse.
        base = Path(scratch, "base")
        base.mkdir()
        for log in out.glob("t1_n2_*.log"):
            text = log.read_text().replace("METRICS race", "EXPSUM").replace("METRICS car", "EXPCAR")
            (base / log.name).write_text(text.replace("hard=2", "hard=1"))
        report = run([*analyzer, str(out), "--baseline", str(base), "--json", str(results)])
        check(len(json.loads(results.read_text())) == 4, report)
        check("per-track means (baseline -> current)" in report, report)
        check(summary_line(report, "hard_per_min").endswith("change +100.0%"), report)   # hard hits doubled

        run([*analyzer, str(Path(scratch, "missing"))], expect=1)

    print("race metrics tools tests passed")


if __name__ == "__main__":
    main()
