cmake_minimum_required(VERSION 3.16)

project(adb_files_tui VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(adb-files-tui
    src/main.cpp
)
