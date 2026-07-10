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

$RootDir = if ([string]::IsNullOrWhiteSpace($PSScriptRoot)) {
    (Get-Location).Path
}
else {
    $PSScriptRoot
}
$RootDir = [System.IO.Path]::GetFullPath($RootDir)
Set-Location -LiteralPath $RootDir

$AndroidCompilePlatform = "android-35"
$AndroidBuildToolsVersion = "37.0.0"
$AndroidNdkVersion = "29.0.14206865"
$GradleTask = if ([string]::IsNullOrWhiteSpace($env:GRADLE_TASK)) {
    "assembleDebug"
}
else {
    $env:GRADLE_TASK
}

function Get-CanonicalProperty {
    param (
        [string]$File,
        [string]$Name
    )

    if (-not (Test-Path -LiteralPath $File -PathType Leaf)) {
        throw "Canonical metadata file is missing: $File"
    }

    $Pattern = "^{0}=(.*)$" -f [regex]::Escape($Name)
    $Value = $null
    foreach ($Line in [System.IO.File]::ReadAllLines($File)) {
        if ($Line -match $Pattern) {
            if ($null -ne $Value) {
                throw "Duplicate $Name entry in $File."
            }
            $Value = $Matches[1].Trim()
        }
    }

    if ([string]::IsNullOrWhiteSpace($Value)) {
        throw "Missing $Name entry in $File."
    }
    return $Value
}

$VersionProperties = Join-Path $RootDir "version.properties"
$GameIdentifier = Get-CanonicalProperty $VersionProperties "GAME_IDENTIFIER"

function Test-AndroidSdkPath {
    param ([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path -LiteralPath $Path -PathType Container)) {
        return $false
    }

    if ($env:SKIP_GRADLE -eq "1") {
        return $true
    }

    return (Test-Path -LiteralPath (Join-Path $Path "platforms/$AndroidCompilePlatform") -PathType Container) `
        -and (Test-Path -LiteralPath (Join-Path $Path "build-tools/$AndroidBuildToolsVersion") -PathType Container)
}

function Test-AndroidNdkPath {
    param ([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) {
        return $false
    }

    $Toolchain = Join-Path $Path "build/cmake/android.toolchain.cmake"
    $SourceProperties = Join-Path $Path "source.properties"
    if (-not (Test-Path -LiteralPath $Toolchain -PathType Leaf) `
        -or -not (Test-Path -LiteralPath $SourceProperties -PathType Leaf)) {
        return $false
    }

    $Revision = $null
    foreach ($Line in Get-Content -LiteralPath $SourceProperties) {
        if ($Line -match '^\s*Pkg\.Revision\s*=\s*(.*?)\s*$') {
            $Revision = $Matches[1]
            break
        }
    }

    return $Revision -eq $AndroidNdkVersion
}

function Get-NormalizedPath {
    param ([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) {
        return ""
    }

    return [System.IO.Path]::GetFullPath($Path).TrimEnd(
        [char[]]@(
            [System.IO.Path]::DirectorySeparatorChar,
            [System.IO.Path]::AltDirectorySeparatorChar
        ))
}

function Test-SamePath {
    param (
        [string]$Left,
        [string]$Right
    )

    $Comparison = if ($HostIsWindows) {
        [System.StringComparison]::OrdinalIgnoreCase
    }
    else {
        [System.StringComparison]::Ordinal
    }
    return [string]::Equals(
        (Get-NormalizedPath $Left),
        (Get-NormalizedPath $Right),
        $Comparison)
}

if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
    throw "Ninja is not found in PATH. Install Ninja (for example, 'winget install ninja') or add it to PATH."
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
        if ($Candidate -and (Test-Path -LiteralPath $Candidate -PathType Container)) {
            Set-Item -Path "env:$Name" -Value $Candidate
            Write-Host "Auto-detected ${Name}: $Candidate"
            return
        }
    }
}

$HomeDir = Get-UserHome
if (-not $env:JAVA_HOME) {
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

$SdkCandidates = @()
if ($env:ANDROID_HOME) {
    $SdkCandidates += $env:ANDROID_HOME
}
if ($env:ANDROID_SDK_ROOT) {
    $SdkCandidates += $env:ANDROID_SDK_ROOT
}
if ($HostIsMacOS) {
    $SdkCandidates += "$HomeDir/Library/Android/sdk"
}
elseif ($HostIsLinux) {
    $SdkCandidates += "$HomeDir/Android/Sdk"
}
else {
    $SdkCandidates += "$env:LOCALAPPDATA\Android\Sdk"
}

$DetectedAndroidHome = $null
foreach ($SdkPath in $SdkCandidates) {
    if (Test-AndroidSdkPath $SdkPath) {
        $DetectedAndroidHome = [System.IO.Path]::GetFullPath($SdkPath)
        break
    }
}

if (-not $DetectedAndroidHome) {
    throw "Neither ANDROID_HOME nor ANDROID_SDK_ROOT points to a usable Android SDK, and none was auto-detected. Packaging requires $AndroidCompilePlatform and build-tools $AndroidBuildToolsVersion."
}
$env:ANDROID_HOME = $DetectedAndroidHome
$env:ANDROID_SDK_ROOT = $DetectedAndroidHome
Write-Host "=== Using Android SDK at $DetectedAndroidHome ==="

$PinnedSdkNdk = Join-Path $DetectedAndroidHome "ndk/$AndroidNdkVersion"
$DetectedAndroidNdk = $null
$NdkCandidates = @($PinnedSdkNdk, $env:ANDROID_NDK_HOME, $env:ANDROID_NDK_ROOT)
foreach ($NdkPath in $NdkCandidates) {
    if (Test-AndroidNdkPath $NdkPath) {
        $DetectedAndroidNdk = [System.IO.Path]::GetFullPath($NdkPath)
        break
    }
}

if (-not $DetectedAndroidNdk) {
    throw "Android NDK $AndroidNdkVersion is required. Install it with: sdkmanager --install `"ndk;$AndroidNdkVersion`""
}
$env:ANDROID_NDK_HOME = $DetectedAndroidNdk
$env:ANDROID_NDK_ROOT = $DetectedAndroidNdk
Write-Host "=== Using Android NDK $AndroidNdkVersion at $DetectedAndroidNdk ==="

$AndroidDir = Join-Path $RootDir "AndroidBuild"
$SdlDir = Join-Path $RootDir "extern/SDL3"

if (-not (Test-Path -LiteralPath (Join-Path $SdlDir "CMakeLists.txt") -PathType Leaf)) {
    throw "SDL3 submodule is missing at $SdlDir. Run: git submodule update --init --recursive"
}
Write-Host "=== Using SDL3 submodule at $SdlDir ==="

function Get-RequestedAbis {
    $Values = @()
    if ($Abi) {
        $Values += $Abi
    }
    elseif ($env:ABIS) {
        $Values += $env:ABIS
    }
    else {
        $Values += @("x86_64", "arm64-v8a")
    }

    return @($Values |
        ForEach-Object { $_ -split '[,\s]+' } |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
        Select-Object -Unique)
}

function Get-BuildDirForAbi {
    param ([string]$AbiName)

    switch ($AbiName) {
        "arm64-v8a" { return (Join-Path $RootDir "build-android") }
        "x86_64" { return (Join-Path $RootDir "build-android-x86") }
        default { throw "Unsupported Android ABI '$AbiName'. Supported ABIs: arm64-v8a, x86_64." }
    }
}

function Build-Abi {
    param (
        [string]$AbiName,
        [string]$BuildDir
    )

    $JniLibsDir = Join-Path $AndroidDir "app/src/main/jniLibs/$AbiName"
    $Toolchain = Join-Path $DetectedAndroidNdk "build/cmake/android.toolchain.cmake"
    $CachePath = Join-Path $BuildDir "CMakeCache.txt"

    Write-Host "=== Building for $AbiName in $BuildDir ==="

    # A CMake build tree cannot safely switch Android toolchains in place. Remove
    # only generated state if this ABI cache belongs to a different NDK.
    if (Test-Path -LiteralPath $CachePath -PathType Leaf) {
        $CachedNdk = $null
        foreach ($Line in Get-Content -LiteralPath $CachePath) {
            if ($Line -match '^CMAKE_ANDROID_NDK:[^=]+=(.*)$') {
                $CachedNdk = $Matches[1]
                break
            }
        }
        if (-not $CachedNdk -or -not (Test-SamePath $CachedNdk $DetectedAndroidNdk)) {
            Write-Host "Removing stale CMake cache in $BuildDir (different or unknown Android NDK)."
            Remove-Item -LiteralPath $BuildDir -Recurse -Force
        }
    }

    $ToolchainForCMake = if ($HostIsWindows) {
        $Toolchain -replace '\\', '/'
    }
    else {
        $Toolchain
    }

    Write-Host "Configuring $BuildDir..."
    & cmake -S $RootDir -B $BuildDir -G Ninja `
        "-DCMAKE_TOOLCHAIN_FILE=$ToolchainForCMake" `
        "-DANDROID_ABI=$AbiName" `
        "-DANDROID_PLATFORM=android-24" `
        "-DBUILD_SDL_FROM_SOURCE=ON" `
        "-DSDL_SHARED=ON" `
        "-DSDL_STATIC=OFF" `
        "-DCMAKE_BUILD_TYPE=Release"
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configuration failed for $AbiName."
    }

    & cmake --build $BuildDir --config Release
    if ($LASTEXITCODE -ne 0) {
        throw "CMake build failed for $AbiName."
    }

    Write-Host "--- Copying libraries to $JniLibsDir ---"
    New-Item -ItemType Directory -Force -Path $JniLibsDir | Out-Null
    Copy-Item -LiteralPath (Join-Path $BuildDir "libCroMagRally.so") `
        -Destination (Join-Path $JniLibsDir "libmain.so") -Force
    Copy-Item -LiteralPath (Join-Path $BuildDir "extern/SDL3/libSDL3.so") `
        -Destination (Join-Path $JniLibsDir "libSDL3.so") -Force
}

# Build the requested ABIs. CI can override the device + emulator default with ABIS.
$RequestedAbis = @(Get-RequestedAbis)
if ($RequestedAbis.Count -eq 0) {
    throw "ABIS must contain at least one Android ABI."
}
foreach ($RequestedAbi in $RequestedAbis) {
    if ($RequestedAbi -notin @("arm64-v8a", "x86_64")) {
        throw "Unsupported Android ABI '$RequestedAbi'. Supported ABIs: arm64-v8a, x86_64."
    }
}

# Clear every previously staged ABI once so a one-ABI build cannot package stale
# libraries left by an earlier multi-ABI build.
$JniLibsRoot = Join-Path $AndroidDir "app/src/main/jniLibs"
Remove-Item -LiteralPath $JniLibsRoot -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $JniLibsRoot | Out-Null

foreach ($RequestedAbi in $RequestedAbis) {
    Build-Abi $RequestedAbi (Get-BuildDirForAbi $RequestedAbi)
}

if ($env:SKIP_GRADLE -eq "1") {
    Write-Host "=== SKIP_GRADLE=1: native libraries built; skipping APK assembly. ==="
    exit 0
}

# Stage assets and generate Android's file manifest without modifying Data/files.txt
# in the source tree.
$AssetsDir = Join-Path $AndroidDir "app/src/main/assets"
Write-Host "=== Copying assets to $AssetsDir ==="
Remove-Item -LiteralPath $AssetsDir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $AssetsDir | Out-Null
Copy-Item -LiteralPath (Join-Path $RootDir "Data") -Destination $AssetsDir -Recurse -Force

$StagedDataDir = Join-Path $AssetsDir "Data"
$AssetsBaseLength = $AssetsDir.Length + 1
$Files = @(Get-ChildItem -LiteralPath $StagedDataDir -Recurse -File |
    Where-Object { $_.Name -ne "files.txt" } |
    ForEach-Object {
        $_.FullName.Substring($AssetsBaseLength).Replace("\", "/")
    } |
    Sort-Object)
[System.IO.File]::WriteAllText(
    (Join-Path $StagedDataDir "files.txt"),
    (($Files -join "`n") + "`n"),
    [System.Text.UTF8Encoding]::new($false))

foreach ($LicenseName in @("LICENSE.md", "THIRD-PARTY-LICENSES.md")) {
    $LicensePath = Join-Path $RootDir $LicenseName
    if (-not (Test-Path -LiteralPath $LicensePath -PathType Leaf)) {
        throw "Required license file is missing: $LicensePath"
    }
    Copy-Item -LiteralPath $LicensePath -Destination (Join-Path $AssetsDir $LicenseName) -Force
}

Write-Host "=== Running Gradle task: $GradleTask ==="
Push-Location $AndroidDir
try {
    if ($HostIsWindows) {
        & .\gradlew.bat --no-daemon $GradleTask
    }
    else {
        & ./gradlew --no-daemon $GradleTask
    }
    if ($LASTEXITCODE -ne 0) {
        throw "Gradle task $GradleTask failed."
    }
}
finally {
    Pop-Location
}
Write-Host "=== Success! Gradle task $GradleTask completed. ==="

if ($Run) {
    $ApkPath = if (-not [string]::IsNullOrWhiteSpace($env:ANDROID_APK_PATH)) {
        [System.IO.Path]::GetFullPath($env:ANDROID_APK_PATH)
    }
    elseif ($GradleTask -eq "assembleDebug") {
        Join-Path $AndroidDir "app/build/outputs/apk/debug/app-debug.apk"
    }
    else {
        throw "ANDROID_APK_PATH is required when running Gradle task $GradleTask."
    }
    if (-not (Test-Path -LiteralPath $ApkPath -PathType Leaf)) {
        throw "Cannot install missing APK: $ApkPath. Set ANDROID_APK_PATH when using a GRADLE_TASK other than assembleDebug."
    }

    $Devices = adb devices | Select-String -Pattern '\tdevice\s*$'
    if (-not $Devices) {
        $AvdName = "TestDevice"
        $EmulatorName = if ($HostIsWindows) { "emulator.exe" } else { "emulator" }
        $EmulatorBin = Join-Path $DetectedAndroidHome "emulator/$EmulatorName"
        if (-not (Test-Path -LiteralPath $EmulatorBin -PathType Leaf)) {
            $EmulatorCommand = Get-Command emulator -ErrorAction SilentlyContinue
            if (-not $EmulatorCommand) {
                throw "Android emulator executable not found."
            }
            $EmulatorBin = $EmulatorCommand.Source
        }

        Write-Host "=== No running device found. Launching emulator: $AvdName ==="
        $EmulatorArgs = @("-avd", $AvdName, "-no-boot-anim", "-netdelay", "none", "-netspeed", "full")
        if ($HostIsWindows) {
            Start-Process -FilePath $EmulatorBin -ArgumentList $EmulatorArgs -WindowStyle Hidden
        }
        else {
            Start-Process -FilePath $EmulatorBin -ArgumentList $EmulatorArgs | Out-Null
        }

        Write-Host "Waiting for device and package manager..."
        $DeviceReady = $false
        for ($Attempt = 0; $Attempt -lt 180; $Attempt++) {
            $DeviceState = (& adb get-state 2>$null)
            $BootComplete = (& adb shell getprop sys.boot_completed 2>$null)
            if ($DeviceState -eq "device" -and ([string]$BootComplete).Trim() -eq "1") {
                & adb shell cmd package list packages *> $null
                if ($LASTEXITCODE -eq 0) {
                    $DeviceReady = $true
                    break
                }
            }
            Start-Sleep -Seconds 1
        }
        if (-not $DeviceReady) {
            throw "Emulator did not become ready within 180 seconds."
        }
        Write-Host "Device connected and booted!"
    }
    else {
        Write-Host "=== Device detected. Skipping emulator launch. ==="
    }

    Write-Host "=== Installing and launching on all devices ==="
    $Devices = adb devices | Select-String -Pattern '\tdevice\s*$'
    foreach ($Device in $Devices) {
        $Serial = $Device.ToString().Split("`t")[0]
        if ([string]::IsNullOrWhiteSpace($Serial)) {
            continue
        }

        Write-Host "--> Processing device: $Serial"
        & adb -s $Serial install -r $ApkPath
        if ($LASTEXITCODE -ne 0) {
            throw "APK installation failed on $Serial."
        }
        & adb -s $Serial shell am start -n "$GameIdentifier/org.libsdl.app.SDLActivity"
        if ($LASTEXITCODE -ne 0) {
            throw "Activity launch failed on $Serial."
        }
        Write-Host "--> Done with $Serial"
    }
}
