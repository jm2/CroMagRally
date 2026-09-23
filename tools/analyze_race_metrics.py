#!/usr/bin/env python3
"""Summarize race-metrics soaks from tools/run_race_metrics.sh.

Reads DIR/t<track>_n<cars>_<tag>.log files holding the METRICS lines documented in
Source/Headers/race_metrics.h (the older experiment harness's EXPSUM/EXPCAR lines are
accepted too) and prints, over the CPU cars of each race:

  * one row per race;
  * means per car count (or per soak, with --baseline);
  * per-track means for two groups side by side -- by default the smallest and the
    largest car count found (e.g. 6 vs 12 cars), or a baseline soak vs DIR -- with
    the number of stranded CPU cars (stuck for more than half the race);
  * the overall change per metric between the two groups.

Tracks are weighted equally. Also writes the per-race rows as JSON (--json).
"""

import argparse
import glob
import json
import os
import re
import statistics
import sys

TRACKS = {1: "Desert", 2: "Jungle", 3: "Ice", 4: "Crete", 5: "China", 6: "Egypt",
          7: "Europe", 8: "Scandinavia", 9: "Atlantis"}
LOG_NAME = re.compile(r"t(\d+)_n(\d+)_(\w+)\.log$")
RACE_LINE = re.compile(r"(?:METRICS race|EXPSUM) (.*)")
CAR_LINE = re.compile(r"(?:METRICS car|EXPCAR) (.*)")
FIELD = re.compile(r"(\w+)=(\S+)")

TABLE_KEYS = ["cpu_lap1", "cpu_lap2", "cpu_ck5", "stuck_pct", "worst_stuck_pct", "wrong_pct",
              "hard_per_min", "blasted_per_min", "pickups_per_min", "uses_per_min", "nbr", "prog_spread"]
COMPARE_KEYS = ["cpu_lap2", "cpu_ck5", "stuck_pct", "hard_per_min", "pickups_per_min", "uses_per_min", "nbr"]


def mean(values):
    values = [v for v in values if v is not None]
    return statistics.mean(values) if values else None


def parse_log(path):
    race, cars = None, []
    with open(path, errors="replace") as log:
        for line in log:
            match = RACE_LINE.search(line)
            if match:
                race = dict(FIELD.findall(match.group(1)))
                cars = []          # a log holds one race; keep the last report
                continue
            match = CAR_LINE.search(line)
            if match:
                cars.append({k: float(v) for k, v in FIELD.findall(match.group(1))})
    return race, cars


def summarize(path, source, stranded_fraction):
    name = LOG_NAME.search(os.path.basename(path))
    row = dict(source=source, track=int(name.group(1)), n=int(name.group(2)), tag=name.group(3), ok=False)
    race, cars = parse_log(path)
    if not race or not cars or float(race.get("racetime", 0)) <= 0:
        return row
    rt = float(race["racetime"])
    minutes = rt / 60.0
    ncp = float(race["ckpts"])
    cpus = [c for c in cars if c["cpu"] == 1]
    humans = [c for c in cars if c["cpu"] == 0]
    # Checkpoints passed; a car still behind the last checkpoint on lap -1 has passed none.
    progress = [c["lap"] * ncp + c["ckpt"] if c["lap"] >= 0 else 0 for c in cpus]  # still on the grid: 0
    row.update(
        ok=True, why=race.get("why", "?"), racetime=rt, cpus=len(cpus),
        cpu_lap1=mean([c["lap1"] - c["lap0"] for c in cpus if c["lap1"] > 0 and c["lap0"] >= 0]),
        cpu_lap2=mean([c["lap2"] - c["lap1"] for c in cpus if c["lap1"] > 0 and c["lap2"] > 0]),
        cpu_ck5=mean([c["ck5"] for c in cpus if c["ck5"] > 0]),
        stuck_pct=mean([100 * c["stuck"] / rt for c in cpus]),
        worst_stuck_pct=max((100 * c["stuck"] / rt for c in cpus), default=None),
        wrong_pct=mean([100 * c["wrong"] / rt for c in cpus]),
        hard_per_min=mean([c["hard"] / minutes for c in cpus]),
        bumps_per_min=mean([c["bumps"] / minutes for c in cpus]),
        blasted_per_min=mean([c["blasted"] / minutes for c in cpus]),
        pickups_per_min=mean([c["pickups"] / minutes for c in cpus]),
        uses_per_min=mean([c["uses"] / minutes for c in cpus]),
        nbr=mean([c["nbr"] for c in cpus]),
        human_nbr=humans[0]["nbr"] if humans else None,
        prog_spread=(max(progress) - min(progress)) / ncp if progress else None,
        stranded=sum(1 for c in cpus if c["stuck"] > stranded_fraction * rt),
    )
    return row


def load(directory, source, stranded_fraction):
    paths = [p for p in glob.glob(os.path.join(directory, "t*_n*_*.log")) if LOG_NAME.search(os.path.basename(p))]
    return [summarize(p, source, stranded_fraction) for p in sorted(paths)]


def fmt(value, width=11):
    return f"{value:{width}.2f}" if value is not None else f"{'-':>{width}s}"


def track_name(track):
    return TRACKS.get(track, f"track{track}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dir", help="soak output directory (tools/run_race_metrics.sh)")
    parser.add_argument("--compare", nargs=2, type=int, metavar=("A", "B"),
                        help="car counts to compare (default: smallest and largest found)")
    parser.add_argument("--baseline", metavar="DIR",
                        help="compare DIR against this earlier soak, race by race (same track, cars and tag)")
    parser.add_argument("--stranded", type=float, default=0.5, metavar="FRACTION",
                        help="a CPU car stuck for more than this fraction of the race is stranded (default 0.5)")
    parser.add_argument("--json", metavar="FILE", help="write the per-race rows here (default: DIR/results.json)")
    args = parser.parse_args()

    rows = load(args.dir, "current", args.stranded)
    if args.baseline:
        base = load(args.baseline, "baseline", args.stranded)
        runs = lambda rs: {(r["track"], r["n"], r["tag"]) for r in rs if r["ok"]}
        matched = runs(rows) & runs(base)
        rows = [r for r in base + rows if (r["track"], r["n"], r["tag"]) in matched or not r["ok"]]
        group_of, groups = (lambda r: r["source"]), ["baseline", "current"]
        labels = {"baseline": "baseline", "current": "current"}
    else:
        group_of = lambda r: r["n"]
        counts = sorted({r["n"] for r in rows if r["ok"]})
        groups = args.compare or (counts[:1] + counts[-1:] if len(counts) > 1 else counts)
        labels = {n: f"{n} cars" for n in counts + list(groups)}
    if not rows:
        sys.exit(f"no t<track>_n<cars>_<tag>.log files in {args.dir}")

    with open(args.json or os.path.join(args.dir, "results.json"), "w") as out:
        json.dump(rows, out, indent=1)

    # ---- one row per race ----
    print(f"{'track':12s} {'n':>2s} {'tag':>8s} {'end':>16s} {'stranded':>8s} "
          + " ".join(f"{k[:11]:>11s}" for k in TABLE_KEYS))
    for r in sorted(rows, key=lambda r: (r["track"], r["n"], r["source"], r["tag"])):
        tag = r["tag"] if not args.baseline else f"{r['source'][:4]}:{r['tag']}"
        if not r["ok"]:
            print(f"{track_name(r['track']):12s} {r['n']:2d} {tag:>8s} {'(no metrics)':>16s}")
            continue
        print(f"{track_name(r['track']):12s} {r['n']:2d} {tag:>8s} {r['why']:>16s} {r['stranded']:8d} "
              + " ".join(fmt(r[k]) for k in TABLE_KEYS))
    unfinished = [r for r in rows if r["ok"] and r["why"] == "frame-cap"]
    if unfinished:
        print(f"note: {len(unfinished)} race(s) hit the frame cap before player 1 finished")

    # ---- means per group, tracks weighted equally ----
    print()
    ok = [r for r in rows if r["ok"]]
    per_track = {}
    for r in ok:
        per_track.setdefault((group_of(r), r["track"]), []).append(r)

    def track_mean(group, track, key):
        return mean([r[key] for r in per_track.get((group, track), [])])

    for group in sorted({group_of(r) for r in ok}):
        tracks = sorted({t for (g, t) in per_track if g == group})
        agg = {k: mean([track_mean(group, t, k) for t in tracks]) for k in TABLE_KEYS}
        races = sum(len(per_track[(group, t)]) for t in tracks)
        stranded = sum(r["stranded"] for r in ok if group_of(r) == group)
        print(f"ALL {labels.get(group, group)} (tracks={len(tracks)} races={races} stranded={stranded}) "
              + " ".join(f"{k}={agg[k]:.2f}" if agg[k] is not None else f"{k}=-" for k in TABLE_KEYS))

    if len(groups) != 2 or groups[0] == groups[1]:
        return
    a, b = groups

    # ---- per-track comparison and stranded cars ----
    print()
    print(f"per-track means ({labels.get(a, a)} -> {labels.get(b, b)}), stranded = CPU cars stuck "
          f"> {100 * args.stranded:.0f}% of the race")
    totals = {k: ([], []) for k in COMPARE_KEYS}
    for t in sorted({t for (_, t) in per_track}):
        line = f"{track_name(t):12s}"
        for k in COMPARE_KEYS:
            x, y = track_mean(a, t, k), track_mean(b, t, k)
            if x is not None and y is not None:
                line += f"  {k[:9]}={x:6.2f}->{y:6.2f}"
                totals[k][0].append(x)
                totals[k][1].append(y)
        ra, rb = per_track.get((a, t), []), per_track.get((b, t), [])
        line += (f"  races={len(ra)}/{len(rb)} stranded {sum(r['stranded'] for r in ra)}"
                 f"->{sum(r['stranded'] for r in rb)}")
        print(line)
    print()
    for k in COMPARE_KEYS:
        x, y = mean(totals[k][0]), mean(totals[k][1])
        if x is None or y is None:
            continue
        change = f"{100 * (y - x) / x:+6.1f}%" if x else "     n/a"
        print(f"{k:16s} {labels.get(a, a):>10s} {x:7.2f}   {labels.get(b, b):>10s} {y:7.2f}   change {change}")


if __name__ == "__main__":
    main()
