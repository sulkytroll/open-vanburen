#!/bin/bash
# Runs the `rea` reverse-engineering CLI (https://www.npmjs.com/package/rea-agents) with Ghidra, which it needs through GHIDRA_INSTALL_DIR.
#   GHIDRA_INSTALL_DIR  Ghidra's install folder (default: ~/ghidra)
#   REA_CLI             how to start rea (default: `rea`; e.g. "npx -y rea-agents" or "node /path/to/rea.mjs")
# Every call re-imports and re-analyses the binary (about two minutes), so the rea_*.sh batch scripts start several calls in parallel.
exec env GHIDRA_INSTALL_DIR="${GHIDRA_INSTALL_DIR:-$HOME/ghidra}" ${REA_CLI:-rea} "$@"
