#!/usr/bin/env python3
"""Only reviewed code may draw the synced simulation RNG.

MyRandomLong, RandomRange, RandomFloat and RandomFloat2 advance the RNG that every
network peer must keep in step: the host sends its next value each frame and a client
whose value differs stops with a seed desync. A draw that depends on anything local
(a menu, a sound, a camera, a position that can differ slightly between peers) breaks
that. Such code uses VisualRandom* or DeterministicSimEventFloat instead. Each function
below was reviewed to draw the same number of times on every peer.

usage: SyncedRandomTests.py SOURCE_ROOT
"""

from pathlib import Path
import re
import sys

SYNCED = re.compile(r"\b(MyRandomLong|RandomRange|RandomFloat2?)\s*\(")
FUNCTION = re.compile(r"^[A-Za-z_][\w \t\*]*?\b(\w+)\s*\([^;]*$")
# A declaration or definition of one of those functions (a type right before the name),
# which is not a draw. "float x = RandomFloat();" and "return RandomRange(...)" are.
DECLARATION = re.compile(r"^\s*(?:extern\s+)?(?:static\s+)?(?:inline\s+)?(?!return\b|else\b|case\b)"
                         r"[A-Za-z_]\w*[\s\*]+(?:MyRandomLong|RandomRange|RandomFloat2?)\s*\(")
ALLOWED = {
    ("Source/System/Misc.c", "RandomRange"),  # the RNG itself
    ("Source/System/Misc.c", "RandomFloat2"),
    ("Source/Network/NetHigh.c", "HostSend_ControlInfoToClients"),  # the per-frame seed check
    ("Source/Network/NetHigh.c", "Client_InGame_HandleHostControlInfoMessage"),
    ("Source/Player/Player.c", "SyncedCPUVehicleRandom"),  # local games only; network games share a picker
    ("Source/Player/Player.c", "ChooseTaggedPlayer"),  # applied at one frame on every peer
    ("Source/Player/Player_Car.c", "SetPhysicsForVehicleType"),  # every car, in slot order, at level start
    ("Source/System/Main.c", "PlayGame_Practice"),  # the self-running demo's track and car
}


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def draws(root: Path) -> set[tuple[str, str]]:
    found = set()
    for path in sorted((root / "Source").rglob("*")):
        if path.suffix not in (".c", ".cpp", ".h"):
            continue
        name = path.relative_to(root).as_posix()
        function = "<file scope>"
        for line in strip_comments(path.read_text(encoding="utf-8", errors="replace")).splitlines():
            match = FUNCTION.match(line)
            if match:
                function = match.group(1)
            if SYNCED.search(line) and not DECLARATION.match(line):
                found.add((name, function))
    return found


def check_filter() -> None:
    for line in ("extern uint32_t MyRandomLong(void);", "extern\tfloat RandomFloat(void);",
                 "uint16_t\tRandomRange(unsigned short min, unsigned short max)", "float RandomFloat2(void);"):
        if not DECLARATION.match(line):
            raise AssertionError(f"declaration counted as a draw: {line!r}")
    for line in ("float x = RandomFloat();", "\tuint32_t seed = MyRandomLong();", "\treturn RandomRange(0, 5);",
                 "\tRandomRange(0, 5);", "\tconst float f = 2 * RandomFloat2();"):
        if not SYNCED.search(line) or DECLARATION.match(line):
            raise AssertionError(f"draw not counted: {line!r}")


def main() -> None:
    check_filter()
    root = Path(sys.argv[1])
    found = draws(root)
    unexpected = sorted(found - ALLOWED)
    if unexpected:
        raise AssertionError("synced RNG drawn by unreviewed code (use VisualRandom* or "
                             f"DeterministicSimEventFloat): {unexpected}")
    if not {("Source/Network/NetHigh.c", "HostSend_ControlInfoToClients")} <= found:
        raise AssertionError("the scan no longer finds the seed check; fix the scanner")
    print(f"PASS: {len(found)} reviewed functions draw the synced RNG")


if __name__ == "__main__":
    main()
