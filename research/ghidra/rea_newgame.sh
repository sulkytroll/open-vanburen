#!/bin/bash
REA="$(dirname "$0")/rea.sh"
F3="${F3_EXE:?set F3_EXE to the tech demo's F3.exe}"
OUT="${REA_OUT:-./rea_out}"; mkdir -p "$OUT"
for a in 0x556cc9 0x440d50 0x426854 0x58ea43 0x440d8d 0x429a13 0x42a1e6; do "$REA" decompile "$F3" $a --format json > "$OUT/ng2_$a.json" 2>&1 & done
wait
echo ALL_DONE
