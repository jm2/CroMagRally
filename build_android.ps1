param(
    [switch]$Run,
    [string[]]$Abi
)

$ErrorActionPreference = "Stop"

$HostIsWindows = Get-Variable -Name IsWindows -ValueOnly -ErrorAction SilentlyContinue
$HostIsLinux = Get-Variable -Name IsLinux -ValueOnly -ErrorAction SilentlyContinue
$HostIsMacOS = Get-Variable -Name IsMacOS -ValueOnly -ErrorAction SilentlyContinue

if ($null -eq $HostIsWindows) {
    $HostIsWindows = $env:OS -eq "Windows_NT"
}
if ($null -eq $HostIsLinux) {
    $HostIsLinux = $false
}
if ($null -eq $HostIsMacOS) {
    $HostIsMacOS = $false
}

$AndroidCompilePlatform = "android-35"
$AndroidBuildToolsVersion = "37.0.0"

function Test-AndroidSdkPath {
    param ([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path $Path)) {
        return $false
    }

    if ($env:SKIP_GRADLE -eq "1") {
        return $true
    }

    return (Test-Path (Join-Path $Path "platforms/$AndroidCompilePlatform")) `
        -and (Test-Path (Join-Path $Path "build-tools/$AndroidBuildToolsVersion"))
}

function Test-AndroidNdkPath {
    param ([string]$Path)

    return -not [string]::IsNullOrWhiteSpace($Path) `
        -and (Test-Path (Join-Path $Path "build/cmake/android.toolchain.cmake"))
}

# Configuration
if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
    Write-Error "Ninja is not found in your PATH. Please install Ninja (e.g., 'winget install ninja') or add it to your PATH."
}

function Get-UserHome {
    if ($env:HOME) {
        return $env:HOME
    }

    if ($env:USERPROFILE) {
        return $env:USERPROFILE
    }

    return [Environment]::GetFolderPath([Environment+SpecialFolder]::UserProfile)
}

function Set-EnvFromFirstExistingPath {
    param (
        [string]$Name,
        [string[]]$Candidates
    )

    foreach ($Candidate in $Candidates) {
        if ($Candidate -and (Test-Path $Candidate)) {
            Set-Item -Path "env:$Name" -Value $Candidate
            Write-Host "Auto-detected ${Name}: $Candidate"
            return
        }
    }
}

if (-not $env:JAVA_HOME) {
    $HomeDir = Get-UserHome
    $PossibleJava = if ($HostIsMacOS) {
        @(
            "/Applications/Android Studio.app/Contents/jbr/Contents/Home",
            "/Applications/Android Studio.app/Contents/jre/Contents/Home"
        )
    }
    elseif ($HostIsLinux) {
        @(
            "/opt/android-studio/jbr",
            "$HomeDir/android-studio/jbr",
            "/snap/android-studio/current/android-studio/jbr"
        )
    }
    else {
        @(
            "$env:ProgramFiles\Android\Android Studio\jbr",
            "$env:ProgramFiles\Android\Android Studio\jre",
            "${env:ProgramFiles(x86)}\Android\Android Studio\jbr",
            "${env:ProgramFiles(x86)}\Android\Android Studio\jre"
        )
    }

    Set-EnvFromFirstExistingPath "JAVA_HOME" $PossibleJava
}

if ($env:ANDROID_HOME -and -not (Test-AndroidSdkPath $env:ANDROID_HOME)) {
    Write-Host "Ignoring incomplete ANDROID_HOME: $env:ANDROID_HOME"
    $env:ANDROID_HOME = $null
}

if (-not $env:ANDROID_HOME -and $env:ANDROID_SDK_ROOT -and (Test-AndroidSdkPath $env:ANDROID_SDK_ROOT)) {
    $env:ANDROID_HOME = $env:ANDROID_SDK_ROOT
}

if (-not $env:ANDROID_HOME) {
    $HomeDir = Get-UserHome
    $PossibleSdk = if ($HostIsMacOS) {
        @("$HomeDir/Library/Android/sdk")
    }
    elseif ($HostIsLinux) {
        @("$HomeDir/Android/Sdk")
    }
    else {
        @("$env:LOCALAPPDATA\Android\Sdk")
    }

    foreach ($SdkPath in $PossibleSdk) {
        if (Test-AndroidSdkPath $SdkPath) {
            $env:ANDROID_HOME = $SdkPath
            Write-Host "Auto-detected ANDROID_HOME: $SdkPath"
            break
        }
    }
}

if ($env:ANDROID_HOME) {
    $env:ANDROID_SDK_ROOT = $env:ANDROID_HOME
}

if (-not (Test-AndroidNdkPath $env:ANDROID_NDK_HOME) -and (Test-AndroidNdkPath $env:ANDROID_NDK_ROOT)) {
    $env:ANDROID_NDK_HOME = $env:ANDROID_NDK_ROOT
}

if (-not (Test-AndroidNdkPath $env:ANDROID_NDK_HOME) -and $env:ANDROID_HOME) {
    $NdkRoot = Join-Path $env:ANDROID_HOME "ndk"
    if (Test-Path $NdkRoot) {
        $LatestNdk = Get-ChildItem $NdkRoot -Directory |
            Sort-Object {
                try { [version]$_.Name } catch { [version]"0.0" }
            } -Descending |
            Select-Object -First 1

        if ($LatestNdk) {
            $env:ANDROID_NDK_HOME = $LatestNdk.FullName
            Write-Host "Auto-detected ANDROID_NDK_HOME: $($LatestNdk.FullName)"
        }
    }
}

if (-not (Test-AndroidSdkPath $env:ANDROID_HOME)) {
    Write-Error "ANDROID_HOME is not set and no Android SDK was auto-detected."
}

if (-not (Test-AndroidNdkPath $env:ANDROID_NDK_HOME)) {
    Write-Error "ANDROID_NDK_HOME is not set and no NDK was found under ANDROID_HOME."
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

function Get-RequestedAbis {
    $RequestedAbis = @()

    if ($Abi) {
        $RequestedAbis += $Abi
    }
    elseif ($env:ABIS) {
        $RequestedAbis += $env:ABIS
    }
    else {
        $RequestedAbis += @("x86_64", "arm64-v8a")
    }

    $RequestedAbis |
        ForEach-Object { $_ -split "[,\s]+" } |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
}

function Get-BuildDirForAbi {
    param ([string]$Abi)

    switch ($Abi) {
        "arm64-v8a" { return "build-android" }
        "x86_64"    { return "build-android-x86" }
        default     { return "build-android-$Abi" }
    }
}

# Function to build and copy for a specific ABI
function Build-Abi {
    param (
        [string]$Abi,
        [string]$BuildDir
    )

    $JniLibsDir = "$AndroidDir/app/src/main/jniLibs/$Abi"
    
    Write-Host "=== Building for $Abi in $BuildDir ==="

    # Configure
    Write-Host "Configuring $BuildDir..."
    New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
    Push-Location $BuildDir

    $Toolchain = Join-Path $env:ANDROID_NDK_HOME "build/cmake/android.toolchain.cmake"
    if (-not (Test-Path $Toolchain)) {
        Write-Error "Android CMake toolchain file not found: $Toolchain"
    }

    # Fix path for CMake if on Windows (forward slashes are generally safer for cmake args)
    if ($HostIsWindows) {
        $Toolchain = $Toolchain -replace "\\", "/"
    }

    try {
        cmake -GNinja `
            "-DCMAKE_TOOLCHAIN_FILE=$Toolchain" `
            "-DANDROID_ABI=$Abi" `
            "-DANDROID_PLATFORM=android-24" `
            "-DBUILD_SDL_FROM_SOURCE=ON" `
            "-DCMAKE_BUILD_TYPE=Release" `
            "-DSDL3_DIR=extern/SDL3-3.2.8" `
            ..
    }
    finally {
        Pop-Location
    }

    cmake --build $BuildDir --config Release
    
    Write-Host "--- Copying libraries to $JniLibsDir ---"
    New-Item -ItemType Directory -Force -Path $JniLibsDir | Out-Null
    Copy-Item "$BuildDir/libCroMagRally.so" "$JniLibsDir/libmain.so"
    Copy-Item "$BuildDir/extern/SDL3-3.2.8/libSDL3.so" "$JniLibsDir/libSDL3.so"
}

# Build the requested ABIs. ABIS defaults to the device + emulator pair; CI can set
# ABIS=arm64-v8a SKIP_GRADLE=1 for a fast native-only smoke build.
$RequestedAbis = @(Get-RequestedAbis)
foreach ($RequestedAbi in $RequestedAbis) {
    Build-Abi $RequestedAbi (Get-BuildDirForAbi $RequestedAbi)
}

if ($env:SKIP_GRADLE -eq "1") {
    Write-Host "=== SKIP_GRADLE=1: native libraries built; skipping APK assembly. ==="
    exit 0
}

# 0. Copy Assets
Write-Host "=== Generating Data/files.txt ==="
$BasePath = (Get-Item .).FullName
$BasePathLength = $BasePath.Length + 1
$Files = Get-ChildItem -Path "Data" -Recurse -File |
    Where-Object { $_.Name -ne "files.txt" } |
    ForEach-Object {
    $_.FullName.Substring($BasePathLength).Replace("\", "/")
} | Sort-Object
[System.IO.File]::WriteAllText(
    (Join-Path $BasePath "Data/files.txt"),
    (($Files -join "`n") + "`n"),
    [System.Text.UTF8Encoding]::new($false))

$AssetsDir = "$AndroidDir/app/src/main/assets"
Write-Host "=== Copying Assets to $AssetsDir ==="
Remove-Item -Path $AssetsDir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $AssetsDir | Out-Null
Copy-Item -Path "Data" -Destination "$AssetsDir" -Recurse -Force

Write-Host "=== 3. Assembling Debug APK (Gradle) ==="
Push-Location $AndroidDir
if ($HostIsWindows) {
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
        $EmulatorArgs = @("-avd", $AvdName, "-no-boot-anim", "-netdelay", "none", "-netspeed", "full")
        if ($HostIsWindows) {
            Start-Process -FilePath $EmulatorBin -ArgumentList $EmulatorArgs -WindowStyle Hidden
        }
        else {
            Start-Process -FilePath $EmulatorBin -ArgumentList $EmulatorArgs | Out-Null
        }
        
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
