# How to build Cro-Mag Rally

## The easy way: build.py (automated build script)

`build.py` can produce a game executable from a fresh clone of the repo in a single command. It will work on macOS, Windows and Linux, provided that your system has Python 3, CMake, and an adequate C++ compiler.

```
git clone --recurse-submodules https://github.com/jorio/CroMagRally
cd CroMagRally
python3 build.py
```

If you want to build the game **manually** instead, the rest of this document describes how to do just that on each of the big 3 desktop operating systems.

## How to build the game manually on macOS

1. Install the prerequisites:
    - Xcode (preferably the latest version)
    - [CMake](https://formulae.brew.sh/formula/cmake) 3.21+ (installing via Homebrew is recommended)
1. Clone the repo **recursively**:
    ```
    git clone --recurse-submodules https://github.com/jorio/CroMagRally
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
    git clone --recurse-submodules https://github.com/jorio/CroMagRally
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
    - Any C++20 compiler
    - CMake 3.21+
    - OpenGL development libraries (e.g. "libgl1-mesa-dev" on Ubuntu)
1. Clone the repo **recursively**:
    ```
    git clone --recurse-submodules https://github.com/jorio/CroMagRally
    cd CroMagRally
    ```
1. Build the game:
    ```
    cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
    cmake --build build
    ```
    SDL3 is built from the `extern/SDL3` submodule by default. Distribution packagers can
    opt into an installed SDL with `-DBUILD_SDL_FROM_SOURCE=OFF`.
    If you'd like to enable runtime sanitizers, append `-DSANITIZE=1` to the **first** `cmake` call above.
1. The game gets built in `build/CroMagRally`. Enjoy!

## How to build for mobile (iOS / tvOS / Android)

The mobile/TV builds are produced by dedicated scripts (they are **not** part of `build.py`). They target **sideloading / GitHub Releases**, not the App Store or Google Play. Every platform builds the same `extern/SDL3` submodule checkout; update that gitlink when adopting a newer official SDL release.

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

### Android (Android SDK + NDK r27+ required)

```
./build_android.sh           # builds x86_64 + arm64-v8a, then assembles a debug APK
./build_android.sh run       # also installs + launches on a connected device/emulator
```

`JAVA_HOME`, `ANDROID_HOME`, and the NDK are auto-detected from a standard Android Studio install; set them explicitly if the build can't find them. Windows users can run `build_android.ps1`. Environment knobs: `ABIS="arm64-v8a"` limits which ABIs are built, and `SKIP_GRADLE=1` stops after the native libraries (used by the CI compile gate).

## CI release signing secrets

`.github/workflows/ReleaseBuilds.yml` signs/notarizes release artifacts when these GitHub Actions secrets are configured. If they're absent (e.g. on a fork), the jobs still succeed and upload **unsigned** artifacts.

| Secret | Used for |
|--------|----------|
| `APPLE_CODE_SIGN_IDENTITY` | macOS `.app` / SDL codesigning identity |
| `APPLE_DEVELOPER_CERTIFICATE_P12_BASE64` / `…_PASSWORD` | macOS signing certificate (base64 `.p12` + password) |
| `APPLE_NOTARIZATION_USERNAME` / `…_PASSWORD` | macOS notarization (notarytool) |
| `APPLE_DEVELOPMENT_TEAM` | macOS notarization team ID |
| `ANDROID_SIGNING_KEY` | base64 Android release keystore |
| `ANDROID_KEY_ALIAS` / `ANDROID_KEYSTORE_PASSWORD` / `ANDROID_KEY_PASSWORD` | Android keystore alias + passwords |

(Note: `SECRETS.md` in the repo root documents in-game cheat codes, **not** these CI secrets.)
