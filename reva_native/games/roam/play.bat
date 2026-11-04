@echo off
REM The streaming world on the Vulkan renderer: ROAM. WASD or the stick moves. Leave it alone and it runs back and forth across the world. The log's [stream] lines say what the machine made of it.
REM Build it first: python .claude\skills\run-reva\driver.py buildvk --game roam. Its cells aren't in git: python games\roam\make_assets.py makes them.
set PATH=C:\msys64\mingw64\bin;%PATH%
cd /d "%~dp0"
set REVA_WINDOWED=1
start "" "..\..\crowd_roam_vk.exe" 0 packs packs/v1_tower.rpack
