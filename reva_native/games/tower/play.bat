@echo off
REM The 3D action game on the Vulkan renderer: TOWER. WASD or the stick moves, right stick turns the lens, SPACE or A jumps, Q or X punches, E or B plays again. Leave it alone and it plays itself.
REM Build it first: python .claude\skills\run-reva\driver.py buildvk --game tower.
set PATH=C:\msys64\mingw64\bin;%PATH%
cd /d "%~dp0"
set REVA_WINDOWED=1
start "" "..\..\crowd_tower_vk.exe" 10 packs packs/v1_tower.rpack
