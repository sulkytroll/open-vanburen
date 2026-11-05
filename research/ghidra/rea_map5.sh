#!/bin/bash
REA="$(dirname "$0")/rea.sh"
F3="${F3_EXE:?set F3_EXE to the tech demo's F3.exe}"
OUT="${REA_OUT:-./rea_out}"; mkdir -p "$OUT"
for a in 574f60; do
  "$REA" decompile "$F3" 0x$a --format json > "$OUT/dc_$a.json" 2>&1 &
done
wait; echo ALL_DONE
