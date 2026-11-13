@echo off
REM A character creator on the Vulkan renderer: PEOPLE. Stick up and down picks a row, left and right changes it, and B says something.
REM Make its assets and build it first: python games\people\make_assets.py, then python .claude\skills\run-reva\driver.py buildvk --game people.
set PATH=C:\msys64\mingw64\bin;%PATH%
cd /d "%~dp0"
set REVA_WINDOWED=1
start "" "..\..\crowd_people_vk.exe" 8 packs packs/v1_boot.rpack
