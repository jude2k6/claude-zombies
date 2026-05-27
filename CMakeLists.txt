cmake_minimum_required(VERSION 3.16)
project(shooter C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)

# ---- raylib (bundles raymath, rlgl, rcamera, raudio) -----------------------
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.5
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(raylib)

# ---- raygui (header-only) --------------------------------------------------
FetchContent_Declare(
    raygui
    GIT_REPOSITORY https://github.com/raysan5/raygui.git
    GIT_TAG        4.0
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(raygui)

# ---- enet (networking) -----------------------------------------------------
FetchContent_Declare(
    enet
    GIT_REPOSITORY https://github.com/lsalzman/enet.git
    GIT_TAG        v1.3.18
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(enet)

# ---- executable ------------------------------------------------------------
add_executable(shooter
    src/main.c
    src/net.c
    src/level.c
    src/player.c
    src/weapons.c
    src/perks.c
    src/entities.c
    src/interact.c
    src/game.c
    src/protocol.c
    src/render.c
    src/hud.c
    src/menu.c
)
target_include_directories(shooter PRIVATE
    ${raygui_SOURCE_DIR}/src
    ${enet_SOURCE_DIR}/include
)
target_link_libraries(shooter PRIVATE raylib enet)

# ---- per-OS bits -----------------------------------------------------------
if(UNIX AND NOT APPLE)
    target_link_libraries(shooter PRIVATE m pthread dl)
endif()

if(APPLE)
    target_link_libraries(shooter PRIVATE
        "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
endif()

if(WIN32)
    target_link_libraries(shooter PRIVATE ws2_32 winmm iphlpapi)
    if(MSVC)
        target_compile_options(shooter PRIVATE /W3 /utf-8)
        target_compile_definitions(shooter PRIVATE _CRT_SECURE_NO_WARNINGS)
        target_compile_options(shooter PRIVATE /std:c11)
    endif()
    if(MINGW AND CMAKE_BUILD_TYPE STREQUAL "Release")
        target_link_options(shooter PRIVATE -static -static-libgcc)
    endif()
endif()
