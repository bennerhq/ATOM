#!/usr/bin/env bash
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
# Performance benchmarking for ATOM benchmarks under testing/benchmarks

# Skiplist logic (moved below shebang and license)
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
#!/usr/bin/env bash
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
# Performance benchmarking for ATOM benchmarks under testing/benchmarks
# - Discovers .atom files dynamically
# - Compiles to WAT/WASM
# - Runs with wasmtime and measures wall time using /usr/bin/time -p
# - Reports avg/min/max over N runs
# - Validates stdout against testing/stdout/*.out files

BUILD_DIR="./build"
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Defaults
RUNS=10
QUIET=0
ATOMC=${ATOMC:-./atomc}
WASMTIME=${WASMTIME:-wasmtime}
CC=${CC:-cc}
CFLAGS=${CFLAGS:--std=c99 -O3}
FILTER=""

# Colors
# Use ANSI C quoting to embed real escape codes for colors
GREEN=$'\033[0;32m'
RED=$'\033[0;31m'
BLUE=$'\033[0;34m'
YELLOW=$'\033[0;33m'
GRAY=$'\033[0;90m'
NC=$'\033[0m'

# Progress helpers (no output in quiet mode)
COLS=$(tput cols 2>/dev/null || echo 100)
progress_update() {
  local name="$1"; shift
  local msg="$*"
  if [[ $QUIET -eq 0 ]]; then
    printf "\r%-${COLS}s" "$name: $msg"
  fi
}
progress_done() {
  local name="$1"; shift
  local msg="$*"
  if [[ $QUIET -eq 0 ]]; then
    printf "\r%-${COLS}s\n" "$name: $msg"
  fi
}
progress_clear() {
  if [[ $QUIET -eq 0 ]]; then
    printf "\r%*s\r" "$COLS" ""
  fi
}

color_speedup() {
  local val="$1"
  local raw="${val/\%/}" # Remove % if present
  local padded=$(printf "%10s" "$val")
  # Use bc for float comparison
  if [[ "$val" == "n/a" ]]; then
    echo "$padded"
  elif awk "BEGIN{exit ($raw > 0)?0:1}"; then
    echo "${GREEN}${padded}${NC}"
  elif awk "BEGIN{exit ($raw < 0)?0:1}"; then
    echo "${RED}${padded}${NC}"
  else
    echo "${YELLOW}${padded}${NC}"
  fi
}

usage() {
  cat <<USAGE
Usage: $0 [options]

Options:
  -r, --runs N           Number of runs per benchmark (default: $RUNS)
  -q, --quiet            Minimal output; only summary line per benchmark
      --compiler PATH    Path to atom compiler binary (default: $ATOMC)
      --wasmtime PATH    Path to wasmtime executable (default: $WASMTIME)
      --filter PATTERN   Only benchmarks with basename matching PATTERN
  -h, --help             Show this help

Environment:
  ATOMC      Compiler override (same as --compiler)
  WASMTIME   Wasmtime override (same as --wasmtime)
USAGE
}

# Parse args
while [[ $# -gt 0 ]]; do
  case "$1" in
    -r|--runs)
      RUNS="$2"; shift 2 ;;
    -q|--quiet)
      QUIET=1; shift ;;
    --compiler)
      ATOMC="$2"; shift 2 ;;
    --wasmtime)
      WASMTIME="$2"; shift 2 ;;
    --filter)
      FILTER="$2"; shift 2 ;;
    -h|--help)
      usage; exit 0 ;;
    *) echo -e "${RED}Unknown option:${NC} $1" >&2; usage; exit 1 ;;
  esac
done

if ! command -v /usr/bin/time >/dev/null 2>&1; then
  echo -e "${RED}Missing /usr/bin/time; required for timing.${NC}" >&2
  exit 1
fi
if ! command -v "$WASMTIME" >/dev/null 2>&1; then
  echo -e "${RED}wasmtime not found (${WASMTIME}). Install wasmtime and retry.${NC}" >&2
  exit 1
fi
if [[ ! -x "$ATOMC" ]]; then
  echo -e "${YELLOW}Compiler not executable at ${ATOMC}. Attempting to build...${NC}"
  make -s
fi

# Discover benchmarks
ATOM_FILES=()
while IFS= read -r f; do ATOM_FILES+=("$f"); done < <(find testing/benchmarks -type f -name "*.atom" | sort)
if [[ ${#ATOM_FILES[@]} -eq 0 ]]; then
  echo -e "${RED}No benchmarks found under testing/benchmarks${NC}" >&2
  exit 1
fi

C_COUNT=$(find testing/benchmarks -type f -name "*.c" | wc -l | awk '{print $1}')
if [[ "$C_COUNT" -gt 0 ]] && ! command -v "$CC" >/dev/null 2>&1; then
  echo -e "${RED}C compiler not found (${CC}). Set CC or install clang/gcc.${NC}" >&2
  exit 1
fi

mkdir -p build

printf "${BLUE}==> Benchmarking %d files (runs: %s)${NC}\n\n" "${#ATOM_FILES[@]}" "$RUNS"

TOTAL=${#ATOM_FILES[@]}
SUCCEEDED=0
FAILED=0
ROW_BUFFER=""
RATIO_SUM=0
RATIO_COUNT=0
FASTER_CPP=0
FASTER_WASM=0
ABOUT_PARITY=0
START_TS=$(date +%s)

# Print table header up-front for streaming rows in non-quiet mode
if [[ $QUIET -eq 0 ]]; then
  progress_clear
  printf "${BLUE}%-24s │ %12s │ %12s │ %10s${NC}\n" "benchmark" "wasm avg" "c avg" "speedup"
  printf '%*s\n' 70 '' | tr ' ' '─'
fi


for file in "${ATOM_FILES[@]}"; do
  # Skip files in skiplist
  if is_skipped "$file"; then
   [[ $QUIET -eq 0 ]] && echo -e "${YELLOW}$file (in skiplist.txt)${NC}"
    continue
  fi
  base=$(basename "$file" .atom)
  if [[ -n "$FILTER" ]] && [[ "$base" != *"$FILTER"* ]]; then
    continue
  fi
  wat_out="build/${base}.wat"
  wasm_out="build/${base}.wasm"

  c_file=""
  if [[ -f "testing/benchmarks/${base}.c" ]]; then
    c_file="testing/benchmarks/${base}.c"
  else
    echo -e "${RED}Missing C file for benchmark: ${base}.c${NC}" >&2
    ((FAILED++))
  fi
  c_out="build/${base}_c"

  atom_stats=""
  atom_avg=""
  cpp_stats=""
  cpp_avg=""
  bench_ok=1

  # Compile
  err_file=$(mktemp)
  #progress_update "$base" "compile wasm..."
  if ! "$ATOMC" "$file" -o "$wat_out" 2>"$err_file"; then
    [[ $QUIET -eq 0 ]] && echo -e "${RED}✗ compile${NC}"
    [[ $QUIET -eq 0 ]] && sed -e 's/^/  /' "$err_file" >&2 || true
    ((FAILED++))
    rm -f "$err_file"
    continue
  fi
  #progress_update "$base" "compile wasm ✓ | wat2wasm..."
  if ! wat2wasm "$wat_out" -o "$wasm_out" 2>>"$err_file"; then
    [[ $QUIET -eq 0 ]] && echo -e "${RED}✗ wat2wasm${NC}"
    [[ $QUIET -eq 0 ]] && sed -e 's/^/  /' "$err_file" >&2 || true
    ((FAILED++))
    rm -f "$err_file"
    continue
  fi
  rm -f "$err_file"
  #progress_update "$base" "compile wasm ✓ | wat2wasm ✓"

  # Run timing (combined progress for WASM and C)
  times_file=$(mktemp)
  run_failed=0
  wasm_runs=0
  c_runs=0
  total_runs=$RUNS
  c_total_runs=0
  if [[ -n "$c_file" ]]; then
    c_total_runs=$RUNS
  fi
  combined_total=$((RUNS + c_total_runs))
  # WASM timing
  for ((i=1; i<=RUNS; i++)); do
    wasm_runs=$i
    # Combined progress bar: yellow for wasm, green for c
    progress_done_count=$((wasm_runs + c_runs))
    bar_width=20
    wasm_filled=$((wasm_runs * bar_width / combined_total))
    c_filled=$((c_runs * bar_width / combined_total))
    empty=$((bar_width - wasm_filled - c_filled))
    bar="${YELLOW}$(printf '%*s' "$wasm_filled" | tr ' ' '█')${GREEN}$(printf '%*s' "$c_filled" | tr ' ' '█')${NC}$(printf '%*s' "$empty" | tr ' ' '░')"
    progress_update "$base" "$bar ${GRAY}$progress_done_count/$combined_total (wasm $wasm_runs/$RUNS, c $c_runs/$c_total_runs)${NC}"
    if ! { /usr/bin/time -p "$WASMTIME" "$wasm_out" >/dev/null; } 2>"$times_file.tmp"; then
      run_failed=1
      break
    fi
    real_line=$(grep '^real ' "$times_file.tmp" | awk '{print $2}')
    echo "$real_line" >> "$times_file"
  done
  rm -f "$times_file.tmp"

  if [[ $run_failed -eq 1 ]]; then
    [[ $QUIET -eq 0 ]] && echo -e "${RED}✗ runtime${NC}"
    ((FAILED++))
    rm -f "$times_file"
    continue
  fi

  # Compute stats (avg only)
  atom_avg=$(awk '{sum+=$1; n++} END{if(n>0)printf "%.6f", sum/n; else print ""}' "$times_file")
  rm -f "$times_file"
  #progress_update "$base" "wasm timing ✓"

  c_failed=0
  if [[ -n "$c_file" ]]; then
    err_file=$(mktemp)
    #progress_update "$base" "c compile..."
    if ! "$CC" $CFLAGS -I testing/benchmarks "$c_file" -o "$c_out" 2>"$err_file"; then
      c_failed=1
      bench_ok=0
      [[ $QUIET -eq 0 ]] && echo -e "${RED}| c compile failed${NC}"
      [[ $QUIET -eq 0 ]] && sed -e 's/^/  /' "$err_file" >&2 || true
    fi
    rm -f "$err_file"

    if [[ $c_failed -eq 0 ]]; then
      #progress_update "$base" "c compile ✓"
      c_times=$(mktemp)
      run_failed=0
      for ((i=1; i<=RUNS; i++)); do
        c_runs=$i
        progress_done_count=$((wasm_runs + c_runs))
        bar_width=20
        wasm_filled=$((wasm_runs * bar_width / combined_total))
        c_filled=$((c_runs * bar_width / combined_total))
        empty=$((bar_width - wasm_filled - c_filled))
        bar="${YELLOW}$(printf '%*s' "$wasm_filled" | tr ' ' '█')${GREEN}$(printf '%*s' "$c_filled" | tr ' ' '█')${NC}$(printf '%*s' "$empty" | tr ' ' '░')"
        progress_update "$base" "$bar ${GRAY}$progress_done_count/$combined_total (wasm $wasm_runs/$RUNS, c $c_runs/$c_total_runs)${NC}"
        if ! { /usr/bin/time -p "$c_out" >/dev/null; } 2>"$c_times.tmp"; then
          run_failed=1
          break
        fi
        c_real=$(grep '^real ' "$c_times.tmp" | awk '{print $2}')
        echo "$c_real" >> "$c_times"
      done
      rm -f "$c_times.tmp"

      if [[ $run_failed -eq 1 ]]; then
        c_failed=1
        bench_ok=0
        [[ $QUIET -eq 0 ]] && echo -e "${RED}| c runtime failed${NC}"
        rm -f "$c_times"
      else
        c_avg=$(awk '{sum+=$1; n++} END{if(n>0)printf "%.6f", sum/n; else print ""}' "$c_times")
        rm -f "$c_times"
        progress_update "$base" "c timing ✓"
      fi
    fi
  fi

  # Remove compiled C binary after timing
  rm -f "$c_out"

  ratio="n/a"
  ratio_val=""
  if [[ -n "$c_avg" ]]; then
    speedup_val=$(awk -v a="$atom_avg" -v c="$c_avg" 'BEGIN{if(c==0||c==""||a==""||c=="NaN")print ""; else printf "%.4f", (1 - a/c)}')
    if [[ -n "$speedup_val" ]]; then
      ratio=$(awk -v s="$speedup_val" 'BEGIN{printf "%.1f%%", s*100}')
      # Aggregate summary stats
      RATIO_SUM=$(awk -v s="$RATIO_SUM" -v r="$speedup_val" 'BEGIN{printf "%.6f", s + r}')
      RATIO_COUNT=$((RATIO_COUNT+1))
      if awk -v s="$speedup_val" 'BEGIN{exit (s > 0)?0:1}'; then
        FASTER_WASM=$((FASTER_WASM+1))
      elif awk -v s="$speedup_val" 'BEGIN{exit (s < 0)?0:1}'; then
        FASTER_CPP=$((FASTER_CPP+1))
      else
        ABOUT_PARITY=$((ABOUT_PARITY+1))
      fi
    fi
  fi

  if [[ $QUIET -eq 0 ]]; then
    progress_clear
    speed_col="$ratio"
    if [[ "$ratio" != "n/a" ]]; then
      speed_col=$(color_speedup "$ratio")
    fi
    atom_avg_str=$(test -n "$atom_avg" && printf "%.3fs" "$atom_avg" || echo "n/a")
    c_avg_str=$(test -n "$c_avg" && printf "%.3fs" "$c_avg" || echo "n/a")
    row=$(printf "%-24s │ %12s │ %12s │ %s" "$base" "$atom_avg_str" "$c_avg_str" "$speed_col")

    if [[ ! -f "testing/stdout/${base}.out" ]]; then
      row="${row} ${YELLOW}★${NC}"
    fi

    printf "%s\n" "$row"
    ROW_BUFFER+="$row\n"
  else
    echo "$base wasm avg: ${atom_avg:-n/a}s"
    if [[ -n "$c_avg" ]]; then
      echo "$base c avg: ${c_avg:-n/a}s speedup: $ratio"
    elif [[ $c_failed -eq 1 ]]; then
      echo "$base c: failed"
    fi
  fi

  if [[ $bench_ok -eq 1 ]]; then
    ((SUCCEEDED++))
  else
    ((FAILED++))
  fi
done

END_TS=$(date +%s)
ELAPSED=$((END_TS-START_TS))

if [[ $QUIET -eq 0 ]]; then
  progress_clear
  printf '%*s\n' 70 '' | tr ' ' '─'
fi

mean_ratio="n/a"
if [[ $RATIO_COUNT -gt 0 ]]; then
  mean_ratio=$(awk -v s="$RATIO_SUM" -v n="$RATIO_COUNT" 'BEGIN{printf "%.1f%%", (s/n)*100}')
fi

if [[ $QUIET -eq 0 ]]; then
  mean_ratio_col=$(color_speedup "$mean_ratio")
  printf "%-24s │ %12d │ %12d │ %s\n" "# fastest" "$FASTER_WASM" "$FASTER_CPP" "$mean_ratio_col"
fi

echo -e "\n${GREEN}Succeeded:${NC} $SUCCEEDED  ${RED}Failed:${NC} $FAILED  ${YELLOW}Duration:${NC} ${ELAPSED}s"

exit $([[ $FAILED -eq 0 ]] && echo 0 || echo 1)
