#!/bin/bash
REA="$(dirname "$0")/rea.sh"
F3="${F3_EXE:?set F3_EXE to the tech demo's F3.exe}"
OUT="${REA_OUT:-./rea_out}"; mkdir -p "$OUT"
for a in 4c8c20 4c9380 4c9400 4c89e0 4d9930; do
  "$REA" decompile "$F3" 0x$a --format json > "$OUT/decomp_FUN_00$a.json" 2>&1 &
done
wait; echo ALL_DONE
