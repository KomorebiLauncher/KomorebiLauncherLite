@echo off
rem Runs the from-zero launcher. Qt's DLLs are not next to the binary yet, so the PATH is set here.
rem   run.bat                                     open the window
rem   run.bat --selftest                          render the controls and check the pixels
rem   run.bat --launch-plan <version.json>        print the command line for a version
rem   run.bat --launch-instance "<instance id>"   start a real instance
setlocal
chcp 65001 >nul

set "HERE=%~dp0"
set "QT=D:\Qt\6.11.2\msvc2022_64"

if not exist "%HERE%build\komorebi-next.exe" (
    echo Not built yet. Run build.bat first.
    exit /b 1
)

set "PATH=%QT%\bin;%PATH%"
set "QT_PLUGIN_PATH=%QT%\plugins"

"%HERE%build\komorebi-next.exe" %*
