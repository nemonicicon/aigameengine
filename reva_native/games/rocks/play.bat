@echo off
REM The 2D game on the Vulkan renderer: ROCKS. A and D or the stick to move, SPACE or A shoots, E or B plays again. Leave it alone and it plays itself.
REM Build it first: python .claude\skills\run-reva\driver.py buildvk --game rocks.
set PATH=C:\msys64\mingw64\bin;%PATH%
cd /d "%~dp0"
set REVA_WINDOWED=1
start "" "..\..\crowd_rocks_vk.exe" 0 packs
