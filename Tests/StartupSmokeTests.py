#!/usr/bin/env python3
"""Exercise real CLI startup, resource loading, rendering, and teardown on Linux."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile


def run(binary: Path, args: list[str], marker: str | None = None,
        rejection: str | None = None) -> None:
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


if __name__ == "__main__":
    main()
