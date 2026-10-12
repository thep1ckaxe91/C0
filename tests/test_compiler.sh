#!/usr/bin/env bash
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build"
COMPILER="${BUILD_DIR}/c0c"

# Color constants
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== C0 Compiler Test Suite ===${NC}"

# Ensure binary is built
if [[ ! -x "$COMPILER" ]]; then
    echo "Compiler binary not found at $COMPILER, building..."
    timeout 60 cmake -B "$BUILD_DIR" -G Ninja
    timeout 60 cmake --build "$BUILD_DIR"
fi

if [[ ! -x "$COMPILER" ]]; then
    echo -e "${RED}ERROR: Failed to produce compiler binary.${NC}"
    exit 1
fi

TOTAL=0
PASSED=0
FAILED=0

# 1. Test Valid Snippets (Must exit 0)
echo -e "\n${BLUE}[Phase 1] Testing Valid C0 Programs (Expect Exit Code 0)...${NC}"
for file in "${REPO_ROOT}"/tests/valid/*.c0; do
    [[ -e "$file" ]] || continue
    TOTAL=$((TOTAL + 1))
    test_name="$(basename "$file")"
    
    if timeout 5s "$COMPILER" "$file" > /dev/null 2>&1; then
        echo -e "  [PASS] $test_name (Exit code 0)"
        PASSED=$((PASSED + 1))
    else
        echo -e "  ${RED}[FAIL]${NC} $test_name (Expected exit code 0, got non-zero)"
        FAILED=$((FAILED + 1))
    fi
done

# 2. Test Invalid Snippets (Must exit non-zero)
echo -e "\n${BLUE}[Phase 2] Testing Invalid C0 Programs (Expect Exit Code != 0)...${NC}"
for file in "${REPO_ROOT}"/tests/invalid/*.c0; do
    [[ -e "$file" ]] || continue
    TOTAL=$((TOTAL + 1))
    test_name="$(basename "$file")"
    
    if ! timeout 5s "$COMPILER" "$file" > /dev/null 2>&1; then
        echo -e "  [PASS] $test_name (Rejected with non-zero exit code)"
        PASSED=$((PASSED + 1))
    else
        echo -e "  ${RED}[FAIL]${NC} $test_name (Expected rejection, but parser exited with 0)"
        FAILED=$((FAILED + 1))
    fi
done

# 3. Test Multi-Line Error Recovery Specifically
echo -e "\n${BLUE}[Phase 3] Verifying Multi-Line Syntax Error Recovery...${NC}"
TOTAL=$((TOTAL + 1))
multi_err_file="${REPO_ROOT}/tests/invalid/02_syntax_multi_line.c0"
err_output=$(timeout 5s "$COMPILER" "$multi_err_file" 2>&1 || true)
err_count=$(echo "$err_output" | grep -c "error:" || true)

if [[ $err_count -ge 4 ]]; then
    echo -e "  [PASS] Multi-line error recovery: Discovered $err_count distinct syntax errors without aborting."
    PASSED=$((PASSED + 1))
else
    echo -e "  ${RED}[FAIL]${NC} Multi-line error recovery failed: Expected >= 4 errors reported across multiple lines, got $err_count"
    echo "$err_output"
    FAILED=$((FAILED + 1))
fi

# Summary
echo -e "\n${BLUE}=== Summary ===${NC}"
echo -e "Total:  $TOTAL"
echo -e "Passed: ${GREEN}$PASSED${NC}"
echo -e "Failed: ${RED}$FAILED${NC}"

if [[ $FAILED -eq 0 ]]; then
    echo -e "\n${GREEN}All tests passed successfully.${NC}"
    exit 0
else
    echo -e "\n${RED}Test suite encountered failures.${NC}"
    exit 1
fi
