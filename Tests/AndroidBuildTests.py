"""Exercise production asset extraction, manifest generation, and both build wrappers."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(sys.argv[1]).resolve()
EXTRACT_TEST = Path(sys.argv[2]).resolve()
CMAKE = str(Path(sys.argv[3]).resolve())


def run(args, **kwargs):
    result = subprocess.run([str(a) for a in args], text=True, capture_output=True, **kwargs)
    if result.returncode:
        raise RuntimeError(f"{args}:\n{result.stdout}\n{result.stderr}")


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


def manifest_test(scratch):
    assets = scratch / "assets"
    write(assets / "Data/z file.bin", "before")
    write(assets / "Data/a.bin", "first")
    command = [CMAKE, f"-DASSETS_DIR={assets}", "-P", ROOT / "packaging/GenerateAssetManifest.cmake"]
    run(command)
    marker = assets / "Data/content.sha256"
    before = marker.read_bytes()
    assert (assets / "Data/files.txt").read_text() == "Data/a.bin\nData/z file.bin\n"
    run(command)
    assert marker.read_bytes() == before, "manifest is not deterministic"
    write(assets / "Data/z file.bin", "after")
    run(command)
    assert marker.read_bytes() != before, "same-version content change did not change identity"
    before = marker.read_bytes()
    (assets / "Data/a.bin").rename(assets / "Data/renamed.bin")
    run(command)
    assert marker.read_bytes() != before, "asset rename did not change identity"
    before = marker.read_bytes()
    for name in ("semi;colon.bin", "nested;dir/asset.bin"):
        invalid = assets / "Data" / name
        write(invalid, "unsupported")
        result = subprocess.run([str(a) for a in command], text=True, capture_output=True)
        assert result.returncode != 0 and "cannot contain semicolons" in result.stderr
        assert marker.read_bytes() == before, "invalid filename changed the current manifest identity"
        invalid.unlink()
        if invalid.parent != assets / "Data":
            invalid.parent.rmdir()


def wrapper_test(scratch, shell, script):
    repo = scratch / script
    repo.mkdir()
    for wrapper in ("build_android.sh", "build_android.ps1"):
        shutil.copy2(ROOT / wrapper, repo / wrapper)
    shutil.copy2(ROOT / "version.properties", repo / "version.properties")
    shutil.copytree(ROOT / "packaging", repo / "packaging")
    write(repo / "extern/SDL3/CMakeLists.txt", "# fixture")
    sdk = repo / "sdk"
    ndk = sdk / "ndk/29.0.14206865"
    write(ndk / "build/cmake/android.toolchain.cmake", "# fixture")
    write(ndk / "source.properties", "Pkg.Revision = 29.0.14206865\n")

    # Only compilation is stubbed. Both wrappers execute their real configure/cache
    # control flow and invoke the actual CMake interpreter for shared helper scripts.
    stub = repo / "bin/cmake"
    write(stub, """#!/usr/bin/env python3
import os, pathlib, subprocess, sys
args = sys.argv[1:]
if '-P' in args:
    sys.exit(subprocess.call([os.environ['CMR_TEST_CMAKE'], *args]))
if '-B' in args:
    build = pathlib.Path(args[args.index('-B') + 1])
    build.mkdir(parents=True, exist_ok=True)
    generator = args[args.index('-G') + 1] if '-G' in args else 'Unix Makefiles'
    cache = build / 'CMakeCache.txt'
    if cache.exists() and ('CMAKE_GENERATOR:INTERNAL=' + generator + '\\n') not in cache.read_text():
        sys.exit('Generator does not match the previous configuration')
    # Real Android toolchains need not set CMAKE_ANDROID_NDK.
    cache.write_text('CMAKE_BUILD_TYPE:STRING=Release\\nCMAKE_GENERATOR:INTERNAL=' + generator + '\\n')
elif '--build' in args:
    build = pathlib.Path(args[args.index('--build') + 1])
    (build / 'extern/SDL3').mkdir(parents=True, exist_ok=True)
    (build / 'libCroMagRally.so').write_text('game')
    (build / 'extern/SDL3/libSDL3.so').write_text('SDL')
else:
    sys.exit('Unexpected CMake invocation: ' + repr(args))
""")
    stub.chmod(0o755)
    env = dict(os.environ, PATH=str(stub.parent) + os.pathsep + os.environ["PATH"],
               ANDROID_HOME=str(sdk), ANDROID_SDK_ROOT=str(sdk), ABIS="arm64-v8a",
               SKIP_GRADLE="1", CMR_TEST_CMAKE=CMAKE)
    command = [*shell, repo / script]
    run(command, env=env)
    build = repo / "build-android"
    sentinel = build / "incremental-object"
    write(sentinel, "keep")
    run(command, env=env)
    assert sentinel.exists(), f"{script} discarded a matching build tree on its second run"
    if shutil.which("pwsh"):
        alternate = (["pwsh", "-NoProfile", "-File", repo / "build_android.ps1"]
                     if script.endswith(".sh") else ["bash", repo / "build_android.sh"])
        run(alternate, env=env)
        assert sentinel.exists(), "switching wrappers discarded the compatible incremental tree"
    # A different toolchain path, even at the same revision, must force a clean tree.
    moved = sdk / "alternate-ndk"
    ndk.rename(moved)
    env.update(ANDROID_NDK_HOME=str(moved), ANDROID_NDK_ROOT=str(moved))
    run(command, env=env)
    assert not sentinel.exists(), f"{script} retained a different NDK's build tree"
    write(sentinel, "legacy cache")
    (build / "cmr-ndk.stamp").unlink()
    run(command, env=env)
    assert not sentinel.exists(), f"{script} retained an unrecognized legacy build tree"
    print(f"PASS: {script} incremental reuse, changed NDK, legacy cache")


with tempfile.TemporaryDirectory(prefix="cmr-android-tests-", dir=os.environ.get("TMPDIR") or (tempfile.gettempdir() if os.name == "nt" else "/var/tmp")) as folder:
    scratch = Path(folder)
    run([EXTRACT_TEST, scratch])
    manifest_test(scratch)
    if os.name != "nt":
        wrapper_test(scratch, ["bash"], "build_android.sh")
        pwsh = shutil.which("pwsh")
        if pwsh:
            wrapper_test(scratch, [pwsh, "-NoProfile", "-File"], "build_android.ps1")
        else:
            print("PowerShell wrapper test skipped: pwsh is not installed")
    print("PASS: extraction recovery and same-version content updates")
