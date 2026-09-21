cmake_minimum_required(VERSION 3.15)
project(g29pedalkeys CXX)

add_executable(g29pedalkeys WIN32
    src/main.cpp
    src/config.cpp
    src/pedals.cpp
    src/emitter.cpp
)

target_compile_definitions(g29pedalkeys PRIVATE WINVER=0x0601 _WIN32_WINNT=0x0601)
target_link_libraries(g29pedalkeys PRIVATE user32 gdi32 comctl32 hid)
