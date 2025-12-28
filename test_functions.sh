#!/bin/bash
# -----------------------------------------------------------------------------
# SPDX-License-Identifier: OBL-1.0
# Open Source Beer License (with Extra Bubbles)
# 
# Licensor: Atom Compiler Contributors
# Human LLM Controller: jens@bennerhq.com
# 
# If we meet some day and you think this code is worth it, you can buy
# the authors a beer (or two). If you see Jens, make it a cold one.
# 
# If you pour beer on your computer, the compiler will not run faster.
# If you pour beer on the authors, results may vary.

# -----------------------------------------------------------------------------
# Test script for ATOM compiler - compiles and runs all test files

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
BLUE='\033[0;34m'
YELLOW='\033[0;33m'
NC='\033[0m'

VERBOSE=0
ATOMC=${ATOMC:-./atomc}
WASMTIME=${WASMTIME:-wasmtime}

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -v|--verbose) VERBOSE=1; shift ;;
        -q|--quiet) VERBOSE=-1; shift ;;
        --compiler)
            ATOMC="$2"; shift 2 ;;
        --wasmtime)
            WASMTIME="$2"; shift 2 ;;
        -h|--help)
            echo "Usage: $0 [-v|--verbose] [-q|--quiet] [--compiler <path>] [--wasmtime <path>]"
            exit 0 ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done


# Read skip list

SKIPLIST_FILE="./testing/skiplist.txt"
SKIPLIST=()
if [[ -f "$SKIPLIST_FILE" ]]; then
    while IFS= read -r line; do
        # Remove leading/trailing whitespace
        line="${line#"${line%%[![:space:]]*}"}"
        line="${line%"${line##*[![:space:]]}"}"
        # Ignore comments and empty lines
        [[ "$line" =~ ^#.*$ || -z "$line" ]] && continue
        SKIPLIST+=("$line")
    done < "$SKIPLIST_FILE"
fi

# Function to check if a file is in the skip list
is_skipped() {
    local f="$1"
    # Remove leading/trailing whitespace from file path
    f="${f#"${f%%[![:space:]]*}"}"
    f="${f%"${f##*[![:space:]]}"}"
    for skip in "${SKIPLIST[@]}"; do
        [[ "$f" == "$skip" ]] && return 0
    done
    return 1
}

ATOM_FILES=$(find testing/functions testing/benchmarks -name "*.atom" -type f | sort)
[[ -z "$ATOM_FILES" ]] && { echo -e "${RED}No .atom files found${NC}"; exit 1; }

TOTAL=$(echo "$ATOM_FILES" | wc -l)
PASSED=0
FAILED=0
ACTUAL=0
OUTPUT=0
SKIPPED=0

mkdir -p build

START_TS=$(date +%s)
echo -e "${BLUE}==> Running $TOTAL tests (target: wasm)${NC}\n"

# Test function
test_file() {
    local file=$1
    local base=$(basename "$file" .atom)
    local wat_out="build/${base}.wat"
    local wasm_out="build/${base}.wasm"
    local err_file=$(mktemp)
    local actual_output=$(mktemp)
    
    # Input and expected output files
    local input_file="testing/stdin/${base}.in"
    
    # Compile ATOM to WAT
    if [[ $VERBOSE -eq 1 ]]; then
        "$ATOMC" "$file" -o "$wat_out" 2>&1 | tee "$err_file"
        [[ ${PIPESTATUS[0]} -ne 0 ]] && { rm "$err_file" "$actual_output"; return 1; }
    else
        "$ATOMC" "$file" -o "$wat_out" 2>"$err_file" || { [[ $VERBOSE -ge 0 ]] && cat "$err_file"; rm "$err_file" "$actual_output"; return 1; }
    fi
    
    # Convert WAT to WASM
    wat2wasm "$wat_out" -o "$wasm_out" 2>>"$err_file" || { [[ $VERBOSE -ge 0 ]] && cat "$err_file"; rm "$err_file" "$actual_output"; return 1; }
    
    # Run with wasmtime
    if [[ $VERBOSE -eq 1 ]]; then
        if [[ -f "$input_file" ]]; then
            "$WASMTIME" "$wasm_out" < "$input_file" 2>&1 > "$actual_output"
        else
            "$WASMTIME" "$wasm_out" 2>&1 > "$actual_output"
        fi
        local ret=0
    else
        if [[ -f "$input_file" ]]; then
            "$WASMTIME" "$wasm_out" < "$input_file" 2>/dev/null > "$actual_output"
        else
            "$WASMTIME" "$wasm_out" 2>/dev/null > "$actual_output"
        fi
        local ret=$?
    fi
    
    # Check output if expected file exists
    local expected_file="testing/stdout/${base}.out"
    if [[ -f "$expected_file" ]]; then
        if diff -q "$actual_output" "$expected_file" >/dev/null 2>&1; then
            rm "$err_file" "$actual_output"
            return 0
        else
            if [[ $VERBOSE -ge 0 ]]; then
                echo -e "\n${YELLOW}Expected vs Actual for $base:${NC}"
                diff "$expected_file" "$actual_output" || true
                ((ACTUAL++))
            fi
            rm "$err_file" "$actual_output"
            return 1
        fi
    else
        ((OUTPUT++))

        echo -e "\n${YELLOW}$expected_file not found.${NC}"
        rm "$err_file" "$actual_output"
        return 0
    fi
    
    rm "$err_file" "$actual_output"
    return $ret
}

# Test each file

for file in $ATOM_FILES; do
    # Check skip list
    if is_skipped "$file"; then
        printf "%-44s ${YELLOW}SKIPPED${NC}\n" "$file"
        ((SKIPPED++))
        continue
    fi

    printf "%-44s " "$file"

    if test_file "$file"; then
        echo -e "${GREEN}✓${NC}"
        ((PASSED++))
    else
        echo -e "\033[41m\033[97m✗${NC}"
        ((FAILED++))
    fi
done

# Summary
END_TS=$(date +%s)
ELAPSED=$((END_TS-START_TS))
echo -e "\n${BLUE}===== Summary =====${NC}"
echo -e "${BLUE}Duration:${NC} ${ELAPSED}s"
echo -e "\n"
echo -e "${GREEN}Passed:${NC} $PASSED"
echo -e "${YELLOW}Skipped:${NC} $SKIPPED"
echo -e "${RED}Errors:${NC}"
echo -e "${RED}    Failed:${NC} $FAILED"
echo -e "${RED}    Actual:${NC} $ACTUAL"
echo -e "${RED}    Output:${NC} $OUTPUT"

if [[ $FAILED -eq 0 ]]; then
    echo -e "\n${GREEN}✔ All tests passed${NC}"
else
    echo -e "\n${RED}✖ Some tests failed${NC}"
fi
exit $FAILED
