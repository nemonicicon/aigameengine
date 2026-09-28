@echo off
REM ---- THE CLUB. Boots into GAME MODE select (ten doors: dance, skate, battle royale,
REM arena, rts, scooter, arpg, platformer, narrative rpg, arcade), then CHARACTER select
REM (chars.txt roster). Set REVA_MODE=<index|name> to skip the mode menu; put a pack
REM name in place of "select" to skip the character menu.
set DANCERS=400
set PATH=C:\msys64\mingw64\bin;%PATH%
cd /d "%~dp0"
taskkill /IM crowd.exe /F >nul 2>&1
start "" "%~dp0crowd.exe" %DANCERS% packs select
