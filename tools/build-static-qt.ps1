# Builds a static Qt for the single-file launcher, and only for that.
#
# A launcher that must be one executable cannot carry DLLs inside it, so Qt itself has to be linked
# in: that means a Qt built with -static and, for a comparable story on the C++ runtime,
# -static-runtime. It is a one-time cost -- roughly half an hour on sixteen threads -- and CI keeps
# the result in its cache, so only the first release of a Qt version pays for it.
#
#   pwsh tools/build-static-qt.ps1                      # downloads, configures, builds, installs
#   pwsh tools/build-static-qt.ps1 -Prefix D:\qt-static-6.10.3
#
# Run it from a developer prompt (or after vcvars64.bat), with ninja on PATH.

[CmdletBinding()]
param(
    [string]$QtVersion = '6.10.3',
    [string]$Prefix = (Join-Path $env:LOCALAPPDATA "KomorebiLauncherLite/Qt-static/$($QtVersion)/msvc2022_64"),
    [string]$SourceRoot = (Join-Path ([System.IO.Path]::GetTempPath()) "komorebi-qt-src-$QtVersion"),
    [string]$Jobs = 0
)

$ErrorActionPreference = 'Stop'

function Get-QtSource([string]$module) {
    $file = "$module-everywhere-src-$QtVersion.tar.xz"
    $target = Join-Path $SourceRoot $file
    if (Test-Path $target) { Write-Host "cached source: $target"; return }
    $urls = @(
        "https://download.qt.io/archive/qt/$($QtVersion.Substring(0, 4))/$QtVersion/submodules/$file",
        "https://mirrors.tuna.tsinghua.edu.cn/qt/archive/qt/$($QtVersion.Substring(0, 4))/$QtVersion/submodules/$file",
        "https://mirror.nju.edu.cn/qt/archive/qt/$($QtVersion.Substring(0, 4))/$QtVersion/submodules/$file"
    )
    foreach ($url in $urls) {
        try {
            Write-Host "downloading $url"
            Invoke-WebRequest -Uri $url -OutFile $target -TimeoutSec 600
            if ((Get-Item $target).Length -gt 1MB) { return }
        } catch {
            Write-Host "  failed: $($_.Exception.Message)"
        }
    }
    throw "could not download $file from any mirror"
}

function Expand-QtSource([string]$module) {
    $dir = Join-Path $SourceRoot "$module-everywhere-src-$QtVersion"
    if (Test-Path (Join-Path $dir 'CMakeLists.txt')) { Write-Host "unpacked already: $dir"; return $dir }
    Write-Host "unpacking $module"
    New-Item -ItemType Directory -Path $SourceRoot -Force | Out-Null
    tar -xf (Join-Path $SourceRoot "$module-everywhere-src-$QtVersion.tar.xz") -C $SourceRoot
    if (-not (Test-Path (Join-Path $dir 'CMakeLists.txt'))) { throw "unpacking $module produced no sources" }
    return $dir
}

# A configure that is small on purpose: no examples, no tests, no ICU (the launcher formats dates
# and numbers through the Windows APIs), third-party libraries from Qt's own copies because a static
# build cannot link DLL-based ones, and the C++ runtime linked in so the zip needs no vc_redist.
$configureArgs = @(
    '-static', '-static-runtime', '-release', '-opensource', '-confirm-license',
    '-nomake', 'examples', '-nomake', 'tests',
    '-no-icu',
    '-qt-zlib', '-qt-libpng', '-qt-libjpeg',
    '-prefix', $Prefix
)

# qtsvg draws the icon; qttools is where lrelease lives, and without lrelease CMake cannot compile the
# translation files at all -- qttools has to be here even though the launcher never uses it directly.
$addOns = 'qtsvg', 'qttools'

New-Item -ItemType Directory -Path $SourceRoot -Force | Out-Null
foreach ($module in @('qtbase') + $addOns) { Get-QtSource $module }

$qtbase = Expand-QtSource 'qtbase'
$buildBase = Join-Path $SourceRoot 'build-qtbase'
if (Test-Path $buildBase) { Remove-Item $buildBase -Recurse -Force }
New-Item -ItemType Directory -Path $buildBase -Force | Out-Null

Write-Host "configuring qtbase (this is the long part)"
Push-Location $buildBase
try {
    & (Join-Path $qtbase 'configure.bat') @configureArgs
    if ($LASTEXITCODE -ne 0) { throw "qtbase configure failed with exit code $LASTEXITCODE" }
    $parallel = if ($Jobs -gt 0) { $Jobs } else { [Environment]::ProcessorCount }
    & cmake --build . --parallel $parallel
    if ($LASTEXITCODE -ne 0) { throw "qtbase build failed with exit code $LASTEXITCODE" }
    & cmake --install .
    if ($LASTEXITCODE -ne 0) { throw "qtbase install failed with exit code $LASTEXITCODE" }
} finally { Pop-Location }

$parallel = if ($Jobs -gt 0) { $Jobs } else { [Environment]::ProcessorCount }
foreach ($module in $addOns) {
    $source = Expand-QtSource $module
    $build = Join-Path $SourceRoot "build-$module"
    if (Test-Path $build) { Remove-Item $build -Recurse -Force }
    New-Item -ItemType Directory -Path $build -Force | Out-Null

    Write-Host "configuring $module against the Qt that was just installed"
    Push-Location $build
    try {
        & (Join-Path $Prefix 'bin/qt-configure-module.bat') $source -- "-DCMAKE_INSTALL_PREFIX=$Prefix"
        if ($LASTEXITCODE -ne 0) { throw "$module configure failed with exit code $LASTEXITCODE" }
        & cmake --build . --parallel $parallel
        if ($LASTEXITCODE -ne 0) { throw "$module build failed with exit code $LASTEXITCODE" }
        & cmake --install .
        if ($LASTEXITCODE -ne 0) { throw "$module install failed with exit code $LASTEXITCODE" }
    } finally { Pop-Location }
}

foreach ($needed in @('lib/cmake/Qt6/Qt6Config.cmake', 'lib/Qt6Core.lib', 'lib/Qt6Widgets.lib',
                      'lib/Qt6Network.lib', 'lib/Qt6Svg.lib', 'plugins/platforms/qwindows.lib',
                      'lib/cmake/Qt6LinguistTools/Qt6LinguistToolsConfig.cmake', 'bin/lrelease.exe')) {
    if (-not (Test-Path (Join-Path $Prefix $needed))) { throw "the static Qt is missing $needed" }
}
$coreSize = [math]::Round((Get-Item (Join-Path $Prefix 'lib/Qt6Core.lib')).Length / 1MB, 1)
Write-Host ""
Write-Host "static Qt $QtVersion installed to $Prefix (Qt6Core.lib is $coreSize MB)"
