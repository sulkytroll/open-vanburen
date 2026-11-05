#!/bin/bash
REA="$(dirname "$0")/rea.sh"
F3="${F3_EXE:?set F3_EXE to the tech demo's F3.exe}"
OUT="${REA_OUT:-./rea_out}"; mkdir -p "$OUT"
for a in 0x692934 0x69d438 0x6581ec 0x651be4 0x652008 0x6581b4; do "$REA" xrefs "$F3" $a --format json > "$OUT/ngx_$a.json" 2>&1 & done
wait
echo ALL_DONE
