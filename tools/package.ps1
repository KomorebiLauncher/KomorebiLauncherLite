# Packs a built launcher into what a release ships: a zip that runs where it is unpacked, an
# installer that puts it in the start menu, and one executable with everything inside it.
#
#   pwsh tools/package.ps1 -Version 0.1.0                  # zip and installer, from a normal build
#   pwsh tools/package.ps1 -Version 0.1.0 -Static -Verify  # the single file, from a static build
#
# The dynamic build loads Qt from DLLs, so it becomes the zip and the installer. The single file is
# a different build altogether -- Qt linked in, see tools/build-static-qt.ps1 -- and in that mode
# this script only has to prove the executable stands alone.
#
# -Verify starts the staged copy with a stripped PATH, so a missing runtime file fails here instead
# of on a player's machine. The installer needs Inno Setup; without it the zip is still built.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$BuildDir = 'build',
    [string]$OutDir = 'dist',
    [string]$QtBin,
    [string]$Iscc,
    [switch]$Verify,
    [switch]$Static,
    [switch]$SkipInstaller
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$build = if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $repo $BuildDir }
$stageRoot = if ([System.IO.Path]::IsPathRooted($OutDir)) { $OutDir } else { Join-Path $repo $OutDir }
$exe = Join-Path $build 'komorebi-lite.exe'
$stage = Join-Path $stageRoot "KomorebiLite-$Version"
$zip = Join-Path $stageRoot "KomorebiLite-$Version-windows-x64.zip"

if (-not (Test-Path $exe)) { throw "no launcher at $exe; build first (build.bat)" }

function Get-Sha256([string]$path) { (Get-FileHash $path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Format-Size([string]$path) { [math]::Round((Get-Item $path).Length / 1MB, 1) }

# A statically linked launcher is already the single file the release offers. Nothing is copied next
# to it, and the only question worth answering is whether it starts with no Qt anywhere else on the
# machine: so it is run alone in an empty directory, with the system PATH and nothing else.
if ($Static) {
    New-Item -ItemType Directory -Path $stageRoot -Force | Out-Null
    $single = Join-Path $stageRoot "KomorebiLite-$Version.exe"
    Copy-Item $exe $single -Force
    Write-Host "single:   $single ($(Format-Size $single) MB)"
    Write-Host "          sha256 $(Get-Sha256 $single)"

    if ($Verify) {
        $alone = Join-Path ([System.IO.Path]::GetTempPath()) ("komorebi-alone-" + [guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path $alone -Force | Out-Null
        $savedPath = $env:PATH
        $savedData = $env:KOMOREBI_DATA
        $savedPlugin = $env:QT_PLUGIN_PATH
        $savedPlatform = $env:QT_QPA_PLATFORM
        try {
            Copy-Item $single (Join-Path $alone 'komorebi-lite.exe')
            $env:PATH = "$env:SystemRoot\system32;$env:SystemRoot"
            $env:KOMOREBI_DATA = Join-Path $alone 'data'
            $env:QT_PLUGIN_PATH = ''
            # No offscreen plugin is linked into a single file; the window plugin is what it has.
            $env:QT_QPA_PLATFORM = $null
            $process = Start-Process -FilePath (Join-Path $alone 'komorebi-lite.exe') -PassThru `
                -ArgumentList @('--page', 'about', '--screenshot', (Join-Path $alone 'verify.png'))
            try {
                if (-not $process.WaitForExit(120000)) { throw "the single file did not finish within two minutes" }
                if ($process.ExitCode -ne 0) { throw "the single file could not start (exit code $($process.ExitCode))" }
                if (-not (Test-Path (Join-Path $alone 'verify.png'))) { throw "the single file painted nothing" }
                Write-Host "alone in an empty directory: ok"
            } finally {
                if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit(10000) | Out-Null }
            }
        } finally {
            $env:PATH = $savedPath
            $env:KOMOREBI_DATA = $savedData
            $env:QT_PLUGIN_PATH = $savedPlugin
            $env:QT_QPA_PLATFORM = $savedPlatform
            Remove-Item $alone -Recurse -Force -ErrorAction SilentlyContinue
        }
    }

    if ($env:GITHUB_OUTPUT) {
        "single=$single" | Out-File -FilePath $env:GITHUB_OUTPUT -Append -Encoding utf8
        "sha256_single=$(Get-Sha256 $single)" | Out-File -FilePath $env:GITHUB_OUTPUT -Append -Encoding utf8
    }
    return
}

if (-not $QtBin) {
    if ($env:QT_ROOT_DIR -and (Test-Path (Join-Path $env:QT_ROOT_DIR 'bin/windeployqt.exe'))) {
        $QtBin = Join-Path $env:QT_ROOT_DIR 'bin'
    } elseif (Get-Command windeployqt.exe -ErrorAction SilentlyContinue) {
        $QtBin = Split-Path -Parent (Get-Command windeployqt.exe).Source
    } else {
        $QtBin = Join-Path $env:LOCALAPPDATA 'KomorebiLauncherLite/Qt/6.10.3/msvc2022_64/bin'
    }
}
$windeployqt = Join-Path $QtBin 'windeployqt.exe'
if (-not (Test-Path $windeployqt)) { throw "no windeployqt at $windeployqt" }

if (Test-Path $stage) {
    # A launcher left running from an earlier look at the package keeps its DLLs open; Windows
    # refuses to delete them until the process is gone.
    for ($attempt = 0; $attempt -lt 5 -and (Test-Path $stage); $attempt++) {
        Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
        if (Test-Path $stage) { Start-Sleep -Milliseconds 500 }
    }
    if (Test-Path $stage) { throw "cannot clear $stage; close the launcher still running from it" }
}
New-Item -ItemType Directory -Path $stage -Force | Out-Null

Copy-Item $exe $stage
# The theme is read from disk at startup and the OptiFine helper is a class file, so neither can be
# compiled into the executable; both have to travel beside it.
foreach ($dir in @('theme', 'optifine-runner')) {
    $from = Join-Path $build $dir
    if (Test-Path $from) { Copy-Item $from (Join-Path $stage $dir) -Recurse -Force }
    else { Write-Warning "the build has no $dir; the release will miss it" }
}
foreach ($file in @('LICENSE', 'NOTICE.md', 'README.md')) {
    $from = Join-Path $repo $file
    if (Test-Path $from) { Copy-Item $from $stage -Force }
}

# Qt's own translation files are not used (this launcher carries its three languages in the binary),
# and Widgets never needs the software OpenGL fallback.
& $windeployqt --release --no-translations --no-opengl-sw --no-system-d3d-compiler --compiler-runtime --dir $stage $exe
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE" }

# windeployqt hands over the Visual C++ installer, which asks a player to install something before
# the launcher starts. A portable zip carries the three runtime files instead.
Remove-Item (Join-Path $stage 'vc_redist.x64.exe') -Force -ErrorAction SilentlyContinue
$crt = @()
if ($env:VCINSTALLDIR) {
    $crt = Get-ChildItem -Path (Join-Path $env:VCINSTALLDIR 'Redist/MSVC/*/x64/Microsoft.VC*.CRT') -Directory -ErrorAction SilentlyContinue |
        Select-Object -First 1
}
if ($crt) {
    foreach ($name in @('msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll', 'vcruntime140.dll', 'vcruntime140_1.dll', 'concrt140.dll')) {
        $file = Join-Path $crt.FullName $name
        if (Test-Path $file) { Copy-Item $file $stage -Force }
    }
} else {
    Write-Warning "no Visual Studio redistributable found; the zip assumes the C++ runtime is installed (run from a developer prompt with vcvars64)"
}

# The DirectX shader compiler belongs to Qt Quick: Widgets draws with GDI and the raster engine.
foreach ($name in @('dxcompiler.dll', 'dxil.dll')) {
    Remove-Item (Join-Path $stage $name) -Force -ErrorAction SilentlyContinue
}

foreach ($required in @('Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6Network.dll', 'Qt6Svg.dll',
                        'platforms/qwindows.dll', 'tls/qschannelbackend.dll', 'theme/light/theme.css',
                        'msvcp140.dll', 'vcruntime140.dll')) {
    if (-not (Test-Path (Join-Path $stage $required))) { throw "the packaged launcher is missing $required" }
}

if ($Verify) {
    # A fresh data directory and the system PATH only: if the executable can start and render with
    # this, it can start on a machine that has never seen Qt.
    $data = Join-Path ([System.IO.Path]::GetTempPath()) ("komorebi-pack-" + [guid]::NewGuid().ToString('N'))
    $savedPath = $env:PATH
    $savedData = $env:KOMOREBI_DATA
    $savedPlugin = $env:QT_PLUGIN_PATH
    try {
        $env:PATH = "$env:SystemRoot\system32;$env:SystemRoot"
        $env:KOMOREBI_DATA = $data
        $env:QT_PLUGIN_PATH = ''
        $env:QT_QPA_PLATFORM = 'offscreen'
        & (Join-Path $stage 'komorebi-lite.exe') --selftest
        if ($LASTEXITCODE -ne 0) { throw "the packaged launcher failed its self test (exit code $LASTEXITCODE)" }
        $log = Join-Path $stage 'selftest.log'
        if (Test-Path $log) { Write-Host (Get-Content $log -Raw) }
        # A windowed run as well: the self test renders without a screen, so it cannot catch a
        # missing qwindows plugin. The page hook paints a window and quits by itself. The platform
        # choice has to go away first, or this run asks for the offscreen plugin the zip does not
        # carry.
        $env:QT_QPA_PLATFORM = $null
        $shot = Join-Path $stage 'verify.png'
        $process = Start-Process -FilePath (Join-Path $stage 'komorebi-lite.exe') -PassThru `
            -ArgumentList @('--page', 'about', '--screenshot', $shot)
        try {
            if (-not $process.WaitForExit(90000)) { throw "the packaged launcher did not paint a window within 90 seconds" }
            if ($process.ExitCode -ne 0) { throw "the packaged launcher could not open a window (exit code $($process.ExitCode))" }
            if (-not (Test-Path $shot) -or (Get-Item $shot).Length -lt 1024) { throw "the packaged launcher painted nothing" }
            Write-Host "windowed self check: ok ($((Get-Item $shot).Length) bytes of pixels)"
        } finally {
            # A launcher that stopped on an error dialog would keep its files open and hang CI.
            if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit(10000) | Out-Null }
        }
    } finally {
        $env:PATH = $savedPath
        $env:KOMOREBI_DATA = $savedData
        $env:QT_PLUGIN_PATH = $savedPlugin
        $env:QT_QPA_PLATFORM = $null
        Remove-Item $data -Recurse -Force -ErrorAction SilentlyContinue
        Get-ChildItem $stage -Filter '*.log' | Remove-Item -Force -ErrorAction SilentlyContinue
        Remove-Item (Join-Path $stage 'verify.png') -Force -ErrorAction SilentlyContinue
    }
}

if (Test-Path $zip) {
    for ($attempt = 0; $attempt -lt 5 -and (Test-Path $zip); $attempt++) {
        Remove-Item $zip -Force -ErrorAction SilentlyContinue
        if (Test-Path $zip) { Start-Sleep -Milliseconds 500 }
    }
    if (Test-Path $zip) { throw "cannot replace $zip; it is open somewhere" }
}
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -CompressionLevel Optimal

Write-Host ""
Write-Host "zip:      $zip ($(Format-Size $zip) MB)"
$hashes = [ordered]@{ zip = (Get-Sha256 $zip) }
Write-Host "          sha256 $($hashes['zip'])"

# The installer: the same files, placed in %LOCALAPPDATA%\Programs, with a start menu entry and an
# entry in "add or remove programs" that knows how to remove it again.
if (-not $SkipInstaller) {
    if (-not $Iscc) {
        foreach ($candidate in @(
                (Join-Path $env:LOCALAPPDATA 'Programs/Inno Setup 6/ISCC.exe'),
                'C:\Program Files (x86)\Inno Setup 6\ISCC.exe',
                'C:\Program Files\Inno Setup 6\ISCC.exe')) {
            if (Test-Path $candidate) { $Iscc = $candidate; break }
        }
    }
    if (-not $Iscc -and (Get-Command ISCC.exe -ErrorAction SilentlyContinue)) {
        $Iscc = (Get-Command ISCC.exe).Source
    }
    if ($Iscc) {
        $setup = Join-Path $stageRoot "KomorebiLite-Setup-$Version.exe"
        Remove-Item $setup -Force -ErrorAction SilentlyContinue
        & $Iscc "/DAppVersion=$Version" "/DStageDir=$stage" "/DOutDir=$stageRoot" (Join-Path $PSScriptRoot 'setup.iss') | Out-Null
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path $setup)) { throw "Inno Setup could not build the installer (exit code $LASTEXITCODE)" }
        $hashes['setup'] = Get-Sha256 $setup
        Write-Host "installer: $setup ($(Format-Size $setup) MB)"
        Write-Host "          sha256 $($hashes['setup'])"
    } else {
        Write-Warning "no Inno Setup found, so no installer was built (winget install JRSoftware.InnoSetup)"
    }
}

# The paths and hashes go to the workflow: the upload step names the files, the release notes
# carry the hashes so a download can be checked against them.
if ($env:GITHUB_OUTPUT) {
    "zip=$zip" | Out-File -FilePath $env:GITHUB_OUTPUT -Append -Encoding utf8
    "sha256_zip=$($hashes['zip'])" | Out-File -FilePath $env:GITHUB_OUTPUT -Append -Encoding utf8
    if ($hashes.Contains('setup')) {
        "setup=$setup" | Out-File -FilePath $env:GITHUB_OUTPUT -Append -Encoding utf8
        "sha256_setup=$($hashes['setup'])" | Out-File -FilePath $env:GITHUB_OUTPUT -Append -Encoding utf8
    }
}
