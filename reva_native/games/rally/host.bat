@echo off
REM RALLY, hosting a game across machines: it waits for the others on a screen of the engine's own, and says this machine's address there.
REM   host.bat           two pads: this machine and one other
REM   host.bat 3         three pads (2 to 4)
REM The others run join.bat <this machine's address>. Port 7777 has to be reachable: the same network, or opened on the router.
REM The first time, Windows asks whether to let it through the firewall: yes, for private networks, or nobody can join.
set PATH=C:\msys64\mingw64\bin;%PATH%
cd /d "%~dp0"
set REVA_WINDOWED=1
set PADS=%1
if "%PADS%"=="" set PADS=2
set REVA_NET=host:7777:%PADS%
start "" "..\..\crowd_rally_vk.exe" 0 packs
