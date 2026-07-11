#!/usr/bin/env python3

import csv
from pathlib import Path
import re
import sys


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
        r"^\s*STR_[A-Z0-9_]+(?:\s*=\s*[^,]+)?\s*,",
        enum_body,
        flags=re.MULTILINE,
    )
    if not string_ids or "STR_NULL" not in string_ids[0]:
        fail("LocStrID must begin with STR_NULL")

    with (root / "Data/System/strings.csv").open(
        "r", encoding="utf-8-sig", newline=""
    ) as strings_file:
        localized_rows = sum(
            1 for row in csv.reader(strings_file) if any(field for field in row)
        )

    expected_rows = len(string_ids) - 1  # STR_NULL has no CSV row.
    if localized_rows != expected_rows:
        fail(
            "strings.csv has "
            f"{localized_rows} localized rows, but LocStrID requires {expected_rows}"
        )


if __name__ == "__main__":
    main()
