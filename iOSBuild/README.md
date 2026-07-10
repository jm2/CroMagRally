# Building Cro-Mag Rally for iOS

## Prerequisites

- **Xcode 15+** with iOS SDK
- **CMake 3.21+** (`brew install cmake`)
- **macOS** (required for iOS development)

Run every command below from the repository root (the directory containing
`build_ios.sh` and `CMakeLists.txt`), not from `iOSBuild/`.

## Quick Build (Simulator)

```bash
./build_ios.sh simulator
```

## Quick Build (Device)

```bash
./build_ios.sh device
```

> **Note:** Installing a directly signed device build requires an Apple Developer account
> and valid provisioning profile.

To produce an unsigned device package for later re-signing with Xcode, AltStore, or
Sideloadly, no developer certificate is required:

```bash
CODE_SIGNING_ALLOWED=NO PACKAGE=1 ./build_ios.sh device
```

This emits a clearly labeled `*-ios-unsigned.ipa`; it is not directly installable until
it has been signed for the destination device.

## Manual Build Steps

### 1. Configure CMake

```bash
# For iOS Simulator
cmake -G Xcode -S . -B build-ios-simulator \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT=iphonesimulator \
    -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
    -DBUILD_SDL_FROM_SOURCE=ON

# For iOS Device
cmake -G Xcode -S . -B build-ios-device \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT=iphoneos \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
    -DBUILD_SDL_FROM_SOURCE=ON
```

### 2. Build with Xcode

```bash
cmake --build build-ios-simulator --config Release
```

Or open in Xcode:
```bash
open build-ios-simulator/CroMagRally.xcodeproj
```

### 3. Run in Simulator

```bash
xcrun simctl boot 'iPhone 15 Pro'
xcrun simctl install booted build-ios-simulator/Release-iphonesimulator/CroMagRally.app
xcrun simctl launch booted io.jor.cromagrally
```

## Asset Icons

Place the following icon files in `iOSBuild/CroMagRally/Assets.xcassets/AppIcon.appiconset/`:

| File | Size | Purpose |
|------|------|---------|
| Icon-1024.png | 1024×1024 | App Store |

The modern iOS asset catalog only requires the 1024×1024 icon; Xcode generates all other sizes automatically.

## Troubleshooting

### "No signing certificate" error
- Open Xcode, go to Signing & Capabilities
- Select your development team
- For testing, enable "Automatically manage signing"

### Build fails on M1/M2 Mac for simulator
- Ensure you're building for both architectures: `x86_64;arm64`
- Some simulators run x86_64 via Rosetta

### OpenGL ES errors
- The game uses gl4es to translate OpenGL 1.x to OpenGL ES 2.0
- If you see rendering issues, check gl4es compatibility
