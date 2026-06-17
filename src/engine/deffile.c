// ============================================================================
//  deffile.c — implementation of the shared def-file reader (see deffile.h).
// ============================================================================

#include "deffile.h"

#include <stdlib.h>
#include <string.h>

int Eng_DefTokenize(char *line, char **toks, int maxToks) {
    int n = 0;
    char *p = line;
    while (*p && n < maxToks) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (!*p) break;
        toks[n++] = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;
        if (*p) { *p = 0; p++; }
    }
    return n;
}

bool Eng_DefParseFloat(const char *s, float *out) {
    if (!s) return false;
    char *end = NULL;
    float v = strtof(s, &end);
    if (end == s) return false;
    *out = v;
    return true;
}

bool Eng_DefParseInt(const char *s, int *out) {
    if (!s) return false;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s) return false;
    *out = (int)v;
    return true;
}

int Eng_DefParseColor(char **toks, int n, int first, unsigned char rgba[4]) {
    int got = 0;
    int defaults[4] = { 0, 0, 0, 255 };
    for (int c = 0; c < 4; c++) {
        int v = defaults[c];
        if (first + c < n && Eng_DefParseInt(toks[first + c], &v)) got++;
        else if (c == 3) { /* alpha defaults to 255 even if absent */ }
        if (v < 0)   v = 0;
        if (v > 255) v = 255;
        rgba[c] = (unsigned char)v;
    }
    return got;
}

void Eng_DefForEachLine(char *text, EngDefLineFn cb, void *user) {
    if (!text || !cb) return;
    char *line = text;
    int lineNo = 0;
    while (*line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        lineNo++;

        // Strip a trailing comment.
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;

        char *toks[16];
        int n = Eng_DefTokenize(line, toks, 16);
        if (n > 0) cb(lineNo, n, toks, user);

        if (!nl) break;
        line = nl + 1;
    }
}
