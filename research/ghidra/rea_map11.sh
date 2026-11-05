#!/bin/bash
REA="$(dirname "$0")/rea.sh"
F3="${F3_EXE:?set F3_EXE to the tech demo's F3.exe}"
OUT="${REA_OUT:-./rea_out}"; mkdir -p "$OUT"
"$REA" decompile "$F3" 0x57aa00 --format json > "$OUT/dc_57aa00.json" 2>&1
echo ALL_DONE
