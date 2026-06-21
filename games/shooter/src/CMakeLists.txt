# ============================================================================
#  games/shooter/src/CMakeLists.txt — the reference game (Claude Zombies).
#
#  A games-as-projects GameModule: the game's own logic, built as the `shooter`
#  executable linking the engine static lib. This file owns everything game-
#  specific (sources, warnings, ASAN, content staging, per-OS link bits) so the
#  game is self-contained; the repo-root CMakeLists just add_subdirectory's it
#  after defining the `engine` target. See docs/game-projects.md.
#
#  Built IN-TREE against the repo's `engine` target. (An out-of-tree game would
#  instead find a prebuilt libengine.a via find_library — see the scaffold at
#  stdlib/templates/empty/src/CMakeLists.txt.)
# ============================================================================

if(NOT TARGET engine)
    message(FATAL_ERROR
        "games/shooter/src must be built from the repo root (the `engine` target "
        "is defined there). Configure with: cmake -S . -B build")
endif()

add_executable(shooter
    main.c
    world.c
    level.c
    player.c
    weapons.c
    mobs.c
    mob_ai.c
    perks.c
    entities.c
    interact.c
    game.c
    protocol.c
    render.c
    viewmodel.c
    devtools.c
    hud.c
    menu.c
    audio_director.c
    settings.c
    assets.c
    props.c
)
target_include_directories(shooter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# The game is an ENGINE PLUGIN HOST: a dynamic plugin (e.g. plugins/voice.so) may
# call ANY Eng_* function, which must therefore be present + exported in this
# binary. A normal static link drops engine objects the game itself never
# references (e.g. audio_capture/audio_voice for voice chat), so whole-archive the
# engine to retain the full engine API for plugins. (ENABLE_EXPORTS below then
# -rdynamic-exports them for dlopen.) Transitive deps (raylib/enet) still flow
# normally. See docs/voice-chat.md + docs/engine-usage.md §3 `plugin`.
target_link_libraries(shooter PRIVATE
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,engine>" ${CMAKE_DL_LIBS})

# Keep the binary at the build-tree root (build/shooter) regardless of this
# subdir's location, so `./build/shooter` and the CI/run scripts are unchanged.
set_target_properties(shooter PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    # -rdynamic so Tier-2 .so mob behaviours (dlopen'd from ./behaviours) resolve
    # the host's Game_RegisterBehaviour / world symbols — same seam as the editor.
    ENABLE_EXPORTS ON)

if(NOT MSVC)
    target_compile_options(shooter PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# AddressSanitizer/UBSan — game half (SHOOTER_ASAN option declared by repo root).
if(SHOOTER_ASAN AND NOT MSVC)
    target_compile_options(shooter PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(shooter PRIVATE -fsanitize=address,undefined)
endif()

# ---- content staging (overlay roots next to the binary) --------------------
# Copy the content trees next to the executable so the overlay resolves from a
# path relative to the binary (docs/game-projects.md, docs/content-packs.md):
#   stdlib/ — read-only universal base (runtime fallback, game-over-stdlib)
#   packs/  — importable pack SOURCES (not runtime-loaded; the editor imports them)
#   games/  — per-game folders (writable; game content wins)
# Runs after the shooter target relinks (a content-only edit needs a touched source).
add_custom_command(TARGET shooter POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/stdlib $<TARGET_FILE_DIR:shooter>/stdlib
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/packs $<TARGET_FILE_DIR:shooter>/packs
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/games $<TARGET_FILE_DIR:shooter>/games
    COMMENT "Copying stdlib/ + packs/ + games/ next to shooter binary"
)

# ---- per-OS link bits ------------------------------------------------------
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
