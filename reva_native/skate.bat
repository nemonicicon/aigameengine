@echo off
REM ---- Skate bench. Edit these, save, double-click. No rebuild needed. ----
REM CHARACTER: "select" = character select screen (chars.txt roster);
REM or name a pack outright: v1_reva_skate.rpack (mocap) / v1_kickflip.rpack (baked clips)
set CHAR=select
set SCALE=1.0
set YAW=180

set REVA_SKATE=1
set REVA_SKSCALE=%SCALE%
set REVA_SKYAW=%YAW%
set REVA_TRICKSPD=2.6
set REVA_FLIPAXIS=2
set REVA_HOLO=0
set REVA_AUTOTRICK=0
set REVA_ONFOOT=0
set REVA_DISPLAY=1

REM MinGW runtime DLLs -- a double-clicked shortcut has no PATH for them
set PATH=C:\msys64\mingw64\bin;%PATH%

cd /d "%~dp0"
taskkill /IM crowd.exe /F >nul 2>&1
start "" "%~dp0crowd.exe" 1 packs %CHAR%
