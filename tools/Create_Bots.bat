@echo off
setlocal DisableDelayedExpansion
title CoA Playerbots - Create Bots

set "SCRIPT_DIR=%~dp0"
set "PY_EXE="

rem Prefer the repack's own bundled Python (same one Start_All_Server.bat etc.
rem use) if this file has been dropped into (or under) a CoA-Repack folder.
for %%D in ("%SCRIPT_DIR%." "%SCRIPT_DIR%.." "%SCRIPT_DIR%..\.." "%SCRIPT_DIR%..\..\..") do (
    if exist "%%~fD\Runtime\python\python.exe" set "PY_EXE=%%~fD\Runtime\python\python.exe"
)

if not defined PY_EXE (
    where python >nul 2>nul
    if not errorlevel 1 set "PY_EXE=python"
)

if not defined PY_EXE (
    echo Could not find a Python interpreter ^(looked for a CoA-Repack's Runtime\python
    echo folder near this file, then for "python" on PATH^). Install Python 3.8+ or
    echo move this file inside your CoA-Repack folder, then try again.
    echo.
    pause
    exit /b 1
)

"%PY_EXE%" "%SCRIPT_DIR%offline_bot_factory.py"
if errorlevel 1 echo The action failed. Read the message above.

echo.
pause
