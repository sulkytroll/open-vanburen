#!/bin/bash
REA="$(dirname "$0")/rea.sh"
F3="${F3_EXE:?set F3_EXE to the tech demo's F3.exe}"
OUT="${REA_OUT:-./rea_out}"; mkdir -p "$OUT"
for kv in MATD:4d415444 VTXD:56545844 LEAF:4c454146 EMAP:454d4150 EEOV:45454f56 EME2:454d4532 8TRE:38545245 NODE:4e4f4445 ER01:45523031 EST2:45535432 IDXD:49445844; do
  k=${kv%%:*}; v=${kv##*:}
  "$REA" search-bytes "$F3" $v --format json > "$OUT/sb_$k.json" 2>&1 &
done
wait; echo ALL_DONE
