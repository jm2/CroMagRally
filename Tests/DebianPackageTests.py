"""Check real DEB dependency metadata and private-library layouts.

With --install (as root, in a disposable Debian container), also install each
package, check which SDL the installed game loads, run a short headless race
through the installed launcher, and purge the package again.
"""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

MIN_SDL_VERSION = (3, 2, 0)


def field(package, name):
    return subprocess.check_output(["dpkg-deb", "--field", str(package), name], text=True).strip()


def verify(package, bundled):
    contents = subprocess.check_output(["dpkg-deb", "--contents", str(package)], text=True)
    dependencies = field(package, "Depends")
    private_sdl = bool(re.search(r"\./usr/lib(?:exec)?/cromagrally/libSDL3\.so", contents))
    external_sdl = bool(re.search(r"(?:^|,)\s*libsdl3-0(?:\s|,|$)", dependencies))
    # dpkg-shlibdeps takes the minimum version from the library's symbols file. SDL 3.2.0
    # is SDL3's first stable ABI, so anything lower would admit a preview release.
    sdl_minimum = re.search(r"(?:^|,)\s*libsdl3-0\s*\(>=\s*(\d+)\.(\d+)\.(\d+)[^)]*\)", dependencies)
    versioned_sdl = bool(sdl_minimum) and tuple(map(int, sdl_minimum.groups())) >= MIN_SDL_VERSION
    assert private_sdl == bundled, (package, contents)
    assert external_sdl != bundled, (package, dependencies)
    assert versioned_sdl != bundled, (package, dependencies)
    assert "./usr/bin/cromagrally" in contents
    assert "./usr/share/cromagrally/Data/" in contents
    print(f"PASS: {package.name}: bundled={bundled}; Depends: {dependencies}")


def run_installed(package, bundled):
    subprocess.run(["apt-get", "install", "-y", "--no-install-recommends", str(package)], check=True)
    try:
        libs = subprocess.check_output(["ldd", "/usr/lib/cromagrally/CroMagRally"], text=True)
        assert "not found" not in libs, libs
        sdl = re.search(r"libSDL3\.so\.0 => (\S+)", libs)
        assert sdl, libs
        assert sdl.group(1).startswith("/usr/lib/cromagrally/") == bundled, libs

        with tempfile.TemporaryDirectory() as home:
            env = dict(os.environ, HOME=home, XDG_CONFIG_HOME=home, XDG_CACHE_HOME=home, XDG_DATA_HOME=home,
                       SDL_VIDEODRIVER="offscreen", SDL_AUDIODRIVER="dummy", LIBGL_ALWAYS_SOFTWARE="1")
            race = subprocess.run(["cromagrally", "--track", "1", "--no-vsync", "--smoke-test-frames", "3"],
                                  env=env, text=True, capture_output=True, timeout=300)
        output = race.stdout + race.stderr
        assert race.returncode == 0, output
        assert "SMOKE: practice track 1 rendered 3 frames" in output, output
        print(f"PASS: installed {package.name}: loads {sdl.group(1)} and races through the launcher")
    finally:
        subprocess.run(["apt-get", "purge", "-y", "cromagrally"], check=True)


if __name__ == "__main__":
    install = "--install" in sys.argv[1:]
    bundled_deb, system_deb = (Path(arg).resolve(strict=True) for arg in sys.argv[1:] if arg != "--install")
    for package, bundled in ((bundled_deb, True), (system_deb, False)):
        verify(package, bundled)
        if install:
            run_installed(package, bundled)
