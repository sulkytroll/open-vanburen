#!/usr/bin/env python3
"""Reversible patch of the Van Buren tech demo's F3.exe: "Start New Game" skips character creation.

The main menu's NewGameBtn handler (FUN_00440ce0 in the Ghidra listing, VA 0x440d6b) asks GetAsyncKeyState(VK_SHIFT):
Shift held  -> console command "RequestNewGame" (the debug "New Game (dbg)": default character, straight into the start map);
Shift not held -> opens the character creation windows.
The patch replaces the 2-byte `JNS +0x64` at VA 0x440d75 (file offset 0x40d75) with two NOPs, so the Shift branch is always taken.

    patch_f3_newgame.py apply  [F3.exe]    patch (keeps F3.exe.orig next to it)
    patch_f3_newgame.py revert [F3.exe]    restore the original bytes (and remove nothing else)
    patch_f3_newgame.py status [F3.exe]
"""
import hashlib, os, shutil, sys

DEFAULT = "F3.exe"
OFFSET, ORIG, PATCHED = 0x40d75, bytes([0x79, 0x64]), bytes([0x90, 0x90])
ORIG_SHA256 = "4652a3d03e7e09facab0ea123568a36c9be3b44ada80c1f60dac0449b3579b87"

def sha(b): return hashlib.sha256(b).hexdigest()

def main():
    if len(sys.argv) < 2 or sys.argv[1] not in ("apply", "revert", "status"): print(__doc__); return 2
    cmd, path = sys.argv[1], (sys.argv[2] if len(sys.argv) > 2 else DEFAULT)
    data = bytearray(open(path, "rb").read()); cur = bytes(data[OFFSET:OFFSET + 2]); backup = path + ".orig"
    state = "original" if cur == ORIG else "patched" if cur == PATCHED else "unknown"
    if cmd == "status":
        print(f"{path}: {state} (bytes at 0x{OFFSET:x}: {cur.hex(' ')}); backup {'present' if os.path.exists(backup) else 'absent'}"); return 0
    if cmd == "apply":
        if state == "patched": print("already patched"); return 0
        if state != "original" or sha(bytes(data)) != ORIG_SHA256: print("refusing: this is not the untouched tech demo F3.exe"); return 1
        if not os.path.exists(backup): shutil.copy2(path, backup)
        data[OFFSET:OFFSET + 2] = PATCHED; open(path, "wb").write(data); print(f"patched; backup kept as {backup}"); return 0
    if state == "original": print("already original"); return 0      # revert
    if state != "patched": print("refusing: unexpected bytes at the patch site"); return 1
    data[OFFSET:OFFSET + 2] = ORIG; open(path, "wb").write(data)
    print("reverted;", "matches the original SHA-256" if sha(bytes(data)) == ORIG_SHA256 else "NOTE: the rest of the file differs from the original SHA-256 (other changes present)"); return 0

if __name__ == "__main__": sys.exit(main())
