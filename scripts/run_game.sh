#!/bin/bash
# Launch the game (vanburen). Usage: scripts/run_game.sh [mapname] [options]   (see tools/vanburen/main.cpp)
# Builds on first use. Reads the tech demo folder directly: set VB_GAME_DIR to it (or pass -game DIR); without either, a sibling folder named F3_Demo is tried.
set -e
cd "$(dirname "$0")/.."
[ -x build/vanburen ] || { cmake -S . -B build >/dev/null && cmake --build build -j"$(nproc)" --target vanburen; }
[ -n "$VB_GAME_DIR" ] || [ -d ../F3_Demo ] && export VB_GAME_DIR="${VB_GAME_DIR:-$PWD/../F3_Demo}"
exec env DISPLAY="${DISPLAY:-:0}" ./build/vanburen "$@"
