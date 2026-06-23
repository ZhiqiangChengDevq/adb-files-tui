cmake_minimum_required(VERSION 3.16)

project(adb_files_tui VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ADB_FILES_TUI_STATIC_FTXUI "Build and link FTXUI from source as static libraries" ON)

if(ADB_FILES_TUI_STATIC_FTXUI)
    include(FetchContent)

    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(ftxui
        GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
        GIT_TAG v7.0.0
    )
    FetchContent_MakeAvailable(ftxui)
else()
    find_package(ftxui CONFIG REQUIRED)
endif()

add_executable(adb-files-tui
    src/adb_client.cpp
    src/app.cpp
    src/main.cpp
)

target_link_libraries(adb-files-tui
    PRIVATE
        ftxui::component
)

target_compile_definitions(adb-files-tui
    PRIVATE
        ADB_FILES_TUI_VERSION="${PROJECT_VERSION}"
)
