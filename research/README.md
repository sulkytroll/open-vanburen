# Research scripts

One-off tools that were used to work out the file formats and the engine's behaviour. They are kept for reference and are **not** part of the build; the findings they
produced are in [../docs/](../docs/), and the C++ code in `src/assets/` is the maintained implementation.

* `*_parse.py`, `walk*.py`, `probe_*.py`, `debug_*.py`, `find_*.py`, `mesh_struct.py`, `map_*.py`, `tail_dump.py`, `brute_stride.py`: format explorers (G3D, octree, map, GUI, VFX, AMX, Granny2).
* `verify_*.py`, `run_corpus.py`, `gr2_verify_oodle0.py`: byte-exact verifiers over the shipped corpus.
* `gr2_oodle0_emu.py`: a Python version of the Granny2 Oodle0 decompressor.
* `patch_f3_newgame.py`: patches a copy of the *original* `F3.exe` so that Start New Game skips character creation (reversible; it only edits two bytes).
* `ghidra/`: scripts that drive the [`rea`](https://www.npmjs.com/package/rea-agents) reverse-engineering CLI with Ghidra to decompile `F3.exe` (set `GHIDRA_INSTALL_DIR`, `REA_CLI`, `F3_EXE`).

Most of the Python scripts expect to be run from a folder that holds `assets/`, the output of `build/vb_extract`, and import each other from this folder.
Some need `granny2.dll`, which is proprietary and not included.
