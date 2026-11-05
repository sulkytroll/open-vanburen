#!/bin/bash
REA="$(dirname "$0")/rea.sh"
F3="${F3_EXE:?set F3_EXE to the tech demo's F3.exe}"
OUT="${REA_OUT:-./rea_out}"; mkdir -p "$OUT"
mkdir -p "$OUT"

"$REA" decompile "$F3" 0x4cad90 --format json > "$OUT/decomp_FUN_004cad90.json" 2>&1 &
"$REA" decompile "$F3" 0x4d79b0 --format json > "$OUT/decomp_FUN_004d79b0.json" 2>&1 &
"$REA" decompile "$F3" 0x4c9b30 --format json > "$OUT/decomp_FUN_004c9b30.json" 2>&1 &
"$REA" decompile "$F3" 0x4d9bb0 --format json > "$OUT/decomp_FUN_004d9bb0.json" 2>&1 &
wait
echo "ALL_DONE"
