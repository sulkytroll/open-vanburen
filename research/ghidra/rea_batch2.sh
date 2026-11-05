#!/bin/bash
REA="$(dirname "$0")/rea.sh"
F3="${F3_EXE:?set F3_EXE to the tech demo's F3.exe}"
OUT="${REA_OUT:-./rea_out}"; mkdir -p "$OUT"
mkdir -p "$OUT"

"$REA" decompile "$F3" 0x4dbe69 --format json > "$OUT/decomp_readb3d_1.json" 2>&1 &
"$REA" decompile "$F3" 0x527d2a --format json > "$OUT/decomp_loadmesh_idx.json" 2>&1 &
"$REA" decompile "$F3" 0x526ed7 --format json > "$OUT/decomp_loadmesh_notconv.json" 2>&1 &
"$REA" decompile "$F3" 0x524b43 --format json > "$OUT/decomp_exportgfxmesh_1.json" 2>&1 &
wait
echo "ALL_DONE"
