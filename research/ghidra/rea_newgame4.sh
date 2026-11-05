#!/bin/bash
REA="$(dirname "$0")/rea.sh"
F3="${F3_EXE:?set F3_EXE to the tech demo's F3.exe}"
OUT="${REA_OUT:-./rea_out}"; mkdir -p "$OUT"
for a in 0x6406d2; do "$REA" decompile "$F3" $a --format json > "$OUT/ng4_$a.json" 2>&1 & done
wait
echo ALL_DONE
