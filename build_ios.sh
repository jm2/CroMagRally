#!/bin/bash
set -e

# iOS Build Script for Cro-Mag Rally
# Usage: ./build_ios.sh [device|simulator]
#
# Env knobs (sensible defaults for local dev; the CI release job sets these):
#   CODE_SIGNING_ALLOWED   set to NO for an unsigned build   (default: YES)
#   PACKAGE                set to 1 to emit a sideloadable    (default: 0)
#                          unsigned .ipa (device target only)

TARGET=${1:-simulator}

echo "=== Building Cro-Mag Rally for iOS ($TARGET) ==="

# Configuration
if [ "$TARGET" == "device" ]; then
    BUILD_DIR="build-ios-device"
    SDK="iphoneos"
    ARCHS="arm64"
    OUT_SUBDIR="Release-iphoneos"
else
    BUILD_DIR="build-ios-simulator"
    SDK="iphonesimulator"
    # Build for both Intel and Apple Silicon Macs
    ARCHS="x86_64;arm64"
    OUT_SUBDIR="Release-iphonesimulator"
fi

# Step 0: Use the same SDL source submodule as every other platform.
if [ ! -f "extern/SDL3/CMakeLists.txt" ]; then
    echo "Error: SDL3 submodule is missing."
    echo "Run: git submodule update --init --recursive"
    exit 1
fi

# Step 1: Configure with CMake
echo "=== Configuring CMake for iOS ($SDK) ==="
cmake -G Xcode \
    -S . \
    -B "$BUILD_DIR" \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT="$SDK" \
    -DCMAKE_OSX_ARCHITECTURES="$ARCHS" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
    -DBUILD_SDL_FROM_SOURCE=ON \
    -DSDL_STATIC=ON \
    -DSDL_SHARED=OFF \
    -DCMAKE_BUILD_TYPE=Release

# Step 2: Build. CI sets CODE_SIGNING_ALLOWED=NO so a hosted runner with no Apple certs
# can still produce an (unsigned) device build.
echo "=== Building with Xcode ==="
BUILD_EXTRA=()
if [ "${CODE_SIGNING_ALLOWED:-YES}" == "NO" ]; then
    BUILD_EXTRA=(-- CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO CODE_SIGN_IDENTITY="" CODE_SIGN_ENTITLEMENTS="")
fi
cmake --build "$BUILD_DIR" --config Release "${BUILD_EXTRA[@]}"

echo "=== Build complete! ==="
APP_BUNDLE="$BUILD_DIR/$OUT_SUBDIR/CroMagRally.app"

# Step 3 (optional): wrap the .app in the conventional Payload/ layout as an UNSIGNED .ipa
# for sideload/re-sign tools (AltStore / Sideloadly). Device target only.
if [ "${PACKAGE:-0}" == "1" ] && [ "$TARGET" == "device" ]; then
    GAME_VERSION=$(sed -n 's/^GAME_VERSION=//p' version.properties)
    if [ -z "$GAME_VERSION" ]; then
        echo "Error: GAME_VERSION is missing from version.properties"
        exit 1
    fi
    IPA="CroMagRally-${GAME_VERSION}-ios-unsigned.ipa"
    echo "=== Packaging $IPA (UNSIGNED; re-sign with AltStore/Sideloadly to install) ==="
    rm -rf Payload "$IPA"
    mkdir -p Payload
    cp -R "$APP_BUNDLE" Payload/
    zip -qry "$IPA" Payload
    rm -rf Payload
    echo "Unsigned IPA: $IPA"
elif [ "$TARGET" == "simulator" ]; then
    echo "App bundle: $APP_BUNDLE"
    echo ""
    echo "To run in simulator:"
    echo "  xcrun simctl boot 'iPhone 15 Pro'"
    echo "  xcrun simctl install booted $APP_BUNDLE"
    echo "  xcrun simctl launch booted io.jor.cromagrally"
else
    echo "App bundle: $APP_BUNDLE"
    echo ""
    echo "To install on device, open in Xcode:"
    echo "  open $BUILD_DIR/CroMagRally.xcodeproj"
fi
