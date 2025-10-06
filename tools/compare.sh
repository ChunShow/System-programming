#!/bin/bash
#---------------------------------------------------------------------------------------------------
# System Programming                         I/O Lab                                      Fall 2025
#
# This script compares the output of the reference binary and my binary for the dirtree command.
# Compare outputs of reference and student dirtree binaries.
# Usage: ./compare.sh [--keep] [ARGS...]
#   --keep : keep output files (ref.out, my.out) instead of temp files

# set -euo pipefail

REF_BIN='../reference/dirtree'
MY_BIN='../bin/dirtree'

KEEP=0
if [[ "${1:-}" == "--keep" ]]; then
  KEEP=1
  shift
fi

# --- sanity checks ---
if [[ ! -x "$REF_BIN" ]]; then
  echo "Error: reference binary not executable: $REF_BIN" >&2
  exit 1
fi
if [[ ! -x "$MY_BIN" ]]; then
  echo "Error: student binary not executable: $MY_BIN" >&2
  exit 1
fi

# --- prepare output files ---
if [[ $KEEP -eq 1 ]]; then
  REF_OUT="ref.out"
  MY_OUT="my.out"
  : > "$REF_OUT"
  : > "$MY_OUT"
  CLEANUP=:
else
  REF_OUT="$(mktemp -t ref.out.XXXXXX)"
  MY_OUT="$(mktemp -t my.out.XXXXXX)"
  CLEANUP="rm -f '$REF_OUT' '$MY_OUT'"
fi
trap "eval $CLEANUP" EXIT

# --- run both binaries (stdout+stderr) ---
"$REF_BIN" "$@" >"$REF_OUT" 2>&1
"$MY_BIN"  "$@" >"$MY_OUT"  2>&1

# --- choose diff command & options ---
DIFFCMD="diff"
if command -v colordiff >/dev/null 2>&1; then
  DIFFCMD="colordiff"
fi

DIFF_OPTS=(-u -w --label=reference --label=student)
# add color if GNU diff supports it
if "$DIFFCMD" --help 2>&1 | grep -q -- '--color'; then
  DIFF_OPTS=(--color=always "${DIFF_OPTS[@]}")
fi

# --- show diff with line numbers ---
$DIFFCMD "${DIFF_OPTS[@]}" "$REF_OUT" "$MY_OUT" | nl