@echo off
REM RALLY, joining a game somebody's hosting:   join.bat <the host's address>
REM   join.bat 192.168.1.20      a machine on the same network
REM   join.bat 127.0.0.1         a host on this same machine, to try it with two windows
if "%1"=="" (
  echo join.bat ^<the host's address^>     for one on this machine: join.bat 127.0.0.1
  exit /b 1
)
set PATH=C:\msys64\mingw64\bin;%PATH%
cd /d "%~dp0"
set REVA_WINDOWED=1
set REVA_NET=join:%1
start "" "..\..\crowd_rally_vk.exe" 0 packs
