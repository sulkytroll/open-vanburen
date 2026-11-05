#!/bin/bash
REA="$(dirname "$0")/rea.sh"
F3="${F3_EXE:?set F3_EXE to the tech demo's F3.exe}"
OUT="${REA_OUT:-./rea_out}"; mkdir -p "$OUT"
for a in 0x58dbe0 0x42f9e0 0x495220 0x4b2fe0; do "$REA" decompile "$F3" $a --format json > "$OUT/ng3_$a.json" 2>&1 & done
for a in 0x440ce0; do "$REA" function "$F3" $a --format json > "$OUT/ng3f_$a.json" 2>&1 & done
wait
echo ALL_DONE
