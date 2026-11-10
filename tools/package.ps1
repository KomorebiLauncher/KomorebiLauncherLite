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

# The Visual C++ files that travel next to the executable have to be the ones that came with the
# toolset that compiled it, and the machine's system directory is not a substitute: a GitHub runner
# carries a runtime older than its own compiler. A standard library object built from the newer
# headers but serviced by the older runtime is not a supported mix, and it faulted at
# MSVCP140.dll+0x13080 in the runtime's thread start-up code the moment the downloader's own thread
# began -- that is what took the launcher down when the modpack page opened on 0.1.1, with no
# message and no window. The toolset is read from the build's own cache, so an old system runtime
# can never be picked up silently again.
function Get-VcRuntimeDir([string]$buildDir) {
    $toolsetDir = $null
    $cache = Join-Path $buildDir 'CMakeCache.txt'
    if (Test-Path $cache) {
        $line = Select-String -Path $cache -Pattern '^CMAKE_CXX_COMPILER:FILEPATH=(.+)$' -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($line) {
            # ...\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\cl.exe
            $compiler = $line.Matches[0].Groups[1].Value.Trim()
            $toolsetDir = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $compiler)))
        }
    }

    $roots = @()
    if ($toolsetDir) { $roots += (Join-Path (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $toolsetDir))) 'Redist/MSVC') }
    if ($env:VCINSTALLDIR) { $roots += (Join-Path $env:VCINSTALLDIR 'Redist/MSVC') }

    $wanted = $null
    if ($toolsetDir) { $wanted = ((Split-Path -Leaf $toolsetDir) -split '\.')[0..1] -join '.' }

    foreach ($root in $roots) {
        if (-not (Test-Path $root)) { continue }
        $candidates = @(Get-ChildItem -Path (Join-Path $root '*/x64/Microsoft.VC*.CRT') -Directory -ErrorAction SilentlyContinue)
        if (-not $candidates) { continue }
        foreach ($candidate in $candidates) {
            # <root>\<version>\x64\Microsoft.VC143.CRT -- the version is two levels up, and there is
            # also a plain "v143" junction that carries no version at all.
            $versioned = Split-Path -Parent (Split-Path -Parent $candidate.FullName)
            $version = Split-Path -Leaf $versioned
            if ($version -notmatch '^\d+\.\d+') { continue }
            $short = ($version -split '\.')[0..1] -join '.'
            if ($wanted -and $short -ne $wanted) { continue }
            return $candidate.FullName
        }
    }
    return $null
}

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

                # Rendering, not just starting: the self test looks at the pixels of a window, a
                # button and a field. A build without its theme is still a running process that
                # writes a screenshot, so only this catches it.
                $selftest = Start-Process -FilePath (Join-Path $alone 'komorebi-lite.exe') -PassThru `
                    -ArgumentList @('--selftest')
                if (-not $selftest.WaitForExit(120000)) { throw "the single file did not finish its self test within two minutes" }
                if ($selftest.ExitCode -ne 0) {
                    $log = Join-Path $alone 'selftest.log'
                    if (Test-Path $log) { Write-Host (Get-Content $log -Raw) }
                    throw "the single file failed its self test (exit code $($selftest.ExitCode))"
                }

                # The modpack page starts the download thread; a build whose runtime handling of
                # threads is broken dies there without a word, so the page is opened here too.
                $packsShot = Join-Path $alone 'verify-modpacks.png'
                $savedDelay = $env:KOMOREBI_SHOT_DELAY
                $env:KOMOREBI_SHOT_DELAY = '4000'
                $packs = Start-Process -FilePath (Join-Path $alone 'komorebi-lite.exe') -PassThru `
                    -ArgumentList @('--page', 'modpacks', '--screenshot', $packsShot)
                try {
                    if (-not $packs.WaitForExit(120000)) { throw "the single file did not open the modpack page within two minutes" }
                    if ($packs.ExitCode -ne 0) {
                        throw ("the modpack page brought the single file down (exit code 0x{0:X8})" -f $packs.ExitCode)
                    }
                    if (-not (Test-Path $packsShot)) { throw "the single file painted no modpack page" }
                } finally {
                    if (-not $packs.HasExited) { $packs.Kill(); $packs.WaitForExit(10000) | Out-Null }
                    $env:KOMOREBI_SHOT_DELAY = $savedDelay
                }
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
# the launcher starts. A portable zip carries the runtime files instead, taken from the toolset that
# compiled the launcher rather than from whatever the building machine has installed.
Remove-Item (Join-Path $stage 'vc_redist.x64.exe') -Force -ErrorAction SilentlyContinue
$crt = Get-VcRuntimeDir $build
if (-not $crt) { throw "no Visual C++ redistributable matching the compiler behind $exe; a release must not ship a runtime it did not build against" }
# The files a program built with 14.3x and later can need; the ones that are absent are simply skipped.
$runtimes = @('msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll', 'msvcp140_atomic_wait.dll',
              'msvcp140_codecvt_ids.dll', 'vcruntime140.dll', 'vcruntime140_1.dll',
              'vcruntime140_threads.dll', 'concrt140.dll')
foreach ($name in $runtimes) {
    $file = Join-Path $crt $name
    if (Test-Path $file) { Copy-Item $file $stage -Force }
}
Write-Host "runtime:  $((Get-Item (Join-Path $stage 'msvcp140.dll')).VersionInfo.FileVersion) from $crt"
# Being present is not enough, the files have to agree with the compiler whose headers built the
# executable. This is the check that would have caught the release that crashed on the modpack page.
$runtimeVersion = (Get-Item (Join-Path $stage 'msvcp140.dll')).VersionInfo.FileVersion
$runtimeMinor = ($runtimeVersion -split '\.')[0..1] -join '.'
$redistMinor = ((Split-Path -Leaf (Split-Path -Parent (Split-Path -Parent $crt))) -split '\.')[0..1] -join '.'
if ($runtimeMinor -ne $redistMinor) { throw "the copied Visual C++ runtime is $runtimeVersion, but $crt says $redistMinor" }

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
        # The zip carries the window plugin and nothing else, so every check here has to use it;
        # asking for the offscreen plugin leaves a modal "no platform plugin could be initialized"
        # box on screen and a process that never ends. The launcher also has to be waited for by
        # hand: it is a windowed executable, PowerShell does not wait for one, and that is how a
        # self test that never actually ran managed to look green in CI.
        $env:QT_QPA_PLATFORM = $null
        $selftest = Start-Process -FilePath (Join-Path $stage 'komorebi-lite.exe') -PassThru -ArgumentList @('--selftest')
        try {
            if (-not $selftest.WaitForExit(120000)) { throw "the packaged launcher did not finish its self test within two minutes" }
            if ($selftest.ExitCode -ne 0) { throw "the packaged launcher failed its self test (exit code $($selftest.ExitCode))" }
            Write-Host "rendering self test: ok"
        } finally {
            if (-not $selftest.HasExited) { $selftest.Kill(); $selftest.WaitForExit(10000) | Out-Null }
        }
        $log = Join-Path $stage 'selftest.log'
        if (Test-Path $log) { Write-Host (Get-Content $log -Raw) }
        # A windowed run as well: the self test renders without a screen, so it cannot catch a
        # missing qwindows plugin. The page hook paints a window and quits by itself.
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

        # The modpack page is the one page that starts a download thread, and a runtime that does not
        # agree with the compiler kills the process inside that thread's first few instructions, with
        # no dialog and no log. Opening the page is therefore part of the check, not an extra: the
        # version of this bug that reached players had every other check green.
        $packsShot = Join-Path $stage 'verify-modpacks.png'
        $savedDelay = $env:KOMOREBI_SHOT_DELAY
        $env:KOMOREBI_SHOT_DELAY = '4000'
        $process = Start-Process -FilePath (Join-Path $stage 'komorebi-lite.exe') -PassThru `
            -ArgumentList @('--page', 'modpacks', '--screenshot', $packsShot)
        try {
            if (-not $process.WaitForExit(120000)) { throw "the modpack page did not finish within two minutes" }
            if ($process.ExitCode -ne 0) {
                throw ("the modpack page brought the launcher down (exit code 0x{0:X8})" -f $process.ExitCode)
            }
            if (-not (Test-Path $packsShot) -or (Get-Item $packsShot).Length -lt 1024) { throw "the modpack page painted nothing" }
            Write-Host "modpack page: ok ($((Get-Item $packsShot).Length) bytes of pixels)"
        } finally {
            if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit(10000) | Out-Null }
            $env:KOMOREBI_SHOT_DELAY = $savedDelay
        }
    } finally {
        $env:PATH = $savedPath
        $env:KOMOREBI_DATA = $savedData
        $env:QT_PLUGIN_PATH = $savedPlugin
        $env:QT_QPA_PLATFORM = $null
        Remove-Item $data -Recurse -Force -ErrorAction SilentlyContinue
        Get-ChildItem $stage -Filter '*.log' | Remove-Item -Force -ErrorAction SilentlyContinue
        # The self test renders a few widgets to pictures next to the executable while it runs. They
        # are there to look at after a failure, not to travel inside the package.
        Get-ChildItem $stage -Filter 'selftest-*.png' | Remove-Item -Force -ErrorAction SilentlyContinue
        Remove-Item (Join-Path $stage 'verify.png') -Force -ErrorAction SilentlyContinue
        Remove-Item (Join-Path $stage 'verify-modpacks.png') -Force -ErrorAction SilentlyContinue
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
