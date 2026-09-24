#!/usr/bin/env python3

import csv
from pathlib import Path
import re
import sys


# English text of rows that must stay at their enum position. LoadLocalizedStrings
# gives each non-empty CSV record the next LocStrID, so a reordered CSV with the
# right row count would show the wrong text. These surround the IDs added for the
# CPU cars option, the 6/12 players option and the GAMEPLAY settings page.
EXPECTED_ENGLISH = {
    "STR_ENGLISH": "ENGLISH",
    "STR_4_MINUTES": "4 MINUTES",
    "STR_CPU_CARS": "CPU CARS",
    "STR_CPU_CARS_HELP": "CPU CARS FILL THE EMPTY SPOTS.\nTHE FIRST PLAYER TO FINISH WINS!",
    "STR_PLAYERS": "PLAYERS",
    "STR_PLAYERS_6_ORIGINAL": "6 [ORIGINAL]",
    "STR_PLAYERS_12": "12",
    "STR_CONTROLS": "CONTROLS",
    "STR_SOUND": "SOUND",
    "STR_GRAPHICS": "GRAPHICS",
    "STR_GAMEPLAY": "GAMEPLAY",
    "STR_FULLSCREEN": "FULLSCREEN",
    "STR_NET_PAUSE": "NET PAUSE",
}


def fail(message: str) -> None:
    raise AssertionError(message)


def main() -> None:
    root = Path(sys.argv[1]).resolve()
    header = (root / "Source/Headers/localization.h").read_text(encoding="utf-8")

    try:
        enum_body = header.split("typedef enum LocStrID", 1)[1].split(
            "NUM_LOCALIZED_STRINGS", 1
        )[0]
    except IndexError:
        fail("Could not find the LocStrID enum")

    string_ids = re.findall(
        r"^\s*(STR_[A-Z0-9_]+)(?:\s*=\s*[^,]+)?\s*,",
        enum_body,
        flags=re.MULTILINE,
    )
    if not string_ids or string_ids[0] != "STR_NULL":
        fail("LocStrID must begin with STR_NULL")

    with (root / "Data/System/strings.csv").open(
        "r", encoding="utf-8-sig", newline=""
    ) as strings_file:
        localized_rows = [
            row for row in csv.reader(strings_file) if any(field for field in row)
        ]

    expected_rows = len(string_ids) - 1  # STR_NULL has no CSV row.
    if len(localized_rows) != expected_rows:
        fail(
            "strings.csv has "
            f"{len(localized_rows)} localized rows, but LocStrID requires {expected_rows}"
        )

    for string_id, english in EXPECTED_ENGLISH.items():
        if string_id not in string_ids:
            fail(f"LocStrID has no {string_id}")
        row = localized_rows[string_ids.index(string_id) - 1]
        if row[0] != english:
            fail(f"{string_id} reads {row[0]!r} in strings.csv, expected {english!r}")


if __name__ == "__main__":
    main()
