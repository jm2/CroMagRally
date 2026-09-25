#!/usr/bin/env python3
"""Exercise real CLI startup, resource loading, rendering, and teardown on Linux."""

import os
from pathlib import Path
import re
import socket
import subprocess
import sys
import tempfile


def run(binary: Path, args: list[str], marker: str | None = None,
        rejection: str | None = None) -> str:
    with tempfile.TemporaryDirectory(prefix="cmr-smoke-", dir=os.environ.get("TMPDIR") or "/var/tmp") as scratch:
        env = os.environ.copy()
        env.update(
            SDL_VIDEODRIVER="offscreen",
            SDL_AUDIODRIVER="dummy",
            LIBGL_ALWAYS_SOFTWARE="1",
            XDG_CONFIG_HOME=scratch,
            XDG_CACHE_HOME=scratch,
            UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1",
            # Mesa/EGL retains process-lifetime allocations after SDL unloads the
            # driver (368 bytes on the local llvmpipe backend). Keep ASan/UBSan
            # errors fatal; leak detection remains enabled in the unit suite.
            ASAN_OPTIONS="halt_on_error=1:detect_leaks=0",
        )
        result = subprocess.run([str(binary), *args], env=env,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, timeout=45)
        output = result.stdout
        if rejection is not None:
            passed = result.returncode != 0 and rejection in output
        else:
            passed = result.returncode == 0 and marker in output
        if (not passed or "ERROR: AddressSanitizer" in output
                or "ERROR: LeakSanitizer" in output or "runtime error:" in output
                or "Game Fatal Alert:" in output):
            raise AssertionError(f"Startup failed for {args} (exit {result.returncode}):\n{output}")
        print(f"PASS: {' '.join(args)}", flush=True)
        return output


def metrics_lines(output: str) -> list[dict[str, str]]:
    """The METRICS lines of a --smoke-metrics run (Source/Headers/race_metrics.h) as key/value dicts."""
    lines = []
    for match in re.finditer(r"METRICS (race|car) (.*)", output):
        fields = dict(re.findall(r"(\w+)=(\S+)", match.group(2)))
        fields["kind"] = match.group(1)
        lines.append(fields)
    return lines


def check_soak_flags(binary: Path, capacity: int) -> None:
    """Practice soak flags (measurement races) are smoke-only and validated strictly."""
    practice = ["--track", "1", "--smoke-test-frames", "3"]
    for flag, value in (("--smoke-autopilot", None), ("--smoke-cars", "2"), ("--smoke-fixed-fps", "60"),
                        ("--smoke-seed", "1"), ("--smoke-until-finish", None), ("--smoke-metrics", None)):
        option = [flag] if value is None else [flag, value]
        message = f"{flag} requires --smoke-test-frames and --track, without --host or --join"
        for args in (["--track", "1"], ["--host", *practice],
                     ["--join-address", "127.0.0.1", "--smoke-test-frames", "3"]):
            run(binary, [*args, *option], rejection=message)

    # Every LAN seat needs a car; the upper limit is MAX_PLAYERS.
    output = run(binary, [*practice, "--smoke-cars", "0"], rejection="Invalid --smoke-cars value '0' (expected 1..")
    max_cars = int(re.search(r"Invalid --smoke-cars value '0' \(expected 1\.\.(\d+)\)", output).group(1))
    if max_cars < capacity:
        raise AssertionError(f"--smoke-cars allows {max_cars} cars but a LAN game seats {capacity}")
    for flag, values in (("--smoke-cars", (str(max_cars + 1), "-1", "garbage")),
                         ("--smoke-fixed-fps", ("0", "8", "1001", "60fps")),   # NET_MIN_FPS..MAX_GAME_FPS
                         ("--smoke-seed", ("-1", "2147483648", "0x10", ""))):
        for value in values:
            run(binary, [*practice, flag, value], rejection=f"Invalid {flag} value '{value}'")
    for flag in ("--smoke-cars", "--smoke-fixed-fps", "--smoke-seed"):
        run(binary, [*practice, flag], rejection=f"{flag} requires a value")

    # Only soak runs may go past the CI frame limit.
    run(binary, ["--track", "1", "--smoke-test-frames", "36001"],
        rejection="Invalid --smoke-test-frames value '36001' (expected 1..36000)")
    run(binary, ["--track", "1", "--smoke-test-frames", "100001", "--smoke-metrics"],
        rejection="Invalid --smoke-test-frames value '100001' (expected 1..100000)")
    run(binary, ["--track", "1", "--smoke-test-frames"], rejection="--smoke-test-frames requires a value")

    # A short soak: 100 frames at a fixed 10 Hz leave a few seconds of racing after the
    # starting light. The AI drives player 1 over the start line, and a pinned seed and
    # timestep make the metrics repeat exactly.
    soak = ["--track", "1", "--no-vsync", "--smoke-test-frames", "100", "--smoke-fixed-fps", "10",
            "--smoke-seed", "7", "--smoke-autopilot", "--smoke-cars", "2", "--smoke-metrics",
            "--smoke-until-finish"]
    runs = [metrics_lines(run(binary, soak, "SMOKE: practice track 1 rendered 100 frames")) for _ in range(2)]
    race, *cars = runs[0]
    if (race["kind"] != "race" or (race["track"], race["cars"], race["why"]) != ("1", "2", "frame-cap")
            or not float(race["racetime"]) > 0 or [car["kind"] for car in cars] != ["car", "car"]
            or [(car["p"], car["cpu"]) for car in cars] != [("0", "0"), ("1", "1")]
            or float(cars[0]["lap0"]) < 0):
        raise AssertionError(f"Unexpected soak metrics: {runs[0]}")
    if runs[1] != runs[0]:
        raise AssertionError(f"Soak metrics differ between identical runs:\n{runs[0]}\n{runs[1]}")


def main() -> None:
    binary = Path(sys.argv[1]).resolve(strict=True)
    for host in (False, True):
        prefix = ["--host"] if host else []
        for track in ("0", "10", "17", "18", "-1", "garbage"):
            run(binary, [*prefix, "--track", track], rejection="Invalid --track")
        for track in range(1, 10):
            mode = "host lobby" if host else "practice"
            run(binary, [*prefix, "--track", str(track), "--no-vsync",
                         "--smoke-test-frames", "3"],
                f"SMOKE: {mode} track {track} rendered 3 frames")

    # Smoke-only local split-screen multiplayer races, 2..4 humans (MAX_LOCAL_PLAYERS),
    # humans only or with CPU cars filling every other slot.
    filled_grids = set()
    for players, track in ((2, 1), (3, 9), (4, 5)):
        local = ["--track", str(track), "--no-vsync", "--smoke-test-frames", "3",
                 "--smoke-local-players", str(players)]
        run(binary, local,
            f"SMOKE: local race track {track} with {players} players and {players} cars rendered 3 frames")
        output = run(binary, [*local, "--smoke-cpu-fill"], f"SMOKE: local race track {track} with {players} players")
        filled_grids.add(int(re.search(r"with \d+ players and (\d+) cars", output).group(1)))
    if filled_grids != {6}:  # fresh prefs: the default, original six-car grid
        raise AssertionError(f"CPU fill should fill the six-car grid every time, got {filled_grids} cars")
    run(binary, ["--track", "1", "--smoke-test-frames", "3", "--smoke-cpu-fill"],
        rejection="--smoke-cpu-fill requires --smoke-local-players or --smoke-net-players")
    for players in ("1", "5", "garbage"):
        run(binary, ["--track", "1", "--smoke-test-frames", "3", "--smoke-local-players", players],
            rejection="Invalid --smoke-local-players")
    for args in (["--smoke-local-players", "2"],
                 ["--track", "1", "--smoke-local-players", "2"],
                 ["--host", "--track", "1", "--smoke-test-frames", "3", "--smoke-local-players", "2"]):
        run(binary, args, rejection="--smoke-local-players requires --track and --smoke-test-frames")

    # Dev/test direct join takes a strict IPv4[:PORT] and conflicts with other modes.
    run(binary, ["--join-address"], rejection="--join-address requires a value")
    for address in ("", "localhost", "127.0.0", "127.0.0.1.1", "127..0.1", "256.0.0.1",
                    "127.0.0.01", " 127.0.0.1", "127.0.0.1 ", "+127.0.0.1", "127.0.0.1:",
                    "127.0.0.1:0", "127.0.0.1:65536", "127.0.0.1:-1", "127.0.0.1:+80",
                    "127.0.0.1:80x", "127.0.0.1:80:80", ":80"):
        run(binary, ["--join-address", address], rejection="Invalid --join-address")
    for args, message in (
            (["--host", "--join-address", "127.0.0.1"], "--host and --join-address are mutually exclusive"),
            (["--join", "--join-address", "127.0.0.1"], "--join and --join-address are mutually exclusive"),
            (["--port", "5000", "--join-address", "127.0.0.1:5001"], "--port cannot be combined"),
            (["--join-address", "127.0.0.1", "--track", "1"], "--track cannot be used with --join-address")):
        run(binary, args, rejection=message)

    # Net smoke options (Tests/NetworkSmokeTests.py) are smoke-only and validated strictly.
    capacity = int(subprocess.run([str(binary), "--print-max-net-players"], stdout=subprocess.PIPE,
                                  text=True, timeout=45, check=True).stdout)
    if capacity < 2:
        raise AssertionError(f"--print-max-net-players reported {capacity}")
    host = ["--host", "--track", "1", "--smoke-test-frames", "3"]
    for players in ("1", str(capacity + 1), "garbage"):
        run(binary, [*host, "--smoke-net-players", players], rejection="Invalid --smoke-net-players")
    for args, message in (
            (["--smoke-test-frames", "3"], "--smoke-test-frames requires --track or --join-address"),
            (["--join", "--smoke-test-frames", "3"], "cannot use --join"),
            (["--host", "--track", "1", "--smoke-net-players", "2"],
             "--smoke-net-players requires --host and --smoke-test-frames"),
            (["--track", "1", "--smoke-test-frames", "3", "--smoke-net-players", "2"],
             "--smoke-net-players requires --host and --smoke-test-frames"),
            ([*host, "--smoke-net-refusals", "1"], "--smoke-net-refusals requires --smoke-net-players"),
            # A full lobby holds a supported player limit: 6 (original) or the capacity.
            ([*host, "--smoke-net-players", str(capacity - 1), "--smoke-net-refusals", "1"],
             "--smoke-net-refusals requires --smoke-net-players 6")):
        run(binary, args, rejection=message)

    check_soak_flags(binary, capacity)

    # An unattended client that cannot reach a host ends cleanly, with a nonzero exit.
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    run(binary, ["--join-address", f"127.0.0.1:{port}", "--no-vsync", "--smoke-test-frames", "3"],
        rejection="SMOKE: net session ended: THE HOST HAS BECOME UNREACHABLE.")


if __name__ == "__main__":
    main()
