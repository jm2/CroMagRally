#!/usr/bin/env python3
"""Generate malformed resource fixtures and exercise the production asset loader."""

import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile


def resource_locations(data: bytes) -> dict[tuple[bytes, int], tuple[int, int, int]]:
    """Return (payload offset, payload length, reference offset) for AppleDouble resources."""
    fork = 0
    if data[:4] == b"\x00\x05\x16\x07":
        for entry in range(struct.unpack_from(">H", data, 24)[0]):
            kind, offset, _ = struct.unpack_from(">III", data, 26 + 12 * entry)
            if kind == 2:
                fork = offset
                break
    payload, resource_map = struct.unpack_from(">II", data, fork)
    payload += fork
    resource_map += fork
    types = resource_map + struct.unpack_from(">H", data, resource_map + 24)[0]
    result = {}
    for index in range(struct.unpack_from(">H", data, types)[0] + 1):
        kind, count, refs = struct.unpack_from(">4sHH", data, types + 2 + 8 * index)
        for item in range(count + 1):
            ref = types + refs + 12 * item
            resource_id, _, packed_offset = struct.unpack_from(">hHI", data, ref)
            offset = payload + (packed_offset & 0xFFFFFF)
            size = struct.unpack_from(">I", data, offset)[0]
            result[kind, resource_id] = (offset + 4, size, ref)
    return result


def run_fixture(binary: Path, source: Path, asset: str, kind: bytes, offset: int,
                fmt: str, value: int | float, expected: str, operation: str = "field",
                accept: bool = False, resource_id: int = 1000, track: int = 1) -> None:
    original = (source / "Data" / asset).read_bytes()
    changed = bytearray(original)
    start, _, ref = resource_locations(original)[kind, resource_id]
    if operation == "size":
        start -= 4
    elif operation == "missing":
        start = ref
    struct.pack_into(fmt, changed, start + offset, value)

    with tempfile.TemporaryDirectory(prefix="cmr-malformed-", dir=os.environ.get("TMPDIR") or "/var/tmp") as scratch:
        root = Path(scratch)
        # Only the mutated resource is copied. All other assets remain read-only
        # links to the checked-in data, so tests never modify the real game data.
        shutil.copytree(source / "Data", root / "Data", copy_function=os.symlink)
        fixture = root / "Data" / asset
        fixture.unlink()
        fixture.write_bytes(changed)
        executable = root / "CroMagRally"
        executable.symlink_to(binary)
        prefs = root / "prefs"
        prefs.mkdir()
        env = os.environ.copy()
        env.update(SDL_VIDEODRIVER="offscreen", SDL_AUDIODRIVER="dummy",
                   LIBGL_ALWAYS_SOFTWARE="1", XDG_CONFIG_HOME=str(prefs),
                   XDG_CACHE_HOME=str(prefs), ASAN_OPTIONS="halt_on_error=1:detect_leaks=0",
                   UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
        result = subprocess.run([str(executable), "--track", str(track), "--no-vsync", "--smoke-test-frames", "1"],
                                env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, timeout=45)
        output = result.stdout
        wrong_outcome = ((result.returncode != 0 or "Game Fatal Alert:" in output) if accept
                         else (result.returncode < 0 or "SMOKE:" in output))
        if (wrong_outcome or expected not in output
                or "ERROR: AddressSanitizer" in output or "runtime error:" in output):
            raise AssertionError(f"Unsafe or missing rejection of {asset} {kind} {operation} {offset}={value}:\n{output}")
        print(f"PASS: {asset} {kind.decode()} {operation} {offset}={value}", flush=True)


def main() -> None:
    binary = Path(sys.argv[1]).resolve(strict=True)
    source = Path(__file__).resolve().parents[1]
    terrain = "Terrain/StoneAge_Desert.ter.rsrc"
    skeleton = "Skeletons/Brog.skeleton.rsrc"

    for offset in (4, 8, 12, 16, 20, 36, 40, 44, 48, 52):
        run_fixture(binary, source, terrain, b"Hedr", offset, ">i", -1, "Invalid playfield header")
    for offset, value in ((4, 65536), (8, 408), (12, 2147483647), (36, 257), (40, 61), (52, 0)):
        expected = "race track requires checkpoints" if offset == 52 else "Invalid playfield header"
        run_fixture(binary, source, terrain, b"Hedr", offset, ">i", value, expected)
    for value in (0.0, float("nan")):
        run_fixture(binary, source, terrain, b"Hedr", 24, ">f", value, "Invalid playfield header")
    run_fixture(binary, source, terrain, b"Layr", 0, ">H", 65535, "Invalid map layer tile ID")
    run_fixture(binary, source, terrain, b"Itms", 8, ">H", 65535, "Invalid terrain item type")
    for value in (-1, 0, 1, 81):
        run_fixture(binary, source, terrain, b"Fenc", 2, ">h", value, "Invalid fence nub count")
    for kind in (b"Hedr", b"Layr", b"YCrd", b"Itms", b"FnNb"):
        run_fixture(binary, source, terrain, kind, 0, ">I", 1, "Invalid resource size/count", "size")
    for kind, expected in ((b"FnNb", "cant get fence nub rez"), (b"CkPt", "Missing checkpoint list resource")):
        run_fixture(binary, source, terrain, kind, 0, ">h", -32768, expected, "missing")

    for offset, value in ((2, -1), (2, 26), (2, 256), (4, -1), (4, 21)):
        run_fixture(binary, source, skeleton, b"Hedr", offset, ">h", value, "Invalid skeleton animation/joint count")
    for value in (-1, 31, 256):
        run_fixture(binary, source, skeleton, b"AnHd", 34, ">h", value, "Invalid skeleton animation event count")
    for kind, expected in ((b"BonP", "Invalid skeleton point index"), (b"BonN", "Invalid skeleton normal index")):
        run_fixture(binary, source, skeleton, kind, 0, ">H", 65535, expected)
    run_fixture(binary, source, skeleton, b"Bone", 0, ">i", 0, "Invalid skeleton bone definition")
    run_fixture(binary, source, skeleton, b"Bone", 0, ">i", 1, "Cyclic skeleton bone hierarchy")
    run_fixture(binary, source, skeleton, b"NumK", 0, ">B", 255, "Invalid skeleton keyframe count")
    for kind in (b"Hedr", b"Bone", b"BonP", b"BonN", b"AnHd", b"Evnt", b"NumK", b"KeyF", b"RelP"):
        run_fixture(binary, source, skeleton, kind, 0, ">I", 0, "Invalid resource size/count", "size")
    # Removing Brog's authoring attachment for live normal 15 is repaired from
    # the mesh's point references; the normal-list unit test checks coverage.
    run_fixture(binary, source, skeleton, b"BonN", 0, ">H", 799,
                "SMOKE: practice track 1 rendered 1 frames", accept=True)
    # Troll has 277 live normals; BonN 1004 begins with obsolete authoring slot
    # 279. Replacing that unused entry preserves every live-normal attachment.
    troll = "Skeletons/Troll.skeleton.rsrc"
    troll_bytes = (source / "Data" / troll).read_bytes()
    stale_start, _, _ = resource_locations(troll_bytes)[b"BonN", 1004]
    assert struct.unpack_from(">H", troll_bytes, stale_start)[0] == 279
    run_fixture(binary, source, troll, b"BonN", 0, ">H", 799,
                "SMOKE: practice track 8 rendered 1 frames", accept=True, resource_id=1004, track=8)


if __name__ == "__main__":
    main()
