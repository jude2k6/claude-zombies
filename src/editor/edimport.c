// ============================================================================
//  edimport.c — copy-on-import core (docs/content-packs.md §5). See edimport.h.
// ============================================================================

#include "edimport.h"

#include "deffile.h"   // Eng_DefForEachLine
#include "raylib.h"    // FS: LoadFileData/SaveFileData, LoadDirectoryFilesEx, GetFileName, ...

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>  // mkdir (POSIX)
#ifdef _WIN32
#  include <direct.h>  // _mkdir (Win32)
#endif

// Portable single-component mkdir (not recursive; "already exists" is fine).
static int MkDir(const char *path) {
#ifdef _WIN32
    return _mkdir(path);
#else
    return mkdir(path, 0755);
#endif
}

// Binary-safe single-file copy. Returns 1 if a file was written, else 0. Uses
// LoadFileData/SaveFileData (NOT LoadFileText, which truncates at the first NUL
// and so corrupts .glb/.png). Overwrites an existing destination.
static int CopyFileBinary(const char *src, const char *dst) {
    if (!FileExists(src)) return 0;
    int bytes = 0;
    unsigned char *data = LoadFileData(src, &bytes);
    if (!data) return 0;
    bool ok = SaveFileData(dst, data, bytes);
    UnloadFileData(data);
    return ok ? 1 : 0;
}

// Map a def-file extension (".mob"/".perk"/".weapon"/".prop") to its content
// subdir ("mobs"/...). Returns NULL for an unrecognised extension.
static const char *TypeSubdirForExt(const char *ext) {
    if (!ext) return NULL;
    if (strcmp(ext, ".mob")    == 0) return "mobs";
    if (strcmp(ext, ".perk")   == 0) return "perks";
    if (strcmp(ext, ".weapon") == 0) return "weapons";
    if (strcmp(ext, ".prop")   == 0) return "props";
    return NULL;
}

// ---- model / texture dependency copying ------------------------------------

// deffile callback to pull the bare `model` value out of a def file.
static void ModelKeyCb(int lineNo, int n, char **toks, void *user) {
    (void)lineNo;
    if (n >= 2 && strcmp(toks[0], "model") == 0)
        snprintf((char *)user, 64, "%s", toks[1]);
}

// State for the .mtl texture walk.
typedef struct { const char *texSrcDir; const char *texDstDir; int copied; } MtlCtx;

// deffile callback over an .mtl: copy every map_Kd-referenced texture that lives
// in texSrcDir into texDstDir.
static void MtlTextureCb(int lineNo, int n, char **toks, void *user) {
    (void)lineNo;
    if (n < 2 || strcmp(toks[0], "map_Kd") != 0) return;
    MtlCtx *c = (MtlCtx *)user;
    const char *texLeaf = GetFileName(toks[1]);   // ignore any path prefix in the .mtl
    if (!texLeaf || !texLeaf[0]) return;
    char src[768], dst[768];
    snprintf(src, sizeof src, "%s/%s", c->texSrcDir, texLeaf);
    snprintf(dst, sizeof dst, "%s/%s", c->texDstDir, texLeaf);
    c->copied += CopyFileBinary(src, dst);
}

// Copy a referenced model file (in modelSrcDir → modelDstDir), its sibling .mtl
// (same basename), and any textures that .mtl references (texSrcDir → texDstDir).
// `modelFile` is a bare filename ("zombie.glb"). The caller picks src/dst dirs to
// MIRROR the source layout — beside the def (weapons) or the shared models/ tier
// (mobs/perks/props) — because the game resolves the model the same way.
// Returns the number of files copied.
static int CopyModelDeps(const char *modelSrcDir, const char *modelDstDir,
                         const char *texSrcDir, const char *texDstDir,
                         const char *modelFile) {
    if (!modelFile || !modelFile[0]) return 0;
    int copied = 0;

    MkDir(modelDstDir);

    // The model itself.
    char src[768], dst[768];
    snprintf(src, sizeof src, "%s/%s", modelSrcDir, modelFile);
    snprintf(dst, sizeof dst, "%s/%s", modelDstDir, modelFile);
    copied += CopyFileBinary(src, dst);

    // Sibling .mtl (same basename) — present for our .obj models; harmless if a
    // .glb happens to ship one too.
    char mtlName[128];
    snprintf(mtlName, sizeof mtlName, "%s.mtl", GetFileNameWithoutExt(modelFile));
    char mtlSrc[768], mtlDst[768];
    snprintf(mtlSrc, sizeof mtlSrc, "%s/%s", modelSrcDir, mtlName);
    snprintf(mtlDst, sizeof mtlDst, "%s/%s", modelDstDir, mtlName);
    if (FileExists(mtlSrc)) {
        copied += CopyFileBinary(mtlSrc, mtlDst);
        // Chase map_Kd textures referenced by the .mtl.
        char *mtlText = LoadFileText(mtlSrc);
        if (mtlText) {
            MkDir(texDstDir);
            MtlCtx ctx = { .texSrcDir = texSrcDir, .texDstDir = texDstDir, .copied = 0 };
            Eng_DefForEachLine(mtlText, MtlTextureCb, &ctx);
            UnloadFileText(mtlText);
            copied += ctx.copied;
        }
    }
    return copied;
}

// ---------------------------------------------------------------------------
//  EdImport_Item
// ---------------------------------------------------------------------------

int EdImport_Item(const char *srcDefPath, const char *srcRoot, const char *gameDir) {
    if (!srcDefPath || !srcRoot || !gameDir) return 0;
    if (!FileExists(srcDefPath)) return 0;

    const char *ext     = GetFileExtension(srcDefPath);   // includes the dot
    const char *typeDir = TypeSubdirForExt(ext);
    if (!typeDir) return 0;

    // The item's <name> subdir = the leaf of the def file's parent directory
    // (e.g. ".../mobs/zombie/zombie.mob" → "zombie").
    char parent[512];
    snprintf(parent, sizeof parent, "%s", GetDirectoryPath(srcDefPath));
    const char *name = GetFileName(parent);
    if (!name || !name[0]) return 0;

    // Create <gameDir>/<type>/<name>/ (parents first; mkdir is single-level).
    char typePath[768], itemPath[768];
    MkDir(gameDir);   // robust if the target game folder doesn't exist yet
    snprintf(typePath, sizeof typePath, "%s/%s", gameDir, typeDir);
    MkDir(typePath);
    snprintf(itemPath, sizeof itemPath, "%s/%s/%s", gameDir, typeDir, name);
    MkDir(itemPath);

    // Copy the def file itself.
    char defDst[1024];
    snprintf(defDst, sizeof defDst, "%s/%s", itemPath, GetFileName(srcDefPath));
    int copied = CopyFileBinary(srcDefPath, defDst);
    if (copied == 0) return 0;   // couldn't even copy the def → failure

    // Copy the referenced model + its deps. Two source conventions exist: a
    // weapon names its model relative to its OWN dir (raygun.obj beside
    // raygun.weapon); a mob/perk/prop names a file in the shared models/ tier.
    // Mirror whichever layout actually holds the file so the game resolves the
    // copy the same way it resolved the original.
    char modelFile[64] = "";
    char *defText = LoadFileText(srcDefPath);
    if (defText) {
        Eng_DefForEachLine(defText, ModelKeyCb, modelFile);
        UnloadFileText(defText);
    }
    if (modelFile[0]) {
        char besideSrc[1024];
        snprintf(besideSrc, sizeof besideSrc, "%s/%s", parent, modelFile);
        if (FileExists(besideSrc)) {
            // Weapon-style: model lives beside the def → copy beside the copy.
            copied += CopyModelDeps(parent, itemPath, parent, itemPath, modelFile);
        } else {
            // Shared-models style: <srcRoot>/models + <srcRoot>/textures.
            char srcModels[768], dstModels[768], srcTex[768], dstTex[768];
            snprintf(srcModels, sizeof srcModels, "%s/models",   srcRoot);
            snprintf(dstModels, sizeof dstModels, "%s/models",   gameDir);
            snprintf(srcTex,    sizeof srcTex,    "%s/textures", srcRoot);
            snprintf(dstTex,    sizeof dstTex,    "%s/textures", gameDir);
            copied += CopyModelDeps(srcModels, dstModels, srcTex, dstTex, modelFile);
        }
    }

    return copied;
}

// ---------------------------------------------------------------------------
//  EdImport_Pack
// ---------------------------------------------------------------------------

// Import every def of one type subdir from the pack into the game.
static int ImportTypeDir(const char *packDir, const char *typeDir,
                         const char *ext, const char *gameDir) {
    char dir[768];
    snprintf(dir, sizeof dir, "%s/%s", packDir, typeDir);
    if (!DirectoryExists(dir)) return 0;

    int copied = 0;
    FilePathList files = LoadDirectoryFilesEx(dir, ext, true);  // recursive
    for (unsigned int i = 0; i < files.count; i++)
        copied += EdImport_Item(files.paths[i], packDir, gameDir);
    UnloadDirectoryFiles(files);
    return copied;
}

int EdImport_Pack(const char *packDir, const char *gameDir) {
    if (!packDir || !gameDir) return 0;
    int copied = 0;
    copied += ImportTypeDir(packDir, "mobs",    ".mob",    gameDir);
    copied += ImportTypeDir(packDir, "perks",   ".perk",   gameDir);
    copied += ImportTypeDir(packDir, "weapons", ".weapon", gameDir);
    copied += ImportTypeDir(packDir, "props",   ".prop",   gameDir);
    return copied;
}
