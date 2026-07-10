#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

# Configuration
ANDROID_COMPILE_PLATFORM="android-35"
ANDROID_BUILD_TOOLS_VERSION="37.0.0"
ANDROID_NDK_VERSION="29.0.14206865"
GRADLE_TASK="${GRADLE_TASK:-assembleDebug}"

GAME_IDENTIFIER="$(sed -n 's/^GAME_IDENTIFIER=//p' version.properties)"
if [[ ! "$GAME_IDENTIFIER" =~ ^[A-Za-z][A-Za-z0-9_]*(\.[A-Za-z][A-Za-z0-9_]*)+$ ]]; then
    echo "Error: version.properties contains an invalid GAME_IDENTIFIER."
    exit 1
fi

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
    local sdk_dir
    local -a sdk_candidates=()

    if [ -n "${ANDROID_HOME:-}" ]; then
        sdk_candidates+=("$ANDROID_HOME")
    fi
    if [ -n "${ANDROID_SDK_ROOT:-}" ]; then
        sdk_candidates+=("$ANDROID_SDK_ROOT")
    fi
    if [ -n "${HOME:-}" ]; then
        sdk_candidates+=("$HOME/Library/Android/sdk" "$HOME/Android/Sdk")
    fi
    if [ -n "${LOCALAPPDATA:-}" ]; then
        sdk_candidates+=("$LOCALAPPDATA/Android/Sdk")
    fi

    for sdk_dir in "${sdk_candidates[@]}"; do
        if is_android_sdk_usable "$sdk_dir"; then
            printf '%s\n' "$sdk_dir"
            return 0
        fi
    done

    return 1
}

is_android_ndk_usable() {
    local ndk_dir="${1:-}"
    local revision

    if [ -z "$ndk_dir" ] || [ ! -f "$ndk_dir/build/cmake/android.toolchain.cmake" ]; then
        return 1
    fi

    revision="$(sed -n 's/^Pkg\.Revision[[:space:]]*=[[:space:]]*//p' "$ndk_dir/source.properties" 2>/dev/null | head -n1)"
    [ "$revision" = "$ANDROID_NDK_VERSION" ]
}

# Auto-detect JAVA_HOME if not set
if [ -z "${JAVA_HOME:-}" ]; then
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
    
    if [ -n "${JAVA_HOME:-}" ]; then
        echo "Auto-detected JAVA_HOME: $JAVA_HOME"
    fi
fi

# Resolve both Android SDK variable conventions to one verified SDK. ANDROID_HOME
# remains Gradle's canonical variable, while ANDROID_SDK_ROOT is still accepted for
# existing developer and CI environments.
DETECTED_ANDROID_HOME="$(find_android_sdk || true)"
if [ -z "$DETECTED_ANDROID_HOME" ]; then
    echo "Error: neither ANDROID_HOME nor ANDROID_SDK_ROOT points to a usable Android SDK, and none was auto-detected."
    exit 1
fi
export ANDROID_HOME="$DETECTED_ANDROID_HOME"
export ANDROID_SDK_ROOT="$DETECTED_ANDROID_HOME"
echo "=== Using Android SDK at $ANDROID_HOME ==="

# Use the pinned side-by-side NDK whenever it is installed in the SDK. Native-only
# callers may supply the same revision in ANDROID_NDK_HOME/ANDROID_NDK_ROOT.
PINNED_SDK_NDK="$ANDROID_HOME/ndk/$ANDROID_NDK_VERSION"
DETECTED_ANDROID_NDK=""
for ndk_dir in "$PINNED_SDK_NDK" "${ANDROID_NDK_HOME:-}" "${ANDROID_NDK_ROOT:-}"; do
    if is_android_ndk_usable "$ndk_dir"; then
        DETECTED_ANDROID_NDK="$ndk_dir"
        break
    fi
done

if [ -z "$DETECTED_ANDROID_NDK" ]; then
    echo "Error: Android NDK $ANDROID_NDK_VERSION is required."
    echo "Install it with: sdkmanager --install \"ndk;$ANDROID_NDK_VERSION\""
    exit 1
fi
export ANDROID_NDK_HOME="$DETECTED_ANDROID_NDK"
export ANDROID_NDK_ROOT="$DETECTED_ANDROID_NDK"
echo "=== Using Android NDK $ANDROID_NDK_VERSION at $ANDROID_NDK_HOME ==="

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
    local abi="$1"
    local build_dir="$2"
    local jni_libs_dir="$ANDROID_DIR/app/src/main/jniLibs/$abi"
    local toolchain_file="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake"
    
    echo "=== Building for $abi in $build_dir ==="

    # A CMake build tree cannot safely switch Android toolchains in place. Drop only
    # generated state when an older NDK populated this ABI's cache.
    if [ -f "$build_dir/CMakeCache.txt" ] \
        && ! grep -Fq "CMAKE_ANDROID_NDK:PATH=$ANDROID_NDK_HOME" "$build_dir/CMakeCache.txt"; then
        echo "Removing stale CMake cache in $build_dir (different Android NDK)."
        rm -rf -- "$build_dir"
    fi

    # Configure
    echo "Configuring $build_dir..."
    cmake -S "$ROOT_DIR" -B "$build_dir" \
        "-DCMAKE_TOOLCHAIN_FILE=$toolchain_file" \
        "-DANDROID_ABI=$abi" \
        "-DANDROID_PLATFORM=android-24" \
        "-DBUILD_SDL_FROM_SOURCE=ON" \
        "-DSDL_SHARED=ON" \
        "-DSDL_STATIC=OFF" \
        "-DCMAKE_BUILD_TYPE=Release"

    cmake --build "$build_dir" --config Release
    
    echo "--- Copying libraries to $jni_libs_dir ---"
    mkdir -p "$jni_libs_dir"
    cp "$build_dir/libCroMagRally.so" "$jni_libs_dir/libmain.so"
    cp "$build_dir/extern/SDL3/libSDL3.so" "$jni_libs_dir/libSDL3.so"
}

# Build the requested ABIs. ABIS defaults to the device + emulator pair; CI overrides
# it with ABIS=arm64-v8a while still assembling a complete debug APK.
IFS=', ' read -r -a REQUESTED_ABIS <<< "${ABIS:-x86_64 arm64-v8a}"
VALIDATED_ABIS=()
for ABI in "${REQUESTED_ABIS[@]}"; do
    [ -n "$ABI" ] || continue
    case "$ABI" in
        arm64-v8a|x86_64) VALIDATED_ABIS+=("$ABI") ;;
        *)
            echo "Error: unsupported Android ABI '$ABI'. Supported ABIs: arm64-v8a, x86_64."
            exit 1
            ;;
    esac
done
if [ "${#VALIDATED_ABIS[@]}" -eq 0 ]; then
    echo "Error: ABIS must contain at least one Android ABI."
    exit 1
fi

# Clear all previously staged ABIs once. This prevents a one-ABI build from silently
# packaging stale libraries left by an earlier multi-ABI build.
JNI_LIBS_ROOT="$ANDROID_DIR/app/src/main/jniLibs"
rm -rf -- "$JNI_LIBS_ROOT"
mkdir -p "$JNI_LIBS_ROOT"

for ABI in "${VALIDATED_ABIS[@]}"; do
    case "$ABI" in
        arm64-v8a) BUILD_DIR="build-android" ;;       # device
        x86_64)    BUILD_DIR="build-android-x86" ;;   # emulator
    esac
    build_abi "$ABI" "$BUILD_DIR"
done

# SKIP_GRADLE=1 stops here: native libs are built (enough for a compile check) but the
# APK is not assembled, so the asset copy + Gradle + install/launch below are skipped.
if [ "${SKIP_GRADLE:-0}" == "1" ]; then
    echo "=== SKIP_GRADLE=1: native libraries built; skipping APK assembly. ==="
    exit 0
fi

# Copy assets. Android can't enumerate its asset directory at runtime, so generate the
# file manifest in the staging tree without modifying the source Data directory.
ASSETS_DIR="$ANDROID_DIR/app/src/main/assets"
echo "=== Copying Assets to $ASSETS_DIR ==="
rm -rf -- "$ASSETS_DIR"
mkdir -p "$ASSETS_DIR/Data"
cp -R "Data/." "$ASSETS_DIR/Data/"
cp "LICENSE.md" "$ASSETS_DIR/LICENSE.md"
cp "THIRD-PARTY-LICENSES.md" "$ASSETS_DIR/THIRD-PARTY-LICENSES.md"

echo "=== Generating staged Data/files.txt ==="
(
    cd "$ASSETS_DIR"
    LC_ALL=C find Data -type f ! -name files.txt | LC_ALL=C sort > Data/files.txt
)

echo "=== Running Gradle task: $GRADLE_TASK ==="
(
    cd "$ANDROID_DIR"
    ./gradlew --no-daemon "$GRADLE_TASK"
)

echo "=== Success! Gradle task $GRADLE_TASK completed. ==="

if [ "$DO_RUN" = true ]; then
    if [ -n "${ANDROID_APK_PATH:-}" ]; then
        APK_PATH="$ANDROID_APK_PATH"
    elif [ "$GRADLE_TASK" = "assembleDebug" ]; then
        APK_PATH="$ANDROID_DIR/app/build/outputs/apk/debug/app-debug.apk"
    else
        echo "Error: ANDROID_APK_PATH is required when running Gradle task $GRADLE_TASK."
        exit 1
    fi
    if [ ! -f "$APK_PATH" ]; then
        echo "Error: cannot install missing APK: $APK_PATH"
        echo "Set ANDROID_APK_PATH when using a GRADLE_TASK other than assembleDebug."
        exit 1
    fi

    # 4. Check/Launch Emulator
    DEVICE_COUNT="$(adb devices | awk 'NR > 1 && $2 == "device" { count++ } END { print count + 0 }')"
    if [ "$DEVICE_COUNT" -eq 0 ]; then
        AVD_NAME="TestDevice"
        EMULATOR_BIN="$ANDROID_HOME/emulator/emulator"
        
        if [ ! -x "$EMULATOR_BIN" ]; then
            if command -v emulator >/dev/null 2>&1; then
                EMULATOR_BIN="$(command -v emulator)"
            else
                echo "Error: Android emulator executable not found."
                exit 1
            fi
        fi
        
        echo "=== No running device found. Launching emulator: $AVD_NAME ==="
        nohup "$EMULATOR_BIN" -avd "$AVD_NAME" -no-boot-anim -netdelay none -netspeed full > /dev/null 2>&1 &
        
        echo "Waiting for device and package manager..."
        DEVICE_READY=false
        for _ in {1..180}; do
            if [ "$(adb get-state 2>/dev/null || true)" = "device" ] \
                && [ "$(adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = "1" ] \
                && adb shell cmd package list packages >/dev/null 2>&1; then
                DEVICE_READY=true
                break
            fi
            sleep 1
        done
        if [ "$DEVICE_READY" != true ]; then
            echo "Error: emulator did not become ready within 180 seconds."
            exit 1
        fi
        echo "Device connected and booted!"
    else
        echo "=== Device detected. Skipping emulator launch. ==="
    fi

    echo "=== 5. Installing and Launching on All Devices ==="

    # Get the list of fully connected devices, skipping the header and offline entries.
    while read -r DEVICE_SERIAL DEVICE_STATE _; do
        if [ "$DEVICE_STATE" != "device" ]; then
            continue
        fi

        echo "--> Processing Device: $DEVICE_SERIAL"
        echo "    Installing APK..."
        adb -s "$DEVICE_SERIAL" install -r "$APK_PATH"

        echo "    Launching Activity..."
        adb -s "$DEVICE_SERIAL" shell am start -n "$GAME_IDENTIFIER/org.libsdl.app.SDLActivity"
        echo "--> Done with $DEVICE_SERIAL"
    done < <(adb devices | tail -n +2)
fi
