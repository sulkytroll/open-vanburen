#!/bin/bash
REA="$(dirname "$0")/rea.sh"
F3="${F3_EXE:?set F3_EXE to the tech demo's F3.exe}"
OUT="${REA_OUT:-./rea_out}"; mkdir -p "$OUT"
mkdir -p "$OUT"

"$REA" xrefs "$F3" 0x672058 --format json > "$OUT/xrefs_readb3d_ver.json" 2>&1 &
"$REA" xrefs "$F3" 0x6780b0 --format json > "$OUT/xrefs_loadmesh_idx.json" 2>&1 &
"$REA" xrefs "$F3" 0x6782e0 --format json > "$OUT/xrefs_loadmesh_notconv.json" 2>&1 &
"$REA" xrefs "$F3" 0x677f90 --format json > "$OUT/xrefs_exportgfxmesh.json" 2>&1 &
wait
echo "ALL_DONE"
