#!/bin/bash
# Launch the standalone model viewer / selector (vb_model; vanburen has a simpler one as its Object Browser). Usage: scripts/run_models.sh [name|file.g3d] [vb_model options]
# Builds on first use. Reads the tech demo folder (VB_GAME_DIR, or -game DIR).
set -e
cd "$(dirname "$0")/.."
[ -x build/vb_model ] || { cmake -S . -B build >/dev/null && cmake --build build -j"$(nproc)" --target vb_model; }
[ -n "$VB_GAME_DIR" ] || [ -d ../F3_Demo ] && export VB_GAME_DIR="${VB_GAME_DIR:-$PWD/../F3_Demo}"
exec env DISPLAY="${DISPLAY:-:0}" ./build/vb_model "$@"
