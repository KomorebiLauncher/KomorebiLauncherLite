@echo off
rem Opens the launcher with its data directory inside build\data instead of next to the executable.
rem
rem A launcher built here keeps its data beside the exe, which for a development build is build\ -- the
rem same folder the objects and the deployed Qt land in. This points it at build\data instead, so the
rem versions, libraries, assets and downloaded Java stay in one place of their own.
setlocal
set "HERE=%~dp0"
set "KOMOREBI_DATA=%HERE%build\data"
call "%HERE%run.bat" %*
