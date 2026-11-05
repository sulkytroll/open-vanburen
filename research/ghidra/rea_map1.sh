#!/bin/bash
REA="$(dirname "$0")/rea.sh"
F3="${F3_EXE:?set F3_EXE to the tech demo's F3.exe}"
OUT="${REA_OUT:-./rea_out}"; mkdir -p "$OUT"
"$REA" search "$F3" "EMAP" --format json > "$OUT/s_emap.json" 2>&1 &
"$REA" search "$F3" "map" --format json > "$OUT/s_map.json" 2>&1 &
"$REA" search "$F3" "Map::" --format json > "$OUT/s_mapcc.json" 2>&1 &
"$REA" search "$F3" "Entity" --format json > "$OUT/s_entity.json" 2>&1 &
wait; echo ALL_DONE
