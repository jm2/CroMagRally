"""Check real DEB dependency metadata and private-library layouts."""
from pathlib import Path
import re
import subprocess
import sys


def field(package, name):
    return subprocess.check_output(["dpkg-deb", "--field", str(package), name], text=True).strip()


def verify(package, bundled):
    contents = subprocess.check_output(["dpkg-deb", "--contents", str(package)], text=True)
    dependencies = field(package, "Depends")
    private_sdl = bool(re.search(r"\./usr/lib(?:exec)?/cromagrally/libSDL3\.so", contents))
    external_sdl = bool(re.search(r"(?:^|,)\s*libsdl3-0(?:\s|,|$)", dependencies))
    assert private_sdl == bundled, (package, contents)
    assert external_sdl != bundled, (package, dependencies)
    assert "./usr/bin/cromagrally" in contents
    assert "./usr/share/cromagrally/Data/" in contents
    print(f"PASS: {package.name}: bundled={bundled}; Depends: {dependencies}")


if __name__ == "__main__":
    verify(Path(sys.argv[1]).resolve(strict=True), bundled=True)
    verify(Path(sys.argv[2]).resolve(strict=True), bundled=False)
