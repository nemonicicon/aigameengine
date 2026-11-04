@echo off
rem Builds the solver's test and the Godot extension with Visual Studio's compiler, no other tools. The extension's static C runtime means the DLL needs nothing installed.
rem   build_windows.bat [out dir for the dll]
cd /d %~dp0
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
if not exist out mkdir out
cl /nologo /O2 /fp:fast /MT /W3 /D_CRT_SECURE_NO_WARNINGS test_clubcloth.c clubcloth.c /Fe:out\test_clubcloth.exe /Fo:out\ || exit /b 1
out\test_clubcloth.exe || exit /b 1
rem Does it move like cloth? The bench holds it to physics and the textile lab's numbers. A DLL that fails it isn't copied anywhere.
cl /nologo /O2 /fp:fast /MT /W3 /D_CRT_SECURE_NO_WARNINGS bench_clubcloth.c clubcloth.c /Fe:out\bench_clubcloth.exe /Fo:out\ || exit /b 1
out\bench_clubcloth.exe > out\bench.txt || (type out\bench.txt & exit /b 1)
findstr /c:"all passed" out\bench.txt
if not exist godot\club_cloth_gde.c exit /b 0
cl /nologo /O2 /fp:fast /MT /LD /W3 /D_CRT_SECURE_NO_WARNINGS /I. /Igodot godot\club_cloth_gde.c clubcloth.c /Fe:out\club_cloth.windows.x86_64.dll /Fo:out\ /link /NOLOGO || exit /b 1
if not "%~1"=="" copy /y out\club_cloth.windows.x86_64.dll "%~1" >nul
echo built out\club_cloth.windows.x86_64.dll
