#ifndef SHOOTER_DEFFILE_H
#define SHOOTER_DEFFILE_H

// ============================================================================
//  deffile.h — the project's shared "def file" reader.
//
//  One house format for all the data catalogs: line-oriented, `#` comments,
//  whitespace-separated `key value...` lines. It backs data/weapons/*.weapon
//  and data/mobs/*.mob today, and any future .perk / .prop catalog.
//
//  Deliberately raylib-free and stdlib-only (same seam rule as mapdoc.h), so
//  BOTH the game and the engine-only map editor can read the same files — the
//  editor reuses this to scan the mob catalog without any game dependency.
// ============================================================================

#include <stdbool.h>

// Tokenize one line IN PLACE on whitespace (space/tab/CR/LF). Returns the
// token count (capped at maxToks), filling toks[] with pointers into `line`
// (NUL terminators are written between tokens). Does not strip comments.
int  Eng_DefTokenize(char *line, char **toks, int maxToks);

// Parse a single float / int token. Returns false (leaving *out untouched)
// when the token is not a number.
bool Eng_DefParseFloat(const char *s, float *out);
bool Eng_DefParseInt(const char *s, int *out);

// Parse up to four colour components (r g b a, each 0-255) from
// toks[first..first+3] into rgba[]. A missing alpha defaults to 255. Returns
// the number of components actually parsed.
int  Eng_DefParseColor(char **toks, int n, int first, unsigned char rgba[4]);

// Per-line callback used by Eng_DefForEachLine: `lineNo` is 1-based, `n` is the
// token count, `toks` are the line's tokens, `user` is passed through.
typedef void (*EngDefLineFn)(int lineNo, int n, char **toks, void *user);

// Walk `text` (a NUL-terminated, WRITABLE buffer — it is mutated in place):
// for every line, strip a trailing `#` comment, tokenize, and (when non-empty)
// invoke `cb`. Up to 16 tokens per line are reported; extras are ignored.
void Eng_DefForEachLine(char *text, EngDefLineFn cb, void *user);

#endif // SHOOTER_DEFFILE_H
