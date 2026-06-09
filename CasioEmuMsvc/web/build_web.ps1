param(
    [string]$BuildDir = "build-web",
    [string]$Config = "Release",
    [string]$EmsdkPath = "C:\Users\Administrator\emsdk"
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildPath = if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $ProjectRoot $BuildDir }
$EmsdkPath = (Resolve-Path $EmsdkPath).Path
$EmscriptenRoot = Join-Path $EmsdkPath "upstream\emscripten"
$ToolchainFile = Join-Path $EmscriptenRoot "cmake\Modules\Platform\Emscripten.cmake"
$CMakeExe = "cmake"
if (Test-Path "C:\Program Files\CMake\bin\cmake.exe") {
    $CMakeExe = "C:\Program Files\CMake\bin\cmake.exe"
}

if (-not (Test-Path $ToolchainFile)) {
    throw "Emscripten toolchain file was not found under $EmsdkPath."
}

$env:EMSDK = $EmsdkPath
$env:EM_CONFIG = Join-Path $EmsdkPath ".emscripten"
$env:EMSDK_NODE = Join-Path $EmsdkPath "node\22.16.0_64bit\bin\node.exe"
$env:EMSDK_PYTHON = Join-Path $EmsdkPath "python\3.13.3_64bit\python.exe"
$env:EM_CACHE = Join-Path $BuildPath "emscripten_cache"
$env:PATH = "$EmsdkPath;$EmscriptenRoot;$env:PATH"

New-Item -ItemType Directory -Force $BuildPath | Out-Null
New-Item -ItemType Directory -Force $env:EM_CACHE | Out-Null

& $CMakeExe --fresh -S $ProjectRoot -B $BuildPath -G Ninja `
    "-DCMAKE_TOOLCHAIN_FILE=$ToolchainFile" `
    "-DCMAKE_BUILD_TYPE=$Config" `
    "-DCASIOEMU_WEB=ON" `
    "-DBUILD_EXECUTABLE=ON"
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed with exit code $LASTEXITCODE."
}

& $CMakeExe --build $BuildPath --config $Config
if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed with exit code $LASTEXITCODE."
}

Write-Host "Built $BuildPath/CasioEmuWeb.html"
