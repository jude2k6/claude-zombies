#!/usr/bin/env bash
# check-seam.sh — cardinal-rule enforcement for the engine/game seam.
#
# Engine code must never include a game header. Run this script (or let CMake
# run it via the 'seam-check' target) to verify that src/engine/ contains no
# game-concept includes. Exits 1 if any violation is found.
#
# Checked game headers (§2 of docs/engine-game-separation.md + level.h):
#   weapons.h  perks.h  entities.h  game.h  player.h
#   interact.h hud.h    menu.h      level.h

set -euo pipefail

# Resolve repo root from this script's own location so it works regardless of CWD.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
ENGINE_DIR="${REPO_ROOT}/src/engine"

if [ ! -d "${ENGINE_DIR}" ]; then
    echo "seam-check: src/engine/ not found at ${ENGINE_DIR}" >&2
    exit 1
fi

VIOLATIONS="$(grep -rEl \
    'weapons\.h|perks\.h|entities\.h|game\.h|player\.h|interact\.h|hud\.h|menu\.h|level\.h|world\.h|audio_director\.h' \
    "${ENGINE_DIR}" 2>/dev/null || true)"

if [ -n "${VIOLATIONS}" ]; then
    echo "SEAM VIOLATION: the following src/engine/ files include game headers:"
    echo "${VIOLATIONS}"
    exit 1
fi

echo "seam OK: src/engine/ is game-clean"
exit 0
