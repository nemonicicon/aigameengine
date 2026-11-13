@echo off
REM Two people and a conversation on the Vulkan renderer: TALK. WASD or the stick walks, A starts talking when you're close, and A cuts a line short. A menu's the stick and A.
REM Make its assets and build it first: python games\talk\make_assets.py, then python .claude\skills\run-reva\driver.py buildvk --game talk.
set PATH=C:\msys64\mingw64\bin;%PATH%
cd /d "%~dp0"
set REVA_WINDOWED=1
start "" "..\..\crowd_talk_vk.exe" 4 packs packs/v1_talk.rpack
