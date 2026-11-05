#!/bin/bash
REA="$(dirname "$0")/rea.sh"
F3="${F3_EXE:?set F3_EXE to the tech demo's F3.exe}"
OUT="${REA_OUT:-./rea_out}"; mkdir -p "$OUT"
"$REA" decompile "$F3" 0x5bd840 --format json > "$OUT/dc_5bd840.json" 2>&1 &
for a in 6705a0 6707a8 670480; do
  "$REA" xrefs "$F3" 0x$a --format json > "$OUT/xr_$a.json" 2>&1 &
done
wait; echo ALL_DONE
