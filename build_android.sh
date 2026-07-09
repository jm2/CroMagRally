#!/bin/bash
set -e

# Configuration
ANDROID_COMPILE_PLATFORM="android-35"
ANDROID_BUILD_TOOLS_VERSION="37.0.0"

is_android_sdk_usable() {
    local sdk_dir="$1"

    if [ -z "$sdk_dir" ] || [ ! -d "$sdk_dir" ]; then
        return 1
    fi

    if [ "${SKIP_GRADLE:-0}" == "1" ]; then
        return 0
    fi

    [ -d "$sdk_dir/platforms/$ANDROID_COMPILE_PLATFORM" ] \
        && [ -d "$sdk_dir/build-tools/$ANDROID_BUILD_TOOLS_VERSION" ]
}

find_android_sdk() {
    for sdk_dir in \
        "$HOME/Library/Android/sdk" \
        "$HOME/Android/Sdk" \
        "$LOCALAPPDATA/Android/Sdk"
    do
        if is_android_sdk_usable "$sdk_dir"; then
            echo "$sdk_dir"
            return 0
        fi
    done

    return 1
}

is_android_ndk_usable() {
    local ndk_dir="$1"
    [ -n "$ndk_dir" ] && [ -f "$ndk_dir/build/cmake/android.toolchain.cmake" ]
}

# Auto-detect JAVA_HOME if not set
if [ -z "$JAVA_HOME" ]; then
    # macOS Android Studio
    if [ -d "/Applications/Android Studio.app/Contents/jbr/Contents/Home" ]; then
        export JAVA_HOME="/Applications/Android Studio.app/Contents/jbr/Contents/Home"
    elif [ -d "/Applications/Android Studio.app/Contents/jre/Contents/Home" ]; then
        export JAVA_HOME="/Applications/Android Studio.app/Contents/jre/Contents/Home"
    # Linux Android Studio (standard/snap/archive)
    elif [ -d "/opt/android-studio/jbr" ]; then
        export JAVA_HOME="/opt/android-studio/jbr"
    elif [ -d "$HOME/android-studio/jbr" ]; then
        export JAVA_HOME="$HOME/android-studio/jbr"
    fi
    
    if [ ! -z "$JAVA_HOME" ]; then
        echo "Auto-detected JAVA_HOME: $JAVA_HOME"
    fi
fi

# Auto-detect ANDROID_HOME if not set, or if the inherited SDK is incomplete
# for Gradle APK assembly. Native-only smoke builds (SKIP_GRADLE=1) only require
# an NDK, so any existing SDK directory is acceptable there.
if ! is_android_sdk_usable "$ANDROID_HOME"; then
    if [ ! -z "$ANDROID_HOME" ]; then
        echo "Ignoring incomplete ANDROID_HOME: $ANDROID_HOME"
    fi

    DETECTED_ANDROID_HOME=$(find_android_sdk || true)
    if [ ! -z "$DETECTED_ANDROID_HOME" ]; then
        export ANDROID_HOME="$DETECTED_ANDROID_HOME"
        export ANDROID_SDK_ROOT="$ANDROID_HOME"
        echo "Auto-detected ANDROID_HOME: $ANDROID_HOME"
    fi
fi

if [ ! -z "$ANDROID_HOME" ] && [ "$ANDROID_SDK_ROOT" != "$ANDROID_HOME" ]; then
    export ANDROID_SDK_ROOT="$ANDROID_HOME"
fi

# Auto-detect NDK if not set (but ANDROID_HOME is present)
if ! is_android_ndk_usable "$ANDROID_NDK_HOME" && is_android_ndk_usable "$ANDROID_NDK_ROOT"; then
    export ANDROID_NDK_HOME="$ANDROID_NDK_ROOT"
fi

if ! is_android_ndk_usable "$ANDROID_NDK_HOME" && [ ! -z "$ANDROID_HOME" ]; then
    NDK_ROOT="$ANDROID_HOME/ndk"
    if [ -d "$NDK_ROOT" ]; then
        # Find the directory with the highest version number
        LATEST_NDK=$(ls -1d "$NDK_ROOT"/* | sort -V | tail -n1)
        if [ ! -z "$LATEST_NDK" ]; then
            export ANDROID_NDK_HOME="$LATEST_NDK"
            echo "Auto-detected ANDROID_NDK_HOME: $ANDROID_NDK_HOME"
        fi
    fi
fi

if [ -z "$ANDROID_HOME" ]; then
    echo "Error: ANDROID_HOME is not set and no usable Android SDK was auto-detected."
    exit 1
fi

if ! is_android_ndk_usable "$ANDROID_NDK_HOME"; then
    echo "Error: ANDROID_NDK_HOME is not set or does not contain build/cmake/android.toolchain.cmake."
    exit 1
fi

ANDROID_DIR="AndroidBuild"
SDL_DIR="extern/SDL3"

# Parse arguments
DO_RUN=false
for arg in "$@"; do
    if [ "$arg" == "run" ]; then
        DO_RUN=true
    fi
done

# The Android build uses the same SDL source checkout as every other platform.
prepare_dependencies() {
    if [ ! -f "$SDL_DIR/CMakeLists.txt" ]; then
        echo "Error: SDL3 submodule is missing at $SDL_DIR."
        echo "Run: git submodule update --init --recursive"
        exit 1
    fi
    echo "=== Using SDL3 submodule at $SDL_DIR ==="
}

prepare_dependencies


# Function to build and copy for a specific ABI
build_abi() {
    ABI=$1
    BUILD_DIR=$2
    JNI_LIBS_DIR="$ANDROID_DIR/app/src/main/jniLibs/$ABI"
    
    echo "=== Building for $ABI in $BUILD_DIR ==="

    # Configure
    echo "Configuring $BUILD_DIR..."
    mkdir -p $BUILD_DIR
    cd $BUILD_DIR
    cmake -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake \
        -DANDROID_ABI=$ABI \
        -DANDROID_PLATFORM=android-24 \
        -DBUILD_SDL_FROM_SOURCE=ON \
        -DSDL_SHARED=ON \
        -DSDL_STATIC=OFF \
        -DCMAKE_BUILD_TYPE=Release \
        ..
    cd ..

    cmake --build $BUILD_DIR --config Release
    
    echo "--- Copying libraries to $JNI_LIBS_DIR ---"
    mkdir -p $JNI_LIBS_DIR
    cp $BUILD_DIR/libCroMagRally.so $JNI_LIBS_DIR/libmain.so
    cp $BUILD_DIR/extern/SDL3/libSDL3.so $JNI_LIBS_DIR/libSDL3.so
}

# Build the requested ABIs. ABIS defaults to the device + emulator pair; the PR compile
# gate overrides it (ABIS=arm64-v8a SKIP_GRADLE=1) for a fast native-only smoke build.
ABIS="${ABIS:-x86_64 arm64-v8a}"
for ABI in $ABIS; do
    case "$ABI" in
        arm64-v8a) BUILD_DIR="build-android" ;;     # device
        x86_64)    BUILD_DIR="build-android-x86" ;;  # emulator
        *)         BUILD_DIR="build-android-$ABI" ;;
    esac
    build_abi "$ABI" "$BUILD_DIR"
done

# SKIP_GRADLE=1 stops here: native libs are built (enough for a compile check) but the
# APK is not assembled, so the asset copy + Gradle + install/launch below are skipped.
if [ "${SKIP_GRADLE:-0}" == "1" ]; then
    echo "=== SKIP_GRADLE=1: native libraries built; skipping APK assembly. ==="
    exit 0
fi

# Copy assets. Android can't enumerate its asset dir at runtime, so ship a file list.
echo "=== Generating Data/files.txt ==="
LC_ALL=C find Data -type f ! -name files.txt | LC_ALL=C sort > Data/files.txt

ASSETS_DIR="$ANDROID_DIR/app/src/main/assets"
echo "=== Copying Assets to $ASSETS_DIR ==="
rm -rf "$ASSETS_DIR"
mkdir -p $ASSETS_DIR
cp -r Data $ASSETS_DIR/

echo "=== Assembling Debug APK (Gradle) ==="
cd $ANDROID_DIR
./gradlew assembleDebug
cd ..

echo "=== Success! APK is at $ANDROID_DIR/app/build/outputs/apk/debug/app-debug.apk ==="

if [ "$DO_RUN" = true ]; then
    # 4. Check/Launch Emulator
    DEVICE_COUNT=$(adb devices | grep -v "List of devices attached" | grep -v "^$" | wc -l)
    if [ "$DEVICE_COUNT" -eq 0 ]; then
        AVD_NAME="TestDevice"
        EMULATOR_BIN="$ANDROID_HOME/emulator/emulator"
        
        if [ ! -x "$EMULATOR_BIN" ]; then
            # Try finding it in path or common locations if ANDROID_HOME is tricky
            EMULATOR_BIN="emulator"
        fi
        
        echo "=== No running device found. Launching emulator: $AVD_NAME ==="
        nohup $EMULATOR_BIN -avd $AVD_NAME -no-boot-anim -netdelay none -netspeed full > /dev/null 2>&1 &
        
        echo "Waiting for device to connect..."
        adb wait-for-device
        echo "Device connected!"
        
        # Optional: Wait for boot completion (can take a while, maybe skip for speed if ADB accepts install early)
        # echo "Waiting for boot completion..."
        # while [[ "$(adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" != "1" ]]; do sleep 1; done
    else
        echo "=== Device detected. Skipping emulator launch. ==="
    fi

    echo "=== 5. Installing and Launching on All Devices ==="

    # Get list of devices (skipping header and empty lines)
    adb devices | grep -v "List of devices attached" | grep -v "^$" | while read -r line; do
        DEVICE_SERIAL=$(echo $line | awk '{print $1}')
        if [ ! -z "$DEVICE_SERIAL" ]; then
            echo "--> Processing Device: $DEVICE_SERIAL"
            
            echo "    Installing APK..."
            adb -s $DEVICE_SERIAL install -r $ANDROID_DIR/app/build/outputs/apk/debug/app-debug.apk
            
            echo "    Launching Activity..."
            adb -s $DEVICE_SERIAL shell am start -n io.jor.cromagrally/org.libsdl.app.SDLActivity
            
            echo "--> Done with $DEVICE_SERIAL"
        fi
    done
fi
