@echo off
REM The sample game on the Vulkan renderer: TAG. WASD or the stick to run, SPACE or A sprints, E or B plays again. Leave it alone and it plays itself.
REM Build it first: python .claude\skills\run-reva\driver.py buildvk --sample (in the kit, plain buildvk).
set PATH=C:\msys64\mingw64\bin;%PATH%
cd /d "%~dp0"
set EXE=crowd_sample_vk.exe
if not exist "..\..\%EXE%" set EXE=crowd_vk.exe
set REVA_WINDOWED=1
start "" "..\..\%EXE%" 12 packs packs/v1_sample.rpack
