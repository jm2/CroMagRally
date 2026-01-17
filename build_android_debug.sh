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
    
    # Configure if missing
    if [ ! -d "$BUILD_DIR" ]; then
        echo "Build directory $BUILD_DIR missing. Configuring..."
        mkdir -p $BUILD_DIR
        cd $BUILD_DIR
        if [ "$ABI" == "arm64-v8a" ]; then
             cmake -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake \
              -DANDROID_ABI=arm64-v8a \
              -DANDROID_PLATFORM=android-24 \
              -DBUILD_SDL_FROM_SOURCE=ON \
              -DSDL3_DIR=extern/SDL3-3.2.8 \
              ..
        else
             cmake -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake \
              -DANDROID_ABI=x86_64 \
              -DANDROID_PLATFORM=android-24 \
              -DBUILD_SDL_FROM_SOURCE=ON \
              -DSDL3_DIR=extern/SDL3-3.2.8 \
              ..
        fi
        cd ..
    fi

    cmake --build $BUILD_DIR
    
    echo "--- Copying libraries to $JNI_LIBS_DIR ---"
    mkdir -p $JNI_LIBS_DIR
    cp $BUILD_DIR/libCroMagRally.so $JNI_LIBS_DIR/libmain.so
    cp $BUILD_DIR/extern/SDL3-3.2.8/libSDL3.so $JNI_LIBS_DIR/libSDL3.so
}

# 0. Copy Assets
echo "=== Generating Data/files.txt ==="
find Data -type f > Data/files.txt

ASSETS_DIR="$ANDROID_DIR/app/src/main/assets"
echo "=== Copying Assets to $ASSETS_DIR ==="
mkdir -p $ASSETS_DIR
cp -r Data $ASSETS_DIR/

# 1. Build x86_64 (Emulator)
build_abi "x86_64" "build-android-x86"

# 2. Build arm64-v8a (Device)
build_abi "arm64-v8a" "build-android"

echo "=== 3. Assembling Debug APK (Gradle) ==="
cd $ANDROID_DIR
./gradlew assembleDebug
cd ..

echo "=== Success! APK is at $ANDROID_DIR/app/build/outputs/apk/debug/app-debug.apk ==="

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
        adb -s $DEVICE_SERIAL shell am start -n io.jor.cromagrally/io.jor.cromagrally.SDLActivity
        
        echo "--> Done with $DEVICE_SERIAL"
    fi
done
