#ifndef SHOOTER_DEVTOOLS_H
#define SHOOTER_DEVTOOLS_H

#include <stdbool.h>

// Check argv for a known dev CLI flag and run the corresponding mode.
// Returns true and sets *exitCode if a dev mode was handled (caller should
// return *exitCode immediately). Returns false if no dev flag matched.
bool Devtools_HandleCLI(int argc, char **argv, int *exitCode);

#endif
