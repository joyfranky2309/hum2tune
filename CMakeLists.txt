
cmake_minimum_required(VERSION 3.20)

project(Hum2Tune
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(hum2tune
    src/main.cpp
    src/pitch_detector.cpp
    src/yin_pitch_detector.cpp
    src/wav_reader.cpp
)

target_compile_options(hum2tune PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)
