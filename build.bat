@echo off
REM Builds g29pedalkeys.exe. Uses MSVC (cl) if it is on PATH, otherwise MinGW g++.
setlocal

where cl >nul 2>nul
if %errorlevel%==0 goto msvc

where g++ >nul 2>nul
if %errorlevel%==0 goto mingw

echo No compiler found.
echo Open a "x64 Native Tools Command Prompt for VS" and re-run, or install MinGW-w64.
exit /b 1

:msvc
echo Building with MSVC...
cl /nologo /EHsc /O2 /W3 /DWINVER=0x0601 /D_WIN32_WINNT=0x0601 ^
   src\main.cpp src\config.cpp src\pedals.cpp src\emitter.cpp ^
   /Fe:g29pedalkeys.exe /link /SUBSYSTEM:WINDOWS ^
   user32.lib gdi32.lib comctl32.lib hid.lib
if errorlevel 1 exit /b 1
del *.obj >nul 2>nul
echo Built g29pedalkeys.exe
exit /b 0

:mingw
echo Building with MinGW g++...
g++ -O2 -DWINVER=0x0601 -D_WIN32_WINNT=0x0601 ^
    src\main.cpp src\config.cpp src\pedals.cpp src\emitter.cpp ^
    -o g29pedalkeys.exe -mwindows -static -lcomctl32 -lhid -lgdi32 -luser32
if errorlevel 1 exit /b 1
echo Built g29pedalkeys.exe
exit /b 0
