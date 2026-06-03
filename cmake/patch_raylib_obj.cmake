# Harden raylib 5.5's OBJ loader against the two ways our UV-less / n-gon OBJs
# break it. Runs as a FetchContent PATCH_COMMAND with the raylib source dir as
# CWD. Idempotent: each step detects its own marker and no-ops on re-run.
#
# Bug 1 — tinyobj face buffer overflow (the crash).
#   parseLine() reads every vertex of an `f` line into a fixed stack array
#   f[TINYOBJ_MAX_FACES_PER_F_LINE] (= 16) with NO bounds check, *before*
#   triangulating. Several of our weapon OBJs have cylinder-cap n-gons with
#   20–24 verts (Blender export without triangulation), so reading them smashes
#   the stack. It's layout-dependent UB — silent until something (e.g. one extra
#   parsed .weapon line) shifts memory, then it's a hard SIGSEGV at model load.
#   Fix: raise the limit so fat n-gons are read in full, then triangulated.
#
# Bug 2 — LoadOBJ texcoord/normal read for UV-less OBJs.
#   LoadOBJ reads objAttributes.texcoords[vt_idx*2] unconditionally; with no
#   `vt` data tinyobj leaves vt_idx = 0x80000000, so the read is wildly out of
#   bounds (usually silent UB; all our OBJs are UV-less). Same for normals.
#   Fix: guard both reads, zero-fill when the data is absent.

if(NOT DEFINED RMODELS)
    set(RMODELS "src/rmodels.c")
endif()
if(NOT DEFINED TINYOBJ)
    set(TINYOBJ "src/external/tinyobj_loader_c.h")
endif()

# ---- Bug 1: raise tinyobj's per-face vertex limit ---------------------------
if(EXISTS "${TINYOBJ}")
    file(READ "${TINYOBJ}" _t)
    string(REPLACE "\r\n" "\n" _t "${_t}")
    if(_t MATCHES "#define TINYOBJ_MAX_FACES_PER_F_LINE \\(256\\)")
        message(STATUS "tinyobj face limit already raised")
    else()
        string(REPLACE
            "#define TINYOBJ_MAX_FACES_PER_F_LINE (16)"
            "#define TINYOBJ_MAX_FACES_PER_F_LINE (256)"
            _tp "${_t}")
        if(_tp STREQUAL _t)
            message(FATAL_ERROR "patch_raylib_obj: TINYOBJ_MAX_FACES_PER_F_LINE not found in ${TINYOBJ}")
        endif()
        file(WRITE "${TINYOBJ}" "${_tp}")
        message(STATUS "Raised tinyobj TINYOBJ_MAX_FACES_PER_F_LINE 16 -> 256")
    endif()
else()
    message(FATAL_ERROR "patch_raylib_obj: cannot find ${TINYOBJ}")
endif()

# ---- Bug 2: guard LoadOBJ's texcoord/normal reads ---------------------------
if(NOT EXISTS "${RMODELS}")
    message(FATAL_ERROR "patch_raylib_obj: cannot find ${RMODELS}")
endif()

file(READ "${RMODELS}" _src)
string(REPLACE "\r\n" "\n" _src "${_src}")

if(_src MATCHES "num_texcoords > 0 && texcordIndex")
    message(STATUS "raylib LoadOBJ already patched (no-UV guard present)")
    return()
endif()

set(_old "            for (int i = 0; i < 3; i++)
                model.meshes[meshIndex].normals[localMeshVertexCount * 3 + i] = objAttributes.normals[normalIndex * 3 + i];

            for (int i = 0; i < 2; i++)
                model.meshes[meshIndex].texcoords[localMeshVertexCount * 2 + i] = objAttributes.texcoords[texcordIndex * 2 + i];

            model.meshes[meshIndex].texcoords[localMeshVertexCount * 2 + 1] = 1.0f - model.meshes[meshIndex].texcoords[localMeshVertexCount * 2 + 1];")

set(_new "            if (objAttributes.num_normals > 0 && normalIndex >= 0)
                for (int i = 0; i < 3; i++)
                    model.meshes[meshIndex].normals[localMeshVertexCount * 3 + i] = objAttributes.normals[normalIndex * 3 + i];
            else
                for (int i = 0; i < 3; i++)
                    model.meshes[meshIndex].normals[localMeshVertexCount * 3 + i] = 0.0f;

            if (objAttributes.num_texcoords > 0 && texcordIndex >= 0)
            {
                for (int i = 0; i < 2; i++)
                    model.meshes[meshIndex].texcoords[localMeshVertexCount * 2 + i] = objAttributes.texcoords[texcordIndex * 2 + i];

                model.meshes[meshIndex].texcoords[localMeshVertexCount * 2 + 1] = 1.0f - model.meshes[meshIndex].texcoords[localMeshVertexCount * 2 + 1];
            }
            else
            {
                model.meshes[meshIndex].texcoords[localMeshVertexCount * 2 + 0] = 0.0f;
                model.meshes[meshIndex].texcoords[localMeshVertexCount * 2 + 1] = 0.0f;
            }")

string(REPLACE "${_old}" "${_new}" _patched "${_src}")

if(_patched STREQUAL _src)
    message(FATAL_ERROR
        "patch_raylib_obj: target block not found in ${RMODELS} — raylib "
        "source changed? Update cmake/patch_raylib_obj.cmake.")
endif()

file(WRITE "${RMODELS}" "${_patched}")
message(STATUS "Patched raylib LoadOBJ (no-UV texcoord/normal guard)")
