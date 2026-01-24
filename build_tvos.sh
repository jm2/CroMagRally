#!/bin/bash
set -e

# tvOS Build Script for Cro-Mag Rally
# Usage: ./build_tvos.sh [device|simulator]

TARGET=${1:-simulator}

echo "=== Building Cro-Mag Rally for tvOS ($TARGET) ==="

# Configuration
if [ "$TARGET" == "device" ]; then
    BUILD_DIR="build-tvos-device"
    SDK="appletvos"
    ARCHS="arm64"
else
    BUILD_DIR="build-tvos-simulator"
    SDK="appletvsimulator"
    ARCHS="arm64"
fi

# Step 0: Check dependencies
if [ ! -d "extern/SDL" ]; then
    echo "=== Cloning SDL3 ==="
    git clone https://github.com/libsdl-org/SDL.git extern/SDL
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

# Step 2: Build
echo "=== Building with Xcode ==="
cmake --build "$BUILD_DIR" --config Release

echo "=== Build complete! ==="

if [ "$TARGET" == "simulator" ]; then
    echo "App bundle: $BUILD_DIR/Release-appletvsimulator/CroMagRally.app"
    echo ""
    echo "To run in simulator:"
    echo "  xcrun simctl boot 'Apple TV 4K (3rd generation)'"
    echo "  xcrun simctl install booted $BUILD_DIR/Release-appletvsimulator/CroMagRally.app"
    echo "  xcrun simctl launch booted io.jor.cromagrally"
else
    echo "App bundle: $BUILD_DIR/Release-appletvos/CroMagRally.app"
    echo ""
    echo "To install on device, open in Xcode:"
    echo "  open $BUILD_DIR/CroMagRally.xcodeproj"
fi
