param(
    [switch]$Run
)

$ErrorActionPreference = "Stop"

# Configuration
if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
    Write-Error "Ninja is not found in your PATH. Please install Ninja (e.g., 'winget install ninja') or add it to your PATH."
}

if (-not $env:JAVA_HOME) {
    $PossibleJava = @(
        "C:\Program Files\Android\Android Studio\jbr",
        "C:\Program Files\Android\Android Studio\jre"
    )
    foreach ($Path in $PossibleJava) {
        if (Test-Path $Path) {
            $env:JAVA_HOME = $Path
            Write-Host "Auto-detected JAVA_HOME: $Path"
            break
        }
    }
}

if (-not $env:ANDROID_HOME) {
    $PossibleSdk = "$env:LOCALAPPDATA\Android\Sdk"
    if (Test-Path $PossibleSdk) {
        $env:ANDROID_HOME = $PossibleSdk
        Write-Host "Auto-detected ANDROID_HOME: $PossibleSdk"
    }
}

if (-not $env:ANDROID_NDK_HOME -and $env:ANDROID_HOME) {
    $NdkRoot = "$env:ANDROID_HOME\ndk"
    if (Test-Path $NdkRoot) {
        $LatestNdk = Get-ChildItem $NdkRoot | Sort-Object Name -Descending | Select-Object -First 1
        if ($LatestNdk) {
            $env:ANDROID_NDK_HOME = $LatestNdk.FullName
            Write-Host "Auto-detected ANDROID_NDK_HOME: $($LatestNdk.FullName)"
        }
    }
}

$AndroidDir = "AndroidBuild"
$SdlVersion = "3.2.8"
$SdlDir = "extern/SDL3-$SdlVersion"
$SdlTar = "SDL3-$SdlVersion.tar.gz"
$SdlUrl = "https://libsdl.org/release/$SdlTar"
$SdlSha256 = "13388fabb361de768ecdf2b65e52bb27d1054cae6ccb6942ba926e378e00db03"

# Function to check and download dependencies
function Install-Dependencies {
    if (-not (Test-Path -Path $SdlDir)) {
        Write-Host "=== SDL3 not found. Downloading... ==="
        New-Item -ItemType Directory -Force -Path "extern" | Out-Null
        
        # Download
        Invoke-WebRequest -Uri $SdlUrl -OutFile $SdlTar
        
        # Verify Checksum
        $FileHash = (Get-FileHash -Path $SdlTar -Algorithm SHA256).Hash.ToLower()
        if ($FileHash -ne $SdlSha256) {
            Write-Host "Error: Checksum verification failed!" -ForegroundColor Red
            Remove-Item $SdlTar
            exit 1
        }
        
        # Extract
        # Assuming tar is available (std on Win10+ and Linux)
        tar -xzf $SdlTar -C extern/
        Remove-Item $SdlTar
        Write-Host "=== SDL3 setup complete ==="
    }
    else {
        Write-Host "=== SDL3 found in $SdlDir ==="
    }
}

Install-Dependencies

# Function to build and copy for a specific ABI
function Build-Abi {
    param (
        [string]$Abi,
        [string]$BuildDir
    )

    $JniLibsDir = "$AndroidDir/app/src/main/jniLibs/$Abi"
    
    Write-Host "=== Building for $Abi in $BuildDir ==="
    
    # Clean up potentially stale/incompatible libraries
    if (Test-Path "lib") {
        Write-Host "Cleaning up local lib directory..."
        Remove-Item "lib" -Recurse -Force
    }

    # Configure
    Write-Host "Configuring $BuildDir..."
    New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
    Push-Location $BuildDir
    
    $Toolchain = "$env:ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake"
    # Fix path for CMake if on Windows (forward slashes are generally safer for cmake args)
    if ($IsWindows) {
        $Toolchain = $Toolchain -replace "\\", "/"
    }

    if ($Abi -eq "arm64-v8a") {
        cmake -GNinja -DCMAKE_TOOLCHAIN_FILE="$Toolchain" `
            -DANDROID_ABI=arm64-v8a `
            -DANDROID_PLATFORM=android-24 `
            -DBUILD_SDL_FROM_SOURCE=ON `
            -DCMAKE_BUILD_TYPE=Release `
            -DSDL3_DIR="extern/SDL3-3.2.8" `
            ..
    }
    else {
        cmake -GNinja -DCMAKE_TOOLCHAIN_FILE="$Toolchain" `
            -DANDROID_ABI=x86_64 `
            -DANDROID_PLATFORM=android-24 `
            -DBUILD_SDL_FROM_SOURCE=ON `
            -DCMAKE_BUILD_TYPE=Release `
            -DSDL3_DIR="extern/SDL3-3.2.8" `
            ..
    }
    Pop-Location

    cmake --build $BuildDir --config Release
    
    Write-Host "--- Copying libraries to $JniLibsDir ---"
    New-Item -ItemType Directory -Force -Path $JniLibsDir | Out-Null
    Copy-Item "$BuildDir/libCroMagRally.so" "$JniLibsDir/libmain.so"
    Copy-Item "$BuildDir/extern/SDL3-3.2.8/libSDL3.so" "$JniLibsDir/libSDL3.so"
}

# 0. Copy Assets
Write-Host "=== Generating Data/files.txt ==="
$BasePath = (Get-Item .).FullName
$BasePathLength = $BasePath.Length + 1
$Files = Get-ChildItem -Path "Data" -Recurse -File | ForEach-Object { 
    $_.FullName.Substring($BasePathLength).Replace("\", "/")
}
# Write with Unix line endings for consistency if needed, but defaults are usually fine.
Set-Content -Path "Data/files.txt" -Value $Files -Force

$AssetsDir = "$AndroidDir/app/src/main/assets"
Write-Host "=== Copying Assets to $AssetsDir ==="
New-Item -ItemType Directory -Force -Path $AssetsDir | Out-Null
Copy-Item -Path "Data" -Destination "$AssetsDir" -Recurse -Force

# 1. Build x86_64 (Emulator)
Build-Abi "x86_64" "build-android-x86"

# 2. Build arm64-v8a (Device)
Build-Abi "arm64-v8a" "build-android"

Write-Host "=== 3. Assembling Debug APK (Gradle) ==="
Push-Location $AndroidDir
if ($IsWindows) {
    .\gradlew.bat assembleDebug
}
else {
    ./gradlew assembleDebug
}
Pop-Location

$ApkPath = "$AndroidDir/app/build/outputs/apk/debug/app-debug.apk"
Write-Host "=== Success! APK is at $ApkPath ==="

if ($Run) {
    # 4. Check/Launch Emulator
    $Devices = adb devices | Select-String -Pattern "\tdevice$"
    if (-not $Devices) {
        $AvdName = "TestDevice"
        $EmulatorBin = "emulator" 
        if ($env:ANDROID_HOME) {
            $EmulatorBin = "$env:ANDROID_HOME/emulator/emulator"
        }
        
        Write-Host "=== No running device found. Launching emulator: $AvdName ==="
        # Provide a warning that this might fail if emulator not in path or weird env
        Start-Process -FilePath $EmulatorBin -ArgumentList "-avd $AvdName -no-boot-anim -netdelay none -netspeed full" -WindowStyle Hidden
        
        Write-Host "Waiting for device to connect..."
        adb wait-for-device
        Write-Host "Device connected!"
    }
    else {
        Write-Host "=== Device detected. Skipping emulator launch. ==="
    }

    Write-Host "=== 5. Installing and Launching on All Devices ==="

    $Devices = adb devices | Select-String -Pattern "\tdevice$"
    foreach ($Dev in $Devices) {
        $Serial = $Dev.ToString().Split("`t")[0]
        if (-not [string]::IsNullOrWhiteSpace($Serial)) {
            Write-Host "--> Processing Device: $Serial"
            
            Write-Host "    Installing APK..."
            adb -s $Serial install -r $ApkPath
            
            Write-Host "    Launching Activity..."
            # Note: Package/Activity name assumes standard. If var in bash was hardcoded, here too.
            adb -s $Serial shell am start -n io.jor.cromagrally/org.libsdl.app.SDLActivity
            
            Write-Host "--> Done with $Serial"
        }
    }
}
