// edassets.c — see edassets.h. Scans the content overlay for maps / models /
// textures into a de-duped index for the asset-browser panel.

#include "edassets.h"
#include "content.h"   // Eng_ContentDirs
#include "raylib.h"
#include <stdio.h>
#include <string.h>

// Scan one content subdir family (e.g. "models", ".glb") across all overlay
// roots into out[], de-duping by display name (first root wins, so a game-root
// asset shadows a same-named library one — same rule as EdScanIdNameCatalog).
static int ScanFamily(const char *subdir, const char *ext,
                      EdAsset out[], int max) {
    int count = 0;
    char dirs[4][512];
    int nDirs = Eng_ContentDirs(subdir, dirs, 4);

    for (int d = 0; d < nDirs && count < max; d++) {
        if (!DirectoryExists(dirs[d])) continue;
        FilePathList files = LoadDirectoryFilesEx(dirs[d], ext, true);
        for (unsigned i = 0; i < files.count && count < max; i++) {
            const char *stem = GetFileNameWithoutExt(files.paths[i]);
            // De-dup by display name against what we've already taken.
            bool dup = false;
            for (int k = 0; k < count; k++)
                if (strcmp(out[k].name, stem) == 0) { dup = true; break; }
            if (dup) continue;
            snprintf(out[count].name, ED_ASSET_NAME_LEN, "%s", stem);
            snprintf(out[count].path, ED_ASSET_PATH_LEN, "%s", files.paths[i]);
            count++;
        }
        UnloadDirectoryFiles(files);
    }
    return count;
}

void EdAssets_Scan(EdAssetIndex *ix) {
    ix->mapCount     = ScanFamily("maps",     ".map", ix->maps,     ED_MAX_MAP_ASSETS);
    ix->modelCount   = ScanFamily("models",   ".glb", ix->models,   ED_MAX_MODEL_ASSETS);
    ix->textureCount = ScanFamily("textures", ".png", ix->textures, ED_MAX_TEXTURE_ASSETS);
}
