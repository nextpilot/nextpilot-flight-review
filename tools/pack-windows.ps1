<#
.SYNOPSIS
  Build the Windows release packages: NSIS installer (.exe) plus portable zip.

.DESCRIPTION
  1. Take plotscope-gui.exe and plotscope.exe from the build tree into a payload bin\ layout;
  2. Run windeployqt once for both exes to deploy Qt runtime and plugins;
  3. Copy the ADS ads.dll manually;
  4. Create the portable zip;
  5. If NSIS (makensis) is available, also build the installer; otherwise ship the zip only.

  IMPORTANT: keep this file ASCII-only. The workflow runs it with Windows
  PowerShell 5.1 (`powershell -File`), which decodes a BOM-less UTF-8 file as
  ANSI/GBK: non-ASCII strings turn into mojibake that breaks quote pairing and
  the whole script dies with a ParserError (seen in build-packages run 45553764956).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\pack-windows.ps1 -BuildDir build -QtDir C:\Qt\6.2.4\msvc2019_64 -OutDir dist
#>
param(
    [string]$BuildDir = "build",
    [string]$QtDir = "",
    [string]$OutDir = "dist",
    [string]$Version = ""
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path $BuildDir)) { throw "Build directory does not exist: $BuildDir" }

function Find-First($candidates, $what) {
    foreach ($c in $candidates) { if (Test-Path $c) { return (Resolve-Path $c).Path } }
    throw "Cannot find $what. Candidates: $($candidates -join ', ')"
}

# MSVC multi-config puts binaries under Release\; Ninja single-config uses the target dir directly.
$gui = Find-First @(
    "$BuildDir\src\application\assembly\Release\plotscope-gui.exe",
    "$BuildDir\src\application\assembly\plotscope-gui.exe"
) "plotscope-gui.exe"
$cli = Find-First @(
    "$BuildDir\src\application\app\Release\plotscope.exe",
    "$BuildDir\src\application\app\plotscope.exe"
) "plotscope.exe"

# Qt prefix: explicit argument first, otherwise derive it from Qt6_DIR in CMakeCache.
if (-not $QtDir) {
    $cache = Join-Path $BuildDir "CMakeCache.txt"
    if (Test-Path $cache) {
        $line = Select-String -Path $cache -Pattern '^Qt6_DIR:PATH=' | Select-Object -First 1
        if ($line) {
            $qt6 = ($line.Line -split '=', 2)[1]
            $QtDir = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $qt6))
        }
    }
}
if (-not $QtDir -or -not (Test-Path "$QtDir\bin\windeployqt.exe")) { throw "Qt prefix not found (windeployqt missing). Pass -QtDir explicitly." }

if (-not $Version) {
    $envFile = Join-Path $Root "versions.env"
    if (Test-Path $envFile) {
        $line = Select-String -Path $envFile -Pattern '^PS_APP_VERSION=' | Select-Object -First 1
        if ($line) { $Version = ($line.Line -split '=', 2)[1].Trim() }
    }
}
if (-not $Version) {
    $cm = Select-String -Path (Join-Path $BuildDir "CMakeCache.txt") -Pattern '^CMAKE_PROJECT_VERSION:STATIC=' | Select-Object -First 1
    if ($cm) { $Version = ($cm.Line -split '=', 2)[1] }
}
if (-not $Version) { $Version = (git describe --tags --always 2>$null) }
if (-not $Version) { $Version = "0.0.0" }
$Version = ($Version -replace '[^0-9A-Za-z.\-]', '')

Write-Host "[pack-windows] version=$Version  Qt=$QtDir  build=$BuildDir"

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$Stage = Join-Path $OutDir "stage"
if (Test-Path $Stage) { Remove-Item -Recurse -Force $Stage }
New-Item -ItemType Directory -Force -Path (Join-Path $Stage "bin") | Out-Null

Copy-Item $gui (Join-Path $Stage "bin\plotscope-gui.exe")
Copy-Item $cli (Join-Path $Stage "bin\plotscope.exe")

# windeployqt covers both exes at once: the CLI only links Core, a subset of the GUI,
# so scanning both finds every plugin without dragging in extras.
$deployQt = Join-Path $QtDir "bin\windeployqt.exe"
& $deployQt --release --no-translations --no-system-d3d-compiler --no-opengl-sw `
    (Join-Path $Stage "bin\plotscope-gui.exe") (Join-Path $Stage "bin\plotscope.exe")
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed (exit $LASTEXITCODE)" }

# ADS ships as a DLL on Windows; windeployqt does not know about it, so copy it by hand.
$adsCandidates = @(Get-ChildItem -Path $BuildDir -Recurse -Filter "ads.dll" -ErrorAction SilentlyContinue |
                   Select-Object -First 1)
if ($adsCandidates) {
    Copy-Item $adsCandidates.FullName (Join-Path $Stage "bin\ads.dll")
} else {
    Write-Warning "ads.dll not found; the GUI will fail to start with a missing DLL"
}

$zip = Join-Path $OutDir "PlotScope-$Version-windows-x86_64-portable.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path $Stage -DestinationPath $zip -CompressionLevel Optimal
Write-Host "[pack-windows] portable zip: $zip"

# NSIS installs under Program Files (x86)\NSIS or on PATH.
$makensis = $null
foreach ($c in @("$env:ProgramFiles\NSIS\makensis.exe", "${env:ProgramFiles(x86)}\NSIS\makensis.exe")) {
    if (Test-Path $c) { $makensis = $c; break }
}
if (-not $makensis) {
    $cmd = Get-Command makensis -ErrorAction SilentlyContinue
    if ($cmd) { $makensis = $cmd.Source }
}

if ($makensis) {
    $installer = Join-Path $OutDir "PlotScope-$Version-windows-x86_64-setup.exe"
    $nsi = Join-Path $Root "packaging\windows\installer.nsi"
    & $makensis "/DVERSION=$Version" "/DSTAGE=$Stage" "/DOUTFILE=$installer" $nsi
    if ($LASTEXITCODE -ne 0) { throw "makensis failed (exit $LASTEXITCODE)" }
    Write-Host "[pack-windows] installer: $installer"
} else {
    Write-Warning "makensis not found (NSIS). Producing the portable zip only; install NSIS 3 for the installer."
}

# Smoke test: run the CLI from the portable layout to prove all DLLs are in the payload.
$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
& (Join-Path $Stage "bin\plotscope.exe") --help | Out-Null
if ($LASTEXITCODE -ne 0) { throw "CLI smoke test failed (exit $LASTEXITCODE)" }
Write-Host "[pack-windows] CLI smoke test passed"
