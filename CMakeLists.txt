cmake_minimum_required(VERSION 3.16)

project(adb_files_tui VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ftxui CONFIG REQUIRED)

add_executable(adb-files-tui
    src/adb_client.cpp
    src/app.cpp
    src/main.cpp
)

target_link_libraries(adb-files-tui
    PRIVATE
        ftxui::ftxui
)

target_compile_definitions(adb-files-tui
    PRIVATE
        ADB_FILES_TUI_VERSION="${PROJECT_VERSION}"
)
