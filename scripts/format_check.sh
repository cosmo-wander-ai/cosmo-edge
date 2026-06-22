#!/bin/bash
# Format checker for Cosmo C++ source files.
# Uses clang-format with the project's .clang-format configuration.
#
# Usage:
#   scripts/format_check.sh --check          # Check all src/test files, list non-compliant ones
#   scripts/format_check.sh --fix            # Auto-fix all non-compliant files
#   scripts/format_check.sh --diff           # Show detailed diff for each non-compliant file
#   scripts/format_check.sh --staged --check # Check only git-staged files
#   scripts/format_check.sh --staged --fix   # Fix only git-staged files

set -euo pipefail

# ── Colors ──────────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'  # No Color

# ── Defaults ────────────────────────────────────────────────────────
MODE=""
STAGED_ONLY=false
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CLANG_FORMAT="${CLANG_FORMAT:-clang-format}"

# ── Parse arguments ─────────────────────────────────────────────────
usage() {
    echo "Usage: $0 [--staged] <--check|--fix|--diff>"
    echo ""
    echo "Options:"
    echo "  --check    Check files, list non-compliant ones (exit code 1 if any found)"
    echo "  --fix      Auto-format all non-compliant files in place"
    echo "  --diff     Show detailed diff for each non-compliant file"
    echo "  --staged   Only process git-staged .h/.cc files"
    echo "  -h|--help  Show this help"
    exit 1
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --check)  MODE="check";  shift ;;
        --fix)    MODE="fix";    shift ;;
        --diff)   MODE="diff";   shift ;;
        --staged) STAGED_ONLY=true; shift ;;
        -h|--help) usage ;;
        *)
            echo -e "${RED}Error: Unknown option '$1'${NC}"
            usage
            ;;
    esac
done

if [[ -z "$MODE" ]]; then
    echo -e "${RED}Error: Must specify --check, --fix, or --diff${NC}"
    usage
fi

# ── Verify clang-format is available ────────────────────────────────
if ! command -v "$CLANG_FORMAT" &> /dev/null; then
    echo -e "${RED}Error: clang-format not found. Install it or set CLANG_FORMAT env var.${NC}"
    exit 1
fi

VERSION=$("$CLANG_FORMAT" --version 2>&1)
echo -e "${CYAN}Using: ${VERSION}${NC}"

# ── Collect files ───────────────────────────────────────────────────
collect_files() {
    if [[ "$STAGED_ONLY" == true ]]; then
        git -C "$PROJECT_ROOT" diff --cached --name-only --diff-filter=ACMR \
            | grep -E '\.(h|cc)$' \
            | grep -vE '(^|/)3rd/' \
            | while read -r f; do echo "$PROJECT_ROOT/$f"; done
    else
        find "$PROJECT_ROOT/src" "$PROJECT_ROOT/test" \
            -type f \( -name '*.h' -o -name '*.cc' \) \
            ! -path '*/3rd/*' \
            ! -path '*/build/*' \
            | sort
    fi
}

FILES=()
while IFS= read -r line; do
    [[ -n "$line" ]] && FILES+=("$line")
done < <(collect_files)

TOTAL=${#FILES[@]}

if [[ $TOTAL -eq 0 ]]; then
    echo -e "${YELLOW}No files to check.${NC}"
    exit 0
fi

echo -e "${CYAN}Scanning ${BOLD}${TOTAL}${NC}${CYAN} files...${NC}"
echo ""

# ── Process files ───────────────────────────────────────────────────
NON_COMPLIANT=()
FIXED=()

for filepath in "${FILES[@]}"; do
    relpath="${filepath#$PROJECT_ROOT/}"

    if ! diff <("$CLANG_FORMAT" "$filepath") "$filepath" > /dev/null 2>&1; then
        case "$MODE" in
            check)
                NON_COMPLIANT+=("$relpath")
                ;;
            diff)
                NON_COMPLIANT+=("$relpath")
                echo -e "${YELLOW}── ${relpath} ──${NC}"
                diff <("$CLANG_FORMAT" "$filepath") "$filepath" || true
                echo ""
                ;;
            fix)
                "$CLANG_FORMAT" -i "$filepath"
                FIXED+=("$relpath")
                ;;
        esac
    fi
done

# ── Report ──────────────────────────────────────────────────────────
echo ""
echo -e "${BOLD}═══════════════════════════════════════════════${NC}"

case "$MODE" in
    check|diff)
        COUNT=${#NON_COMPLIANT[@]}
        if [[ $COUNT -eq 0 ]]; then
            echo -e "${GREEN}✅ All ${TOTAL} files are properly formatted.${NC}"
            exit 0
        else
            echo -e "${RED}❌ ${COUNT} / ${TOTAL} files need formatting:${NC}"
            echo ""
            for f in "${NON_COMPLIANT[@]}"; do
                echo -e "  ${YELLOW}•${NC} $f"
            done
            echo ""
            echo -e "${CYAN}Run '${BOLD}scripts/format_check.sh --fix${NC}${CYAN}' to auto-fix.${NC}"
            exit 1
        fi
        ;;
    fix)
        COUNT=${#FIXED[@]}
        if [[ $COUNT -eq 0 ]]; then
            echo -e "${GREEN}✅ All ${TOTAL} files were already properly formatted.${NC}"
        else
            echo -e "${GREEN}✅ Fixed ${COUNT} / ${TOTAL} files:${NC}"
            echo ""
            for f in "${FIXED[@]}"; do
                echo -e "  ${GREEN}✓${NC} $f"
            done
        fi
        exit 0
        ;;
esac
