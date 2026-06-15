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
    # Guard LoadOBJ's texcoord/normal reads for UV-less OBJs (all of ours).
    # Without this, model load is heap-layout-dependent UB that crashes when
    # e.g. a .weapon file gains a line. See cmake/patch_raylib_obj.cmake.
    PATCH_COMMAND  ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_raylib_obj.cmake
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

# ---- engine static library (src/engine/ only — ZERO game sources) ----------
# libengine.a is the reusable runtime: window/loop, render GL facade, audio
# mixer, animation, particles/decals, input, net transport, map document.
# The §2 cardinal rule (no game header in engine) is enforced by seam-check;
# this add_library is the link-time half — it physically contains no game .c.
add_library(engine STATIC
    src/engine/audio.c
    src/engine/net.c
    src/engine/mapdoc.c
    src/engine/pad.c
    src/engine/fx.c
    src/engine/decals.c
    src/engine/particles.c
    src/engine/anim.c
    src/engine/gfx.c
    src/engine/app.c
    src/engine/content.c
    src/engine/eng_render.c
)
target_include_directories(engine PUBLIC
    ${CMAKE_SOURCE_DIR}/src/engine
    ${raygui_SOURCE_DIR}/src
    ${enet_SOURCE_DIR}/include
)
target_link_libraries(engine PUBLIC raylib enet)

# ---- game executable (src/game/ — rules + content, links the engine) -------
add_executable(shooter
    src/game/main.c
    src/game/world.c
    src/game/level.c
    src/game/player.c
    src/game/weapons.c
    src/game/perks.c
    src/game/entities.c
    src/game/interact.c
    src/game/game.c
    src/game/protocol.c
    src/game/render.c
    src/game/viewmodel.c
    src/game/devtools.c
    src/game/hud.c
    src/game/menu.c
    src/game/audio_director.c
    src/game/settings.c
    src/game/assets.c
)
target_include_directories(shooter PRIVATE
    ${CMAKE_SOURCE_DIR}/src/game
)
target_link_libraries(shooter PRIVATE engine)

# ---- warning flags (our code only, not FetchContent deps) ------------------
if(NOT MSVC)
    target_compile_options(engine  PRIVATE -Wall -Wextra -Wno-unused-parameter)
    target_compile_options(shooter PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# ---- AddressSanitizer / UBSan (opt-in) -------------------------------------
option(SHOOTER_ASAN "Build with AddressSanitizer/UBSan" OFF)
if(SHOOTER_ASAN AND NOT MSVC)
    target_compile_options(engine  PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_compile_options(shooter PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(shooter PRIVATE -fsanitize=address,undefined)
endif()

# ---- engine/game seam check ------------------------------------------------
# Verifies that no src/engine/ file includes a game header (§2 cardinal rule).
# The engine library depends on it, so a normal cmake --build fails immediately
# on a seam violation before any engine C is compiled.
add_custom_target(seam-check
    COMMAND ${CMAKE_COMMAND} -E echo "-- Running seam check..."
    COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-seam.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Checking engine/game seam (cardinal rule: no game headers in src/engine/)"
)
add_dependencies(engine seam-check)

# ---- data files ------------------------------------------------------------
# Copy data/ next to the executable at build time so map files load from a
# relative path. Re-runs whenever something under data/ changes.
add_custom_command(TARGET shooter POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/data $<TARGET_FILE_DIR:shooter>/data
    COMMENT "Copying data/ next to shooter binary"
)

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
