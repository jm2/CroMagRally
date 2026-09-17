"""Collect a complete release, then upload/verify a private draft before publication."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

BUILD_JOBS = (
    "build-source", "build-linux-appimage", "build-windows", "build-windows-arm64",
    "build-macos", "build-android", "build-ios", "build-tvos", "build-linux-deb",
    "build-linux-rpm", "build-linux-arch", "build-linux-flatpak",
)


def digest(path):
    with path.open("rb") as stream:
        checksum = hashlib.sha256()
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            checksum.update(chunk)
        return checksum.hexdigest()


def expected_assets(version, mac_signed, production):
    prefix = f"CroMagRally-{version}"
    names = {f"{prefix}-{suffix}" for suffix in (
        "source.tar.gz", "windows-x64.zip", "windows-ARM64.zip",
        "ios-unsigned.ipa", "tvos-unsigned.zip", "linux-x86_64.pkg.tar.zst",
    )}
    names.add(f"{prefix}-mac{'' if mac_signed else '-unsigned'}.dmg")
    names.add(f"{prefix}-android{'' if production else '-unsigned'}.apk")
    for arch in ("x86_64", "aarch64"):
        for extension in ("AppImage", "deb", "rpm", "flatpak"):
            names.add(f"{prefix}-linux-{arch}.{extension}")
    return names


def prepare(artifacts, output, version, mac_signed, production):
    expected = expected_assets(version, mac_signed, production)
    files = {}
    for file in artifacts.rglob("*"):
        if not file.is_file():
            continue
        if file.name in files:
            raise RuntimeError(f"Duplicate release asset: {file.name}")
        if file.stat().st_size == 0:
            raise RuntimeError(f"Empty release asset: {file.name}")
        files[file.name] = file
    if files.keys() != expected:
        raise RuntimeError(f"Incomplete release: missing={expected - files.keys()}, unexpected={files.keys() - expected}")
    output.mkdir()  # Refuse to mix with a previous run's upload set.
    for name, file in files.items():
        shutil.copyfile(file, output / name)
    (output / "SHA256SUMS.txt").write_text("".join(
        f"{digest(output / name)}  {name}\n" for name in sorted(files)), encoding="utf-8")


class GitHub:
    def call(self, *args, optional=False):
        result = subprocess.run(["gh", *args], capture_output=True, text=True)
        if result.returncode:
            if optional:
                return None
            raise RuntimeError(result.stderr)
        return result.stdout


def publish(upload, tag, needs, github):
    required = {"release_metadata", "checksums", *BUILD_JOBS}
    if set(needs) != required or any(needs[job]["result"] != "success" for job in required):
        raise RuntimeError("Every required build and checksum job must succeed before publication")
    version = needs["release_metadata"]["outputs"]["game_version"]
    mac_signed = needs["build-macos"]["outputs"].get("signed") == "true"
    if tag != f"v{version}":
        raise RuntimeError("Release tag does not match the built version")
    expected = expected_assets(version, mac_signed, True)
    if {p.name for p in upload.iterdir()} != expected | {"SHA256SUMS.txt"}:
        raise RuntimeError("Incomplete upload set")
    manifest = "".join(f"{digest(upload / name)}  {name}\n" for name in sorted(expected))
    if (upload / "SHA256SUMS.txt").read_text(encoding="utf-8") != manifest:
        raise RuntimeError("Release checksums do not match upload bytes")

    state = github.call("release", "view", tag, "--json", "isDraft,body", optional=True)
    if state is not None and not json.loads(state)["isDraft"]:
        raise RuntimeError("Refusing to modify an already public release")
    notes = ("**macOS: Developer-ID signed and notarized.**\n\n" if mac_signed else
             "**macOS download is unsigned (ad-hoc signed) and not notarized.** "
             "Gatekeeper may block first launch; use macOS Privacy & Security to allow it only if you trust this download.\n\n")
    notes += "iOS and tvOS downloads are unsigned sideload builds and require re-signing before installation.\n\n"
    # All API mutations occur after every local validation above. Upload or download
    # failures leave the release private; the one publication call is last.
    with tempfile.TemporaryDirectory(prefix="cmr-release-", dir=os.environ.get("TMPDIR") or "/var/tmp") as folder:
        scratch = Path(folder)
        notes_file = scratch / "notes.md"
        if state is None:
            notes_file.write_text(notes, encoding="utf-8")
            github.call("release", "create", tag, "--draft", "--verify-tag", "--title", tag,
                        "--generate-notes", "--notes-file", str(notes_file))
        else:
            body = json.loads(state)["body"] or ""
            if not body.startswith(notes):
                body = notes + body
            notes_file.write_text(body, encoding="utf-8")
            github.call("release", "edit", tag, "--draft", "--notes-file", str(notes_file))
        github.call("release", "upload", tag, *[str(p) for p in sorted(upload.iterdir())], "--clobber")
        downloaded = scratch / "downloaded"
        github.call("release", "download", tag, "--dir", str(downloaded))
        if {p.name for p in downloaded.iterdir()} != expected | {"SHA256SUMS.txt"}:
            raise RuntimeError("Draft asset list differs from the complete upload set")
        for original in upload.iterdir():
            if digest(original) != digest(downloaded / original.name):
                raise RuntimeError(f"Draft asset verification failed: {original.name}")
        github.call("release", "edit", tag, "--draft=false", "--verify-tag")


if __name__ == "__main__":
    if sys.argv[1] == "prepare":
        prepare(Path("artifacts"), Path("upload"), os.environ["GAME_VERSION"],
                os.environ.get("MAC_SIGNED") == "true", os.environ["GITHUB_EVENT_NAME"] == "push")
    elif sys.argv[1] == "publish":
        if os.environ["GITHUB_EVENT_NAME"] != "push" or os.environ["GITHUB_REF_TYPE"] != "tag":
            raise RuntimeError("Only pushed version tags may publish releases")
        publish(Path("upload"), os.environ["GITHUB_REF_NAME"], json.loads(os.environ["RELEASE_NEEDS"]), GitHub())
    else:
        raise RuntimeError("Expected prepare or publish")
