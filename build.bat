@echo off
rem Builds the from-zero launcher on this machine: Qt only, no vcpkg, no other dependencies.
rem Run it from anywhere; it works out its own directory first.
setlocal
chcp 65001 >nul

set "HERE=%~dp0"
rem Lite builds against its own Qt, installed for it alone. The full edition has its own; neither
rem reads the other's, and nothing is installed at a drive root to keep them apart.
set "QT=%LOCALAPPDATA%\KomorebiLauncherLite\Qt\6.10.3\msvc2022_64"
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

if not exist "%VCVARS%" (
    echo Cannot find vcvars64.bat at "%VCVARS%".
    exit /b 1
)
if not exist "%QT%\bin\qmake.exe" (
    echo Cannot find Qt at "%QT%".
    exit /b 1
)

call "%VCVARS%" >nul

rem English diagnostics: Ninja recognises MSVC's include lines by their English prefix, and a
rem localized compiler silently breaks header dependency tracking (CMakeLists.txt sets it too).
set "VSLANG=1033"

cmake -S "%HERE%." -B "%HERE%build" -G Ninja -DCMAKE_PREFIX_PATH=%QT% -DCMAKE_BUILD_TYPE=Debug
if errorlevel 1 exit /b 1

rem A clean build on purpose: with dependency tracking working the --clean-first is belt and braces,
rem and it costs a minute rather than an evening of chasing a crash that is really a stale object.
cmake --build "%HERE%build" --clean-first
if errorlevel 1 exit /b 1

echo.
echo Built: %HERE%build\komorebi-lite.exe
