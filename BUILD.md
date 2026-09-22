# How to build Cro-Mag Rally

## The easy way: build.py (automated build script)

`build.py` can configure, build, and package the game from a fresh clone in a single command. It works on macOS, Windows, and Linux with Git, Python 3, CMake, and suitable C and C++ compilers installed.

```
git clone --recurse-submodules https://github.com/jm2/CroMagRally.git
cd CroMagRally
python3 build.py
```

With no step flags, `build.py` runs the complete pipeline, including packaging. To stop after compiling the executable, run:

```
python3 build.py --dependencies --configure --build
```

If you already cloned without `--recurse-submodules`, populate the pinned source dependencies with:

```
git submodule update --init --recursive
```

The repository pins SDL3, Pomme, and gl4es as submodules. Desktop builds use SDL3 and Pomme; the mobile builds also use gl4es. The default build compiles the pinned `extern/SDL3` checkout on every platform, so it does not silently select a different system SDL release. When updating a gitlink, review the dependency's license files and keep `THIRD-PARTY-LICENSES.md` in sync.

If you want to build the game **manually** instead, the rest of this document describes how to do just that on each of the big 3 desktop operating systems.

## How to build the game manually on macOS

1. Install the prerequisites:
    - Xcode (preferably the latest version)
    - [CMake](https://formulae.brew.sh/formula/cmake) 3.21+ (installing via Homebrew is recommended)
1. Clone the repo **recursively**:
    ```
    git clone --recurse-submodules https://github.com/jm2/CroMagRally.git
    cd CroMagRally
    ```
1. Prep the Xcode project:
    ```
    cmake -G Xcode -S . -B build
    ```
1. Now you can open `build/CroMagRally.xcodeproj` in Xcode, or you can just go ahead and build the game:
    ```
    cmake --build build --config RelWithDebInfo
    ```
1. The game gets built in `build/RelWithDebInfo/CroMagRally.app`. Enjoy!

## How to build the game manually on Windows

1. Install the prerequisites:
    - Visual Studio 2022 with the C++ toolchain
    - [CMake](https://cmake.org/download/) 3.21+
1. Clone the repo **recursively**:
    ```
    git clone --recurse-submodules https://github.com/jm2/CroMagRally.git
    cd CroMagRally
    ```
1. Prep the Visual Studio solution:
    ```
    cmake -G "Visual Studio 17 2022" -A x64 -S . -B build
    ```
1. Now you can open `build/CroMagRally.sln` in Visual Studio, or you can just go ahead and build the game:
    ```
    cmake --build build --config Release
    ```
1. The game gets built in `build/Release/CroMagRally.exe`. Enjoy!

## How to build the game manually on Linux et al.

1. Install the prerequisites from your package manager:
    - C17 and C++20 compilers
    - CMake 3.21+
    - `pkg-config`
    - OpenGL development libraries (e.g. "libgl1-mesa-dev" on Ubuntu)
1. Clone the repo **recursively**:
    ```
    git clone --recurse-submodules https://github.com/jm2/CroMagRally.git
    cd CroMagRally
    ```
1. Build the game:
    ```
    cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
    cmake --build build
    ```
    SDL3 is built from the `extern/SDL3` submodule by default. Distribution packagers can
    opt into an installed SDL with `-DBUILD_SDL_FROM_SOURCE=OFF`; the installed package
    must provide a compatible `SDL3Config.cmake`.
    If you'd like to enable runtime sanitizers, append `-DSANITIZE=1` to the **first** `cmake` call above.
1. The game gets built in `build/CroMagRally`. Enjoy!

## How to build for mobile (iOS / tvOS / Android)

The mobile/TV builds are produced by dedicated scripts (they are **not** part of `build.py`). They target **sideloading / GitHub Releases**, not the App Store or Google Play. Every platform builds the same pinned `extern/SDL3` checkout. Updating SDL means moving that gitlink to a reviewed official SDL release tag and rebuilding every target.

### iOS / tvOS (macOS with Xcode 15+ required)

```
./build_ios.sh  simulator    # or: device
./build_tvos.sh simulator    # or: device
```

A device build can be packaged as a sideloadable **unsigned** artifact:

```
PACKAGE=1 CODE_SIGNING_ALLOWED=NO ./build_ios.sh  device   # -> CroMagRally-<ver>-ios-unsigned.ipa
PACKAGE=1 CODE_SIGNING_ALLOWED=NO ./build_tvos.sh device   # -> CroMagRally-<ver>-tvos-unsigned.zip
```

The unsigned `.ipa` / `.app` must be re-signed (AltStore / Sideloadly, or your own Apple ID in Xcode) before it will install on hardware. See [iOSBuild/README.md](iOSBuild/README.md) for the full iOS walkthrough, manual CMake steps, and troubleshooting.

### Android (Android SDK + NDK 29.0.14206865 required)

```
./build_android.sh           # builds x86_64 + arm64-v8a, then assembles a debug APK
./build_android.sh run       # also installs + launches on a connected device/emulator
```

`JAVA_HOME`, `ANDROID_HOME`, and the pinned NDK are auto-detected from a standard Android Studio install; set them explicitly if the build can't find them. Both wrappers require Ninja and share the same CMake generator, so switching shells preserves compatible incremental builds. Windows users can run `build_android.ps1`, which enforces the same SDK/NDK and staging rules. Environment knobs: `ABIS="arm64-v8a"` limits which supported ABIs are built, `GRADLE_TASK=assembleRelease` selects a different Gradle assembly task, and `SKIP_GRADLE=1` provides a local native-only compile check. Pull-request CI assembles and lints both arm64-v8a Debug and x86_64 Release APKs.

## CI release signing secrets

`.github/workflows/ReleaseBuilds.yml` starts production builds when a `vMAJOR.MINOR.PATCH` tag is pushed and requires the Android keystore secrets. The tag must match `version.properties`. Every build must succeed before a complete, checksummed artifact bundle is uploaded to a draft GitHub Release; publication happens only after downloading and verifying every draft asset. Failed builds or uploads leave no public incomplete release. Re-run the failed workflow to retry; already public releases are never overwritten. For macOS, it uses Developer-ID signing + notarization when all six Apple secrets are configured, and falls back to an ad-hoc-signed, un-notarized `-unsigned.dmg` disk image when none are set (the release notes disclose its ad-hoc signing and lack of notarization); a partial Apple secret set fails the release instead of silently downgrading. Manual workflow runs are smoke builds and label credential-dependent outputs as **unsigned**. The iOS and tvOS jobs intentionally emit unsigned sideload artifacts that must be re-signed before installation on hardware.

| Secret | Used for |
|--------|----------|
| `APPLE_CODE_SIGN_IDENTITY` | macOS `.app` / SDL codesigning identity |
| `APPLE_DEVELOPER_CERTIFICATE_P12_BASE64` / `…_PASSWORD` | macOS signing certificate (base64 `.p12` + password) |
| `APPLE_NOTARIZATION_USERNAME` / `…_PASSWORD` | macOS notarization (notarytool) |
| `APPLE_DEVELOPMENT_TEAM` | macOS notarization team ID |
| `ANDROID_SIGNING_KEY` | base64 Android release keystore |
| `ANDROID_KEY_ALIAS` / `ANDROID_KEYSTORE_PASSWORD` / `ANDROID_KEY_PASSWORD` | Android keystore alias + passwords |

(Note: `SECRETS.md` in the repo root documents in-game cheat codes, **not** these CI secrets.)

## Licensing packaged builds

Distributions must include both [LICENSE.md](LICENSE.md) and [THIRD-PARTY-LICENSES.md](THIRD-PARTY-LICENSES.md). The latter contains the notices for the pinned SDL3, Pomme, and gl4es sources and for directly vendored support code and data. A system-provided SDL package remains subject to that package’s own license-distribution rules.
## Startup smoke tests

On Linux, `python3 Tests/StartupSmokeTests.py <path-to-CroMagRally>` exercises
every CLI-selectable race track with SDL's offscreen video and dummy audio
backends. It checks practice gameplay, host lobby startup, invalid track IDs,
invalid developer options, and teardown with isolated preferences and a
45-second limit per invocation.

The developer option `--smoke-test-frames N` (1–600) requires `--track` or
`--join-address` and exits after that many gameplay frames, or lobby frames when
combined with `--host`. It cannot be used with `--join`. A smoke run that ends
before printing its `SMOKE:` completion line exits with status 1. CI builds the
full game with ASan/UBSan before running these tests; unit tests alone do not
exercise asset loading. Leak detection remains enabled in the unit suite, but is
disabled for the rendering smoke because Mesa/EGL retains process-lifetime driver
allocations after unload.

### LAN smoke tests

Several game instances on one machine can't all use LAN discovery, because it
binds a fixed UDP port. These developer options let them meet directly:

- `--join-address HOST[:PORT]` joins the host at an IPv4 address without
  searching the LAN. It applies to the join started at launch only; joining from
  the menu later searches as usual. It can't be combined with `--host`, `--join`
  or `--track` (the host picks the track), nor with `--port` when the address
  already names a port.
- `--smoke-net-players N` (with `--host`, `--track` and `--smoke-test-frames`)
  keeps the lobby open until N players, the host included, have joined, then
  starts the race. `--smoke-net-refusals N` (only when `--smoke-net-players`
  equals the most players a game seats) also waits until N further joins were
  turned away because the game is full. Clients started with
  `--join-address` and `--smoke-test-frames` accept the default character and
  vehicle. Each instance races that many simulated frames and exits 0.
- `--print-max-net-players` prints how many players one LAN game seats, then
  exits.

`python3 Tests/NetworkSmokeTests.py <path-to-CroMagRally> [PLAYERS] [--track N]
[--frames K]` uses these options to race a host and its clients on loopback,
headless, and checks that one join too many is refused. PLAYERS defaults to the
most the binary seats. Use a sanitizer build: it is how out-of-bounds HUD or
network state for high player numbers shows up. CI doesn't run this script yet;
a six-player run takes about 15–30 seconds.

## Race-metrics soaks

Six more developer options shape one unattended practice race for measurement.
They require `--smoke-test-frames` and `--track`, cannot be combined with
`--host`, `--join` or `--join-address`, and raise the `--smoke-test-frames`
limit from 600 to 100000, so a whole race fits:

| Option | Effect |
|--------|--------|
| `--smoke-autopilot` | The CPU AI drives player 1, who still counts as the human (catch-up, race end). |
| `--smoke-cars N` | N cars in the race, 1 to `MAX_PLAYERS`. |
| `--smoke-fixed-fps N` | Step the simulation at a fixed N Hz (9–1000) instead of the measured frame time. |
| `--smoke-seed N` | Seed the synced RNG with N (0–2147483647) instead of the clock. |
| `--smoke-until-finish` | End the run as soon as player 1 finishes. |
| `--smoke-metrics` | At the end of the race, log a `METRICS race` line and one `METRICS car` line per car. |

With a pinned seed and a fixed timestep a race repeats exactly; vary the timestep
for independent samples. The metric fields are documented in
`Source/Headers/race_metrics.h`. Metrics are collected only with
`--smoke-metrics`; otherwise each gameplay hook costs one untaken branch.

`tools/run_race_metrics.sh <build dir> <out dir> [tracks] [car counts] [fps list] [jobs]`
runs such races in parallel, one per track, car count and timestep, and resumes an
interrupted soak. `tools/analyze_race_metrics.py <out dir>` summarizes the CPU cars:
per-race rows, per-track means for two car counts side by side (for example 6 and
12, or `--compare A B`), or for an earlier soak with `--baseline DIR`, and the number
of stranded CPU cars (stuck for more than half the race).
