#!/bin/bash
# -----------------------------------------------------------------------------
# SPDX-License-Identifier: OBL-1.0
# Open Source Beer License (with Extra Bubbles)
# 
# Licensor: Atom Compiler Contributors
# Human LLM Controller: jens@bennerhq.com
# 
# If we meet some day and you think this code is worth it, you can buy
# the authors a beer (or two). If you see benner, make it a cold one.
# 
# If you pour beer on your computer, the compiler will not run faster.
# If you pour beer on the authors, results may vary.

# -----------------------------------------------------------------------------
# Debug script for ATOM compiler - compiles and runs a single test file 
# with detailed output
# 

if [ $# -lt 1 ]; then
    echo "Usage: $0 <atom_source_file> [args...]"
    exit 1
fi

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
BLUE='\033[0;34m'
YELLOW='\033[0;33m'
NC='\033[0m'

ATOMC=${ATOMC:-./atomc}
WASMTIME=${WASMTIME:-wasmtime}
WASMOUT="output.wasm"
SRC_FILE="$1"
shift

if [ ! -f "$SRC_FILE" ]; then
    echo -e "${RED}Error:${NC} Source file '$SRC_FILE' does not exist."
    exit 1
fi

make all

$ATOMC "$SRC_FILE" -o "$WASMOUT" 
if [ $? -ne 0 ]; then
    echo -e "${RED}Error: Compilation of $SRC_FILE failed${NC}"
    exit 1
fi

# Check for .in file in testing/stdin
INFILE="testing/stdin/$(basename "$SRC_FILE" .atom).in"
if [ -f "$INFILE" ]; then
    IN_CONTENT=$(cat "$INFILE")
    echo -e "${BLUE}Stdin:\n$IN_CONTENT${NC}"
    ACTUAL_OUT=$(cat "$INFILE" | $WASMTIME "$WASMOUT" "$@")
else
    ACTUAL_OUT=$($WASMTIME "$WASMOUT" "$@")
fi
if [ $? -ne 0 ]; then
    echo -e "${RED}Error: Executing $SRC_FILE failed${NC}"
    exit 1
fi

echo -e "$ACTUAL_OUT\n\n"

EXPECTED_SRC="testing/stdout/$(basename "$SRC_FILE" .atom).out"
if [ -f "$EXPECTED_SRC" ]; then
    EXPECTED_OUT=$(cat "$EXPECTED_SRC")

    DIFF_OUT=$(diff <(echo "$ACTUAL_OUT") <(echo "$EXPECTED_OUT"))
    if [ -n "$DIFF_OUT" ]; then
        echo -e "${YELLOW}$DIFF_OUT${NC}\n\n"
        exit 2
    else
        echo -e "${GREEN}Match $EXPECTED_SRC${NC}\n"
    fi
else
    echo -e "${YELLOW}Warning: '$EXPECTED_SRC' not found.${NC}\n"
    echo "$ACTUAL_OUT" > "$EXPECTED_SRC"
    echo -e "${BLUE}Creating '$EXPECTED_SRC' ${NC}\n"
    echo -e "${BLUE}$ACTUAL_OUT${NC}\n"
fi

exit 0
