<#
.SYNOPSIS
  PlotScope Windows 发布包：安装器（NSIS .exe）+ 便携 zip。

.DESCRIPTION
  1. 从构建目录取 plotscope-gui.exe 与 plotscope.exe，落成 payload 结构 bin\；
  2. windeployqt 一次性为两个 exe 部署 Qt 运行库与插件；
  3. 补拷 ADS 的 ads.dll；
  4. 打 portable zip；
  5. 有 NSIS（makensis）时再出安装包，否则只出 zip 并给出提示。

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
if (-not (Test-Path $BuildDir)) { throw "构建目录不存在：$BuildDir" }

function Find-First($candidates, $what) {
    foreach ($c in $candidates) { if (Test-Path $c) { return (Resolve-Path $c).Path } }
    throw "找不到 $what，候选：$($candidates -join ', ')"
}

# MSVC 多配置在 Release\ 下，Ninja 单配置直接在目标目录。
$gui = Find-First @(
    "$BuildDir\src\application\gui\Release\plotscope-gui.exe",
    "$BuildDir\src\application\gui\plotscope-gui.exe"
) "plotscope-gui.exe"
$cli = Find-First @(
    "$BuildDir\src\application\app\Release\plotscope.exe",
    "$BuildDir\src\application\app\plotscope.exe"
) "plotscope.exe"

# Qt 目录：参数优先，其次从 CMakeCache 的 Qt6_DIR 反推。
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
if (-not $QtDir -or -not (Test-Path "$QtDir\bin\windeployqt.exe")) { throw "找不到 Qt（windeployqt）。用 -QtDir 指定 Qt 前缀" }

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

Write-Host "[pack-windows] 版本=$Version  Qt=$QtDir  构建=$BuildDir"

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$Stage = Join-Path $OutDir "stage"
if (Test-Path $Stage) { Remove-Item -Recurse -Force $Stage }
New-Item -ItemType Directory -Force -Path (Join-Path $Stage "bin") | Out-Null

Copy-Item $gui (Join-Path $Stage "bin\plotscope-gui.exe")
Copy-Item $cli (Join-Path $Stage "bin\plotscope.exe")

# windeployqt 同时扫两个 exe：CLI 只链 Core，是 GUI 的子集，一起扫不会漏也不会多带插件。
$deployQt = Join-Path $QtDir "bin\windeployqt.exe"
& $deployQt --release --no-translations --no-system-d3d-compiler --no-opengl-sw `
    (Join-Path $Stage "bin\plotscope-gui.exe") (Join-Path $Stage "bin\plotscope.exe")
if ($LASTEXITCODE -ne 0) { throw "windeployqt 失败（exit $LASTEXITCODE）" }

# ADS 在 Windows 上是动态库，windeployqt 不认识，手工补。
$adsCandidates = @(Get-ChildItem -Path $BuildDir -Recurse -Filter "ads.dll" -ErrorAction SilentlyContinue |
                   Select-Object -First 1)
if ($adsCandidates) {
    Copy-Item $adsCandidates.FullName (Join-Path $Stage "bin\ads.dll")
} else {
    Write-Warning "未找到 ads.dll，GUI 启动会报缺少 DLL"
}

$zip = Join-Path $OutDir "PlotScope-$Version-windows-x86_64-portable.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path $Stage -DestinationPath $zip -CompressionLevel Optimal
Write-Host "[pack-windows] 便携包：$zip"

# NSIS：装在 Program Files (x86)\NSIS 或 PATH 里。
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
    if ($LASTEXITCODE -ne 0) { throw "makensis 失败（exit $LASTEXITCODE）" }
    Write-Host "[pack-windows] 安装包：$installer"
} else {
    Write-Warning "未找到 makensis（NSIS）。只产出便携 zip；要安装包请先安装 NSIS 3。"
}

# 冒烟：便携布局下直接跑 CLI，验证 DLL 全在 payload 里。
$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
& (Join-Path $Stage "bin\plotscope.exe") --help | Out-Null
if ($LASTEXITCODE -ne 0) { throw "CLI 冒烟失败（exit $LASTEXITCODE）" }
Write-Host "[pack-windows] CLI 冒烟通过"
