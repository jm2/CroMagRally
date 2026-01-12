#!/bin/bash
set -e

# Configuration
# Configuration
ANDROID_DIR="AndroidBuild"

# Function to build and copy for a specific ABI
build_abi() {
    ABI=$1
    BUILD_DIR=$2
    JNI_LIBS_DIR="$ANDROID_DIR/app/src/main/jniLibs/$ABI"
    
    echo "=== Building for $ABI in $BUILD_DIR ==="
    
    # Check if build directory exists, if not, we might need to configure (optional, but good for robustness)
    # For now assuming it exists as per previous context
    
    cmake --build $BUILD_DIR
    
    echo "--- Copying libraries to $JNI_LIBS_DIR ---"
    mkdir -p $JNI_LIBS_DIR
    cp $BUILD_DIR/libCroMagRally.so $JNI_LIBS_DIR/libmain.so
    cp $BUILD_DIR/extern/SDL3-3.2.8/libSDL3.so $JNI_LIBS_DIR/libSDL3.so
}

# 1. Build x86_64 (Emulator)
build_abi "x86_64" "build-android-x86"

# 2. Build arm64-v8a (Device)
build_abi "arm64-v8a" "build-android"

echo "=== 3. Assembling Debug APK (Gradle) ==="
cd $ANDROID_DIR
./gradlew assembleDebug

echo "=== Success! APK is at $ANDROID_DIR/app/build/outputs/apk/debug/app-debug.apk ==="
