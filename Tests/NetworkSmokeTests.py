#!/usr/bin/env python3
"""Race a host and its LAN clients on loopback, headless, and refuse one join too many.

usage: NetworkSmokeTests.py BINARY [PLAYERS] [--track N] [--frames K] [--verbose]

Every instance is a real game process (use a sanitizer build). Clients join with
--join-address, so no LAN discovery is involved. PLAYERS counts the host and
defaults to the most the binary seats (--print-max-net-players). At that
capacity, one more client is started and must be turned away as full.
"""

import argparse
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading
import time

FAILURE_MARKERS = ("ERROR: AddressSanitizer", "ERROR: LeakSanitizer", "runtime error:",
                   "Game Fatal Alert:")
TIMEOUT_SECONDS = 300


class Instance:
    """One game process whose combined output is collected on a reader thread."""

    def __init__(self, name: str, binary: Path, args: list[str], scratch: Path, epoch: float):
        self.name = name
        self.args = args
        self.epoch = epoch
        self.lines: list[str] = []
        self.stamped: list[str] = []  # the same lines, prefixed with seconds since the test began
        self.lobby_open = False
        self.lobby_or_exit = threading.Event()
        home = scratch / name
        home.mkdir()
        env = os.environ.copy()
        env.update(
            SDL_VIDEODRIVER="offscreen",
            SDL_AUDIODRIVER="dummy",
            LIBGL_ALWAYS_SOFTWARE="1",
            XDG_CONFIG_HOME=str(home),
            XDG_CACHE_HOME=str(home),
            UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1",
            # Mesa/EGL retains process-lifetime allocations after SDL unloads the
            # driver; see StartupSmokeTests.py. ASan/UBSan errors stay fatal.
            ASAN_OPTIONS="halt_on_error=1:detect_leaks=0",
        )
        env.setdefault("LP_NUM_THREADS", "2")  # several software renderers share the CPU
        self.process = subprocess.Popen([str(binary), *args], env=env, stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT, text=True, errors="replace")
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()

    def _read(self) -> None:
        for line in self.process.stdout:
            self.lines.append(line)
            self.stamped.append(f"{time.monotonic() - self.epoch:7.2f} {line}")
            if "SMOKE: host lobby open on port" in line:
                self.lobby_open = True
                self.lobby_or_exit.set()
        self.lobby_or_exit.set()

    def wait(self, deadline: float) -> None:
        try:
            self.process.wait(timeout=max(1.0, deadline - time.monotonic()))
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
            self.lines.append(f"<killed after {TIMEOUT_SECONDS} s>\n")
            self.stamped.append(self.lines[-1])
        self.reader.join()

    @property
    def output(self) -> str:
        return "".join(self.lines)

    def report(self) -> str:
        # SDL_Log lines (stderr) arrive as they happen; printf lines (stdout) are
        # buffered in the pipe and may only arrive at exit.
        return (f"===== {self.name}: exit {self.process.returncode}, args {' '.join(self.args)}\n"
                f"{''.join(self.stamped)}")


def max_net_players(binary: Path) -> int:
    result = subprocess.run([str(binary), "--print-max-net-players"], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, timeout=30, check=True)
    return int(result.stdout.split()[-1])


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def race(binary: Path, players: int, capacity: int, track: int, frames: int,
         verbose: bool) -> None:
    refuse = players == capacity  # only a full game turns joins away
    port = free_port()
    started = time.monotonic()
    deadline = started + TIMEOUT_SECONDS
    instances: list[Instance] = []
    with tempfile.TemporaryDirectory(prefix="cmr-netsmoke-",
                                     dir=os.environ.get("TMPDIR") or "/var/tmp") as scratch:
        try:
            host_args = ["--host", "--track", str(track), "--port", str(port), "--no-vsync",
                         "--smoke-test-frames", str(frames), "--smoke-net-players", str(players)]
            if refuse:
                host_args += ["--smoke-net-refusals", "1"]
            host = Instance("host", binary, host_args, Path(scratch), started)
            instances.append(host)
            host.lobby_or_exit.wait(timeout=max(1.0, deadline - time.monotonic()))
            if not host.lobby_open:
                raise AssertionError("host lobby never opened")

            # Start every client at once; at capacity one of them finds no seat left.
            client_args = ["--join-address", f"127.0.0.1:{port}", "--no-vsync",
                           "--smoke-test-frames", str(frames)]
            for number in range(1, players + (1 if refuse else 0)):
                instances.append(Instance(f"client{number}", binary, client_args, Path(scratch),
                                          started))
            for instance in instances:
                instance.wait(deadline)
            check(instances, players, track, frames, refuse)
            if verbose:
                for instance in instances:
                    sys.stdout.write(instance.report())
        except BaseException:
            for instance in instances:
                if instance.process.poll() is None:
                    instance.process.kill()
                    instance.wait(deadline)
            for instance in instances:
                sys.stderr.write(instance.report())
            raise
    print(f"PASS: {players}-player LAN race on track {track}, {frames} frames"
          f"{', one extra join refused' if refuse else ''} ({time.monotonic() - started:.1f} s)",
          flush=True)


def check(instances: list[Instance], players: int, track: int, frames: int, refuse: bool) -> None:
    for instance in instances:
        for marker in FAILURE_MARKERS:
            if marker in instance.output:
                raise AssertionError(f"{instance.name} reported {marker!r}")

    host, clients = instances[0], instances[1:]
    marker = f"SMOKE: net race track {track} player {{}}/{players} simulated {frames} frames"
    if (host.process.returncode != 0 or marker.format(1) not in host.output
            or "SMOKE: net race host saw all clients leave" not in host.output):
        raise AssertionError("host did not finish the race after every client left")

    seated = [c for c in clients if c.process.returncode == 0]
    refused = [c for c in clients if c.process.returncode != 0]
    numbers = sorted(n for c in seated for n in range(2, players + 1) if marker.format(n) in c.output)
    if numbers != list(range(2, players + 1)) or len(seated) != players - 1:
        raise AssertionError(f"expected clients 2..{players} to race, got {numbers}")

    if not refuse:
        if refused:
            raise AssertionError(f"{refused[0].name} failed")
        return
    if "A new client wants to connect, but the game is full!" not in host.output:
        raise AssertionError("host did not report the extra join as full")
    # The extra client leaves cleanly: an ordinary nonzero exit (not a signal) that
    # names the reason, without starting the race.
    if (len(refused) != 1 or refused[0].process.returncode != 1
            or "SMOKE: net session ended: THE GAME IS FULL." not in refused[0].output
            or "SMOKE: net race" in refused[0].output):
        raise AssertionError("expected exactly one client to be refused cleanly as full")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("binary", type=Path)
    parser.add_argument("players", type=int, nargs="?",
                        help="host plus clients (default: the most the binary seats)")
    parser.add_argument("--track", type=int, default=1, help="race track 1..9 (default 1)")
    parser.add_argument("--frames", type=int, default=120, help="race frames per instance")
    parser.add_argument("--verbose", action="store_true", help="print every instance's output")
    args = parser.parse_args()

    binary = args.binary.resolve(strict=True)
    capacity = max_net_players(binary)
    players = capacity if args.players is None else args.players
    if not 2 <= players <= capacity:
        parser.error(f"players must be 2..{capacity}")
    race(binary, players, capacity, args.track, args.frames, args.verbose)


if __name__ == "__main__":
    main()
