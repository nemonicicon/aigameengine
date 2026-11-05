@echo off
REM RALLY: bats and a ball, for one to four pads on this machine. Stick or W and S moves, SPACE or A starts, Q or X is the online menu, E or B goes back.
REM Leave it alone and it plays itself. Build it first: python .claude\skills\run-reva\driver.py buildvk --game rally.
REM Across machines: host.bat on one, join.bat <its address> on the others. Or pick HOST A GAME or JOIN on the game's own X menu.
set PATH=C:\msys64\mingw64\bin;%PATH%
cd /d "%~dp0"
set REVA_WINDOWED=1
start "" "..\..\crowd_rally_vk.exe" 0 packs
