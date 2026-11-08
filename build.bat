@echo off
rem Builds the from-zero launcher on this machine: Qt only, no vcpkg, no other dependencies.
rem Run it from anywhere; it works out its own directory first.
setlocal
chcp 65001 >nul

set "HERE=%~dp0"
set "QT=D:\Qt\6.11.2\msvc2022_64"
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

cmake -S "%HERE%." -B "%HERE%build" -G Ninja -DCMAKE_PREFIX_PATH=%QT% -DCMAKE_BUILD_TYPE=Debug
if errorlevel 1 exit /b 1

rem A clean build on purpose: an object compiled against an older copy of a struct keeps working
rem just well enough to crash at startup, which looks like a logic bug and costs an hour every time.
cmake --build "%HERE%build" --clean-first
if errorlevel 1 exit /b 1

echo.
echo Built: %HERE%build\komorebi-lite.exe
