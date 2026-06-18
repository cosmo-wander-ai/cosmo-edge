#!/bin/bash
# Cosmo Static Analysis Runner.
# Unified entry point for all static analysis tools.
#
# Usage:
#   scripts/static_analysis.sh --cppcheck           # Run cppcheck on all src/test files
#   scripts/static_analysis.sh --cppcheck --staged   # Only git-staged files
#   scripts/static_analysis.sh --clang-tidy          # Run clang-tidy (requires compile_commands.json)
#   scripts/static_analysis.sh --all                 # Run all available analyzers
#   scripts/static_analysis.sh --summary             # Show warning count from last build log

set -euo pipefail

# ── Colors ──────────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

# ── Defaults ────────────────────────────────────────────────────────
MODE=""
STAGED_ONLY=false
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# ── Parse arguments ─────────────────────────────────────────────────
usage() {
    echo -e "${BOLD}Cosmo Static Analysis Runner${NC}"
    echo ""
    echo "Usage: $0 [--staged] <--cppcheck|--clang-tidy|--all|--summary>"
    echo ""
    echo "Options:"
    echo "  --cppcheck    Run cppcheck static analyzer"
    echo "  --clang-tidy  Run clang-tidy (requires build/compile_commands.json)"
    echo "  --all         Run all available analyzers"
    echo "  --summary     Show compiler warning summary from last build"
    echo "  --staged      Only analyze git-staged .h/.cc files"
    echo "  -h|--help     Show this help"
    exit 1
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --cppcheck)   MODE="cppcheck";   shift ;;
        --clang-tidy) MODE="clang-tidy"; shift ;;
        --all)        MODE="all";        shift ;;
        --summary)    MODE="summary";    shift ;;
        --staged)     STAGED_ONLY=true;  shift ;;
        -h|--help)    usage ;;
        *)
            echo -e "${RED}Error: Unknown option '$1'${NC}"
            usage
            ;;
    esac
done

if [[ -z "$MODE" ]]; then
    echo -e "${RED}Error: Must specify a mode${NC}"
    usage
fi

# ── Collect files ───────────────────────────────────────────────────
collect_files() {
    if [[ "$STAGED_ONLY" == true ]]; then
        git -C "$PROJECT_ROOT" diff --cached --name-only --diff-filter=ACMR \
            | grep -E '\.(h|cc)$' \
            | while read -r f; do echo "$PROJECT_ROOT/$f"; done
    else
        find "$PROJECT_ROOT/src" "$PROJECT_ROOT/test" \
            -type f \( -name '*.h' -o -name '*.cc' \) \
            ! -path '*/3rd/*' \
            ! -path '*/build/*' \
            ! -name 'catch_amalgamated.*' \
            ! -name 'trompeloeil.hpp' \
            | sort
    fi
}

# ── cppcheck ────────────────────────────────────────────────────────
run_cppcheck() {
    if ! command -v cppcheck &> /dev/null; then
        echo -e "${RED}Error: cppcheck not found. Install via: apt install cppcheck${NC}"
        return 1
    fi

    local VERSION
    VERSION=$(cppcheck --version 2>&1)
    echo -e "${CYAN}Using: ${VERSION}${NC}"
    echo ""

    local SUPPRESSION_FILE="$PROJECT_ROOT/.cppcheck-suppressions"
    local SUPPRESSION_ARG=""
    if [[ -f "$SUPPRESSION_FILE" ]]; then
        SUPPRESSION_ARG="--suppressions-list=$SUPPRESSION_FILE"
    fi

    local FILES=()
    while IFS= read -r line; do
        [[ -n "$line" ]] && FILES+=("$line")
    done < <(collect_files)

    local TOTAL=${#FILES[@]}
    if [[ $TOTAL -eq 0 ]]; then
        echo -e "${YELLOW}No files to analyze.${NC}"
        return 0
    fi

    echo -e "${CYAN}Analyzing ${BOLD}${TOTAL}${NC}${CYAN} files with cppcheck...${NC}"
    echo ""

    local EXIT_CODE=0
    cppcheck \
        --std=c++17 \
        --language=c++ \
        --enable=warning,style,performance,portability \
        --inline-suppr \
        $SUPPRESSION_ARG \
        --quiet \
        --template='{file}:{line}: [{severity}] {id}: {message}' \
        -I "$PROJECT_ROOT/src" \
        -j"$(nproc)" \
        "${FILES[@]}" 2>&1 || EXIT_CODE=$?

    echo ""
    if [[ $EXIT_CODE -eq 0 ]]; then
        echo -e "${GREEN}✅ cppcheck: No issues found in ${TOTAL} files.${NC}"
    else
        echo -e "${YELLOW}⚠️  cppcheck: Issues found (see above). Exit code: ${EXIT_CODE}${NC}"
    fi
    return $EXIT_CODE
}

# ── clang-tidy ──────────────────────────────────────────────────────
run_clang_tidy() {
    local COMPILE_DB="$PROJECT_ROOT/build/compile_commands.json"

    if ! command -v run-clang-tidy &> /dev/null; then
        if ! command -v clang-tidy &> /dev/null; then
            echo -e "${RED}Error: clang-tidy not found. Install via: apt install clang-tidy${NC}"
            return 1
        fi
        echo -e "${YELLOW}Warning: run-clang-tidy not found, falling back to single-file mode${NC}"
    fi

    if [[ ! -f "$COMPILE_DB" ]]; then
        echo -e "${RED}Error: compile_commands.json not found at ${COMPILE_DB}${NC}"
        echo -e "${CYAN}Build the project first: scripts/build.sh${NC}"
        return 1
    fi

    local VERSION
    VERSION=$(clang-tidy --version 2>&1 | head -1)
    echo -e "${CYAN}Using: ${VERSION}${NC}"
    echo ""

    if command -v run-clang-tidy &> /dev/null; then
        echo -e "${CYAN}Running clang-tidy on project sources...${NC}"
        run-clang-tidy \
            -p "$PROJECT_ROOT/build" \
            -header-filter='src/.*\.h$' \
            -quiet \
            'src/.*\.(cc|h)$' 2>&1
    else
        # Fallback: run clang-tidy on files one by one
        local FILES=()
        while IFS= read -r line; do
            [[ -n "$line" ]] && FILES+=("$line")
        done < <(collect_files)

        echo -e "${CYAN}Running clang-tidy on ${#FILES[@]} files...${NC}"
        for f in "${FILES[@]}"; do
            clang-tidy \
                -p "$PROJECT_ROOT/build" \
                --header-filter='src/.*\.h$' \
                --quiet \
                "$f" 2>&1 || true
        done
    fi
}

# ── Summary ─────────────────────────────────────────────────────────
run_summary() {
    echo -e "${BOLD}Compiler Warning Summary${NC}"
    echo ""

    local BUILD_DIR="$PROJECT_ROOT/build"
    if [[ ! -d "$BUILD_DIR" ]]; then
        echo -e "${YELLOW}No build directory found. Build the project first.${NC}"
        return 1
    fi

    echo -e "${CYAN}Tip: Rebuild with warnings captured:${NC}"
    echo -e "  ${BOLD}scripts/build.sh 2>&1 | tee build.log${NC}"
    echo -e "  ${BOLD}grep -c 'warning:' build.log${NC}"
    echo ""

    if [[ -f "$PROJECT_ROOT/build.log" ]]; then
        local WARN_COUNT
        WARN_COUNT=$(grep -c 'warning:' "$PROJECT_ROOT/build.log" 2>/dev/null || echo "0")
        local ERR_COUNT
        ERR_COUNT=$(grep -c 'error:' "$PROJECT_ROOT/build.log" 2>/dev/null || echo "0")

        echo -e "Last build: ${YELLOW}${WARN_COUNT} warnings${NC}, ${RED}${ERR_COUNT} errors${NC}"

        if [[ $WARN_COUNT -gt 0 ]]; then
            echo ""
            echo -e "${BOLD}Top warning categories:${NC}"
            grep 'warning:' "$PROJECT_ROOT/build.log" \
                | sed 's/.*warning: //' \
                | sed 's/ \[.*//' \
                | sort | uniq -c | sort -rn | head -15
        fi
    else
        echo -e "${YELLOW}No build.log found. Rebuild with output capture.${NC}"
    fi
}

# ── Main ────────────────────────────────────────────────────────────
echo -e "${BOLD}══════════════════════════════════════════════════${NC}"
echo -e "${BOLD}  Cosmo Static Analysis${NC}"
echo -e "${BOLD}══════════════════════════════════════════════════${NC}"
echo ""

case "$MODE" in
    cppcheck)
        run_cppcheck
        ;;
    clang-tidy)
        run_clang_tidy
        ;;
    summary)
        run_summary
        ;;
    all)
        OVERALL_EXIT=0

        if command -v cppcheck &> /dev/null; then
            echo -e "${BOLD}── cppcheck ──────────────────────────────────────${NC}"
            run_cppcheck || OVERALL_EXIT=1
            echo ""
        else
            echo -e "${YELLOW}Skipping cppcheck (not installed)${NC}"
        fi

        if command -v clang-tidy &> /dev/null; then
            echo -e "${BOLD}── clang-tidy ────────────────────────────────────${NC}"
            run_clang_tidy || OVERALL_EXIT=1
            echo ""
        else
            echo -e "${YELLOW}Skipping clang-tidy (not installed)${NC}"
        fi

        echo -e "${BOLD}── Summary ───────────────────────────────────────${NC}"
        run_summary || true

        exit $OVERALL_EXIT
        ;;
esac
