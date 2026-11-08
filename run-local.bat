@echo off
rem Opens the launcher with its data directory inside this project instead of %APPDATA%.
rem
rem On this machine a launcher built here is not allowed to write under %APPDATA% (the same
rem restriction that stops downloads and logs there), so the data directory is kept beside the build.
rem The versions, libraries, assets and downloaded Java are already in build\data.
setlocal
set "HERE=%~dp0"
set "KOMOREBI_DATA=%HERE%build\data"
call "%HERE%run.bat" %*
