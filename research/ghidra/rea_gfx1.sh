#!/bin/bash
REA="$(dirname "$0")/rea.sh"
F3="${F3_EXE:?set F3_EXE to the tech demo's F3.exe}"
OUT="${REA_OUT:-./rea_out}"; mkdir -p "$OUT"
"$REA" decompile "$F3" 0x5049b7 --format json > "$OUT/dc_enumdevices.json" 2>&1 &
"$REA" decompile "$F3" 0x581810 --format json > "$OUT/dc_setmode.json" 2>&1 &
wait; echo ALL_DONE
