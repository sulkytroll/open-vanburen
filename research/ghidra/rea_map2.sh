#!/bin/bash
REA="$(dirname "$0")/rea.sh"
F3="${F3_EXE:?set F3_EXE to the tech demo's F3.exe}"
OUT="${REA_OUT:-./rea_out}"; mkdir -p "$OUT"
for a in 699c7c 699c48 699ee4 699f64 69a05c 699f18; do
  "$REA" xrefs "$F3" 0x$a --format json > "$OUT/xr_$a.json" 2>&1 &
done
"$REA" search "$F3" "octree" --format json > "$OUT/s_octree.json" 2>&1 &
"$REA" search "$F3" "Collision" --format json > "$OUT/s_collision.json" 2>&1 &
wait; echo ALL_DONE
