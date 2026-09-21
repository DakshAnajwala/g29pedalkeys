#!/bin/sh
# Cross-compiles the Windows exe from macOS/Linux. Needs mingw-w64.
# On Windows itself, use build.bat instead.
set -e
x86_64-w64-mingw32-g++ -O2 -Wall -DWINVER=0x0601 -D_WIN32_WINNT=0x0601 \
    src/main.cpp src/config.cpp src/pedals.cpp src/emitter.cpp \
    -o g29pedalkeys.exe -mwindows -static -lcomctl32 -lhid -lgdi32 -luser32
echo "Built g29pedalkeys.exe"
