#!/bin/bash
# Install Git hooks for Cosmo.
# Creates a relative symlink from .git/hooks/pre-commit to scripts/pre-commit.
#
# Usage:
#   scripts/install-hooks.sh

set -euo pipefail

# ── Colors ──────────────────────────────────────────────────────────
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

# ── Resolve paths ──────────────────────────────────────────────────
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GIT_HOOKS_DIR="$PROJECT_ROOT/.git/hooks"
HOOK_SOURCE="$PROJECT_ROOT/scripts/pre-commit"
HOOK_TARGET="$GIT_HOOKS_DIR/pre-commit"

# ── Validate ───────────────────────────────────────────────────────
if [[ ! -d "$GIT_HOOKS_DIR" ]]; then
    echo -e "${YELLOW}Warning: $GIT_HOOKS_DIR not found. Is this a git repository?${NC}"
    exit 1
fi

if [[ ! -f "$HOOK_SOURCE" ]]; then
    echo -e "${YELLOW}Warning: $HOOK_SOURCE not found.${NC}"
    exit 1
fi

# ── Ensure hook source is executable ───────────────────────────────
chmod +x "$HOOK_SOURCE"

# ── Create relative symlink (idempotent) ───────────────────────────
# Relative path: .git/hooks/pre-commit -> ../../scripts/pre-commit
ln -sf "../../scripts/pre-commit" "$HOOK_TARGET"

echo -e "${GREEN}${BOLD}✅ Pre-commit hook installed successfully.${NC}"
echo -e "${CYAN}   ${HOOK_TARGET} -> ../../scripts/pre-commit${NC}"
echo ""
echo -e "${CYAN}To uninstall: rm ${HOOK_TARGET}${NC}"
