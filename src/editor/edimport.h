#ifndef SHOOTER_EDIMPORT_H
#define SHOOTER_EDIMPORT_H

// ============================================================================
//  edimport.{c,h} — copy-on-import core (docs/content-packs.md §5).
//
//  Importing copies a placeable def (.mob/.perk/.weapon/.prop) PLUS the unique
//  model/texture assets it references FROM a source root (a pack like
//  packs/zombies, or stdlib, or an external folder) INTO a target game folder
//  (games/<game>/), so the game becomes self-contained for that content. The
//  runtime overlay (game → stdlib) then resolves the game's own copies first.
//
//  Pure filesystem + deffile work: raylib FS + deffile.h only, never a
//  src/game/ header (seam rule). Copies are binary-safe (LoadFileData/
//  SaveFileData) so .glb/.png survive intact.
// ============================================================================

// Import ONE item. `srcDefPath` is the full path to a .mob/.perk/.weapon/.prop
// def file (e.g. "packs/zombies/mobs/zombie/zombie.mob"). `srcRoot` is the pack
// or stdlib root that owns it (e.g. "packs/zombies") — used to locate referenced
// models/ and textures/. `gameDir` is the target game root (e.g. "games/shooter").
// Creates <gameDir>/<type>/<name>/, copies the def there, and copies referenced
// models/<model> (+ its sibling .mtl + any map_Kd textures) into <gameDir>/models
// and <gameDir>/textures. Returns the number of files copied (>=1 on success),
// 0 on failure.
int EdImport_Item(const char *srcDefPath, const char *srcRoot, const char *gameDir);

// Import a WHOLE pack: every .mob/.perk/.weapon/.prop under
// <packDir>/{mobs,perks,weapons,props} plus referenced assets, into gameDir.
// Returns the total number of files copied.
int EdImport_Pack(const char *packDir, const char *gameDir);

#endif // SHOOTER_EDIMPORT_H
