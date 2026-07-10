#!/usr/bin/env python3

from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET


def fail(message: str) -> None:
    raise AssertionError(message)


def read_properties(path: Path) -> dict[str, str]:
    properties: dict[str, str] = {}
    for line_number, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        key, separator, value = line.partition("=")
        if not separator or not key or not value:
            fail(f"Malformed property at {path}:{line_number}")
        if key in properties:
            fail(f"Duplicate property {key} at {path}:{line_number}")
        properties[key] = value
    return properties


def numeric_version(version: str) -> tuple[int, int, int]:
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
        fail(f"Not a three-part numeric version: {version!r}")
    return tuple(int(part) for part in version.split("."))  # type: ignore[return-value]


def require_text(path: Path, needle: str) -> None:
    if needle not in path.read_text(encoding="utf-8"):
        fail(f"{path} does not reference {needle!r}")


def main() -> None:
    root = Path(sys.argv[1]).resolve()
    properties = read_properties(root / "version.properties")

    for key in (
        "GAME_NAME",
        "GAME_FULL_NAME",
        "GAME_IDENTIFIER",
        "GAME_VERSION",
        "ANDROID_VERSION_CODE",
        "APPLE_BUILD_NUMBER",
    ):
        if not properties.get(key):
            fail(f"Missing required property: {key}")

    game_version = properties["GAME_VERSION"]
    game_version_tuple = numeric_version(game_version)
    if int(properties["ANDROID_VERSION_CODE"]) <= 0:
        fail("ANDROID_VERSION_CODE must be positive")
    if int(properties["APPLE_BUILD_NUMBER"]) <= 0:
        fail("APPLE_BUILD_NUMBER must be positive")

    if (root / "Source/Headers/version.h").exists():
        fail("Source/Headers/version.h must be generated in the build tree, not committed")

    cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    if re.search(r"set\s*\(\s*GAME_VERSION\s+", cmake):
        fail("CMakeLists.txt contains a second GAME_VERSION definition")
    for key in ("GAME_NAME", "GAME_FULL_NAME", "GAME_IDENTIFIER", "GAME_VERSION"):
        if f"cmr_read_property({key} " not in cmake:
            fail(f"CMake does not consume canonical property {key}")

    require_text(root / "build.py", "version.properties")
    require_text(root / "build_ios.sh", "version.properties")
    require_text(root / "build_tvos.sh", "version.properties")
    require_text(root / ".github/workflows/ReleaseBuilds.yml", "version.properties")

    gradle = (root / "AndroidBuild/app/build.gradle").read_text(encoding="utf-8")
    for key in ("GAME_VERSION", "ANDROID_VERSION_CODE"):
        if key not in gradle:
            fail(f"Android Gradle config does not consume {key}")
    manifest = (root / "AndroidBuild/app/src/main/AndroidManifest.xml").read_text(encoding="utf-8")
    if "android:versionCode" in manifest or "android:versionName" in manifest:
        fail("Android manifest duplicates Gradle version metadata")

    for plist in (
        root / "iOSBuild/CroMagRally/Info.plist",
        root / "tvOSBuild/CroMagRally/Info.plist",
    ):
        require_text(plist, "$(MARKETING_VERSION)")
        require_text(plist, "$(CURRENT_PROJECT_VERSION)")

    pkgbuild = (root / "packaging/PKGBUILD").read_text(encoding="utf-8")
    pkgver_match = re.search(r"^pkgver=(.+)$", pkgbuild, re.MULTILINE)
    if not pkgver_match or pkgver_match.group(1) != game_version:
        fail("PKGBUILD's bootstrap pkgver does not match GAME_VERSION")
    require_text(root / "packaging/PKGBUILD", "version.properties")

    changelog = (root / "CHANGELOG.md").read_text(encoding="utf-8")
    if f"**{game_version} (NOT RELEASED YET)**" not in changelog and f"**{game_version} (" not in changelog:
        fail("Changelog has no entry for GAME_VERSION")

    appstream_root = ET.parse(root / "packaging/io.jor.cromagrally.appdata.xml").getroot()
    released_versions = [
        numeric_version(release.attrib["version"])
        for release in appstream_root.findall("./releases/release")
    ]
    if released_versions and max(released_versions) > game_version_tuple:
        fail("AppStream advertises a release newer than GAME_VERSION")

    print(f"Version metadata is consistent for {game_version}.")


if __name__ == "__main__":
    main()
