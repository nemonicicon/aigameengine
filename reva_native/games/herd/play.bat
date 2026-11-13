@echo off
REM The crowd game on the Vulkan renderer: HERD. WASD or the stick moves the cursor, hold SPACE or A to gather, right stick turns the lens, E or B plays again. Leave it alone and it plays itself. The 400 is how many bodies: try more.
REM Build it first: python .claude\skills\run-reva\driver.py buildvk --game herd.
set PATH=C:\msys64\mingw64\bin;%PATH%
cd /d "%~dp0"
set REVA_WINDOWED=1
start "" "..\..\crowd_herd_vk.exe" 400 packs packs/v1_herd.rpack
