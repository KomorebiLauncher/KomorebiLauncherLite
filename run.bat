@echo off
rem Runs the from-zero launcher. Qt's DLLs are not next to the binary yet, so the PATH is set here.
rem
rem   run.bat                                     open the window
rem   run.bat --selftest                          render the controls and check the pixels
rem   run.bat --launch-plan <version.json>        print the command line for a version
rem   run.bat --launch-instance "<instance id>"   start a real instance
rem
rem The launcher itself is a windowed program: it prints nothing. Everything it does goes to
rem build\launch.log (or build\selftest.log), and this script shows the tail of it for you.
setlocal
chcp 65001 >nul

set "HERE=%~dp0"
set "QT=D:\Qt\6.11.2\msvc2022_64"
set "EXE=%HERE%build\komorebi-next.exe"

if not exist "%EXE%" (
    echo Not built yet. Run build.bat first.
    exit /b 1
)

set "PATH=%QT%\bin;%PATH%"
set "QT_PLUGIN_PATH=%QT%\plugins"

"%EXE%" %*
set "CODE=%ERRORLEVEL%"

rem Windowed program: wait for it, then say what happened instead of returning silently.
echo.
echo exit code: %CODE%

if exist "%HERE%build\launch.log" (
    echo ---- build\launch.log ----
    type "%HERE%build\launch.log"
)
if exist "%HERE%build\selftest.log" (
    echo ---- build\selftest.log ----
    type "%HERE%build\selftest.log"
)

exit /b %CODE%
