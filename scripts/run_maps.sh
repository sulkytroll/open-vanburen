#!/bin/bash
# Launch the standalone map viewer / selector (vb_map; vanburen has the same as its Map Browser). Usage: scripts/run_maps.sh [mapname] [vb_map options]
# Builds on first use. Reads the tech demo folder (VB_GAME_DIR, or -game DIR).
set -e
cd "$(dirname "$0")/.."
[ -x build/vb_map ] || { cmake -S . -B build >/dev/null && cmake --build build -j"$(nproc)" --target vb_map; }
[ -n "$VB_GAME_DIR" ] || [ -d ../F3_Demo ] && export VB_GAME_DIR="${VB_GAME_DIR:-$PWD/../F3_Demo}"
exec env DISPLAY="${DISPLAY:-:0}" ./build/vb_map "$@"
