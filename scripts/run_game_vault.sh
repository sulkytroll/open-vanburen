#!/bin/bash
# Launch the game (vanburen) straight in the Vault (04_Tutorial_Vault). Usage: scripts/run_game_vault.sh [options]
exec "$(dirname "$0")/run_game.sh" 04_Tutorial_Vault "$@"
