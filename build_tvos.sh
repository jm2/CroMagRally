#!/bin/bash
set -e

# tvOS Build Script for Cro-Mag Rally
# Usage: ./build_tvos.sh [device|simulator]
#
# Env knobs (sensible defaults for local dev; the CI release job sets these):
#   CODE_SIGNING_ALLOWED   set to NO for an unsigned build   (default: YES)
#   PACKAGE                set to 1 to emit a versioned       (default: 0)
#                          unsigned .app .zip (device target only)

TARGET=${1:-simulator}

echo "=== Building Cro-Mag Rally for tvOS ($TARGET) ==="

# Configuration
if [ "$TARGET" == "device" ]; then
    BUILD_DIR="build-tvos-device"
    SDK="appletvos"
    ARCHS="arm64"
    OUT_SUBDIR="Release-appletvos"
else
    BUILD_DIR="build-tvos-simulator"
    SDK="appletvsimulator"
    ARCHS="arm64"
    OUT_SUBDIR="Release-appletvsimulator"
fi

# Step 0: Use the same SDL source submodule as every other platform.
if [ ! -f "extern/SDL3/CMakeLists.txt" ]; then
    echo "Error: SDL3 submodule is missing."
    echo "Run: git submodule update --init --recursive"
    exit 1
fi

# Step 1: Configure with CMake
echo "=== Configuring CMake for tvOS ($SDK) ==="
cmake -G Xcode \
    -S . \
    -B "$BUILD_DIR" \
    -DCMAKE_SYSTEM_NAME=tvOS \
    -DCMAKE_OSX_SYSROOT="$SDK" \
    -DCMAKE_OSX_ARCHITECTURES="$ARCHS" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
    -DBUILD_SDL_FROM_SOURCE=ON \
    -DSDL_STATIC=ON \
    -DSDL_SHARED=OFF \
    -DSDL_OPENGLES=ON \
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

# Step 3 (optional): zip the UNSIGNED .app into a single versioned asset for distribution.
# (A bare .app directory loses its wrapper when round-tripped through CI artifacts.)
# Device target only. NOTE: an unsigned tvOS .app is not installable on real Apple TV
# hardware without re-signing in Xcode.
if [ "${PACKAGE:-0}" == "1" ] && [ "$TARGET" == "device" ]; then
    GAME_VERSION=$(grep -m1 'set(GAME_VERSION' CMakeLists.txt | sed -E 's/.*"([^"]+)".*/\1/')
    ZIP_ABS="$PWD/CroMagRally-${GAME_VERSION}-tvos-unsigned.zip"
    echo "=== Packaging $(basename "$ZIP_ABS") (UNSIGNED) ==="
    rm -f "$ZIP_ABS"
    ( cd "$BUILD_DIR/$OUT_SUBDIR" && zip -qry "$ZIP_ABS" CroMagRally.app )
    echo "Unsigned tvOS app: $ZIP_ABS"
elif [ "$TARGET" == "simulator" ]; then
    echo "App bundle: $APP_BUNDLE"
    echo ""
    echo "To run in simulator:"
    echo "  xcrun simctl boot 'Apple TV 4K (3rd generation)'"
    echo "  xcrun simctl install booted $APP_BUNDLE"
    echo "  xcrun simctl launch booted io.jor.cromagrally"
else
    echo "App bundle: $APP_BUNDLE"
    echo ""
    echo "To install on device, open in Xcode:"
    echo "  open $BUILD_DIR/CroMagRally.xcodeproj"
fi
