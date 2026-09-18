#!/usr/bin/env python3
"""Exercise real CLI startup, resource loading, rendering, and teardown on Linux."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile


def run(binary: Path, args: list[str], marker: str | None) -> None:
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
        if marker is None:
            passed = result.returncode != 0 and "Invalid --track" in output
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
            run(binary, [*prefix, "--track", track], None)
        for track in range(1, 10):
            mode = "host lobby" if host else "practice"
            run(binary, [*prefix, "--track", str(track), "--no-vsync",
                         "--smoke-test-frames", "3"],
                f"SMOKE: {mode} track {track} rendered 3 frames")


if __name__ == "__main__":
    main()
