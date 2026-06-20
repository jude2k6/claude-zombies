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
    src/engine/deffile.c
    src/engine/pad.c
    src/engine/fx.c
    src/engine/decals.c
    src/engine/particles.c
    src/engine/anim.c
    src/engine/gfx.c
    src/engine/app.c
    src/engine/content.c
    src/engine/pack.c
    src/engine/eng_render.c
    src/engine/pick.c
    src/engine/debugdraw.c
    src/engine/stats.c
    src/engine/collide.c
    src/engine/gizmo.c
    src/engine/mapedit.c
    src/engine/ui.c
    src/engine/cfg.c
)
target_include_directories(engine PUBLIC
    ${CMAKE_SOURCE_DIR}/src/engine
    ${raygui_SOURCE_DIR}/src
    ${enet_SOURCE_DIR}/include
)
target_link_libraries(engine PUBLIC raylib enet)

# ---- AddressSanitizer / UBSan (opt-in) -------------------------------------
# Declared here, BEFORE the game subdir, so the game's own CMakeLists can consume
# it. The engine half is applied below; the game half lives in its CMakeLists.
option(SHOOTER_ASAN "Build with AddressSanitizer/UBSan" OFF)

# ---- the reference game (games-as-projects GameModule) ----------------------
# The game's logic + data live together in games/shooter/, and games/shooter/src/
# has its OWN CMakeLists that defines the `shooter` target (linking the `engine`
# target above). The repo root no longer lists the game's source files — it just
# pulls in the game project. See docs/game-projects.md.
add_subdirectory(games/shooter/src)

# ---- editor executable (src/editor/ — the scene/map builder) ----------------
# A SIBLING application to the game: its own GameModule on the engine, linking
# libengine.a and NOTHING from the game (games/shooter/src/). The map editor is an IDE-style shell
# (edhost) hosting a scene/viewport (edscene) filled by first-party plugins
# (builtins). See docs/scene-builder.md.
#
# ENABLE_EXPORTS gives the editor binary a dynamic symbol table (-rdynamic), so
# dlopen'd plugins in ./plugins resolve the EdHost_*/Eng_* symbols they call.
add_executable(editor
    src/editor/editor_main.c
    src/editor/edscene.c
    src/editor/edsettings.c
    src/editor/edcatalog.c
    src/editor/edplace.c
    src/editor/edmat.c
    src/editor/edhost.c
    src/editor/ed_recents.c
    src/editor/edmenus.c
    src/editor/panel_inspector.c
    src/editor/edpanels.c
    src/editor/edassets.c
    src/editor/edthumb.c
    src/editor/edfiledialog.c
    src/editor/edproject.c
    src/editor/edimport.c
    src/editor/edlauncher.c
    src/editor/vendor/tinyfiledialogs.c
)
target_include_directories(editor PRIVATE src/editor/vendor)
target_link_libraries(editor PRIVATE engine ${CMAKE_DL_LIBS})
set_target_properties(editor PROPERTIES ENABLE_EXPORTS ON)
# Suppress warnings for the vendored tinyfiledialogs; our own files are wall-clean.
set_source_files_properties(src/editor/vendor/tinyfiledialogs.c PROPERTIES
    COMPILE_FLAGS "-w")
if(NOT MSVC)
    target_compile_options(editor PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# ---- example dynamic plugin (proves the .so plugin seam) --------------------
# Builds build/plugins/example_plugin.so. A third-party plugin is written just
# like this: implement ed_plugin_main() returning an EdPluginDesc, link nothing
# (host symbols resolve at dlopen via the editor's exported table), drop the .so
# in the ./plugins folder beside the working directory.
add_library(editor_example_plugin MODULE
    src/editor/plugins/example_plugin.c
)
target_include_directories(editor_example_plugin PRIVATE
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/engine
    ${raylib_SOURCE_DIR}/src
    ${raygui_SOURCE_DIR}/src
)
set_target_properties(editor_example_plugin PROPERTIES
    PREFIX "" OUTPUT_NAME "example_plugin"
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins)
if(NOT MSVC)
    target_compile_options(editor_example_plugin PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# ---- warning flags (engine only here; the game + editor set their own) ------
if(NOT MSVC)
    target_compile_options(engine PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# ---- AddressSanitizer / UBSan — engine half (game half is in its CMakeLists) -
if(SHOOTER_ASAN AND NOT MSVC)
    target_compile_options(engine PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
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

# Content staging next to the binary + the game's per-OS link bits now live with
# the game target in games/shooter/src/CMakeLists.txt (the shooter target is
# defined there). The engine + editor remain configured above.
