#!/usr/bin/env python3
"""G3D header analysis - dump raw bytes after magic."""

import struct
import subprocess
from pathlib import Path

G3D_DIR = Path("assets")

result = subprocess.run(["find", str(G3D_DIR), "-name", "*.g3d"], capture_output=True, text=True)
g3d_files = [Path(p) for p in result.stdout.strip().split('\n') if p]
print(f"Analyzing {len(g3d_files)} G3D files...\n")

# For each file: dump first 48 bytes as hex, then scan for first valid tag
def analyze(path):
    with open(path, 'rb') as f:
        data = f.read(48)
    
    name = path.name
    
    # After B3D 1.1  (8 bytes), what are bytes 8-16?
    after_magic = list(data[8:24]) if len(data) >= 24 else []
    
    # Scan for first tag byte in 0x07..0x0F range after offset ~16
    first_tag = None
    for i in range(16, min(len(data), 64)):
        if 0x07 <= data[i] <= 0x0F:
            first_tag = i
            break
    
    return name, after_magic, first_tag

# Analyze all files, group by prefix pattern
prefixes = {}
for path in g3d_files:
    name, after_magic, first_tag = analyze(path)
    # Use first 3 bytes after magic as prefix key
    key = tuple(after_magic[:3]) if len(after_magic) >= 3 else tuple(after_magic)
    if key not in prefixes:
        prefixes[key] = []
    prefixes[key].append((name, first_tag, after_magic[:6]))

print(f"Found {len(prefixes)} distinct prefix patterns\n")

# Sort by frequency
sorted_prefixes = sorted(prefixes.items(), key=lambda x: -len(x[1]))

for prefix, entries in sorted_prefixes[:15]:
    prefix_hex = ' '.join(f'{b:02x}' for b in prefix[:3])
    n = len(entries)
    tag_offsets = sorted(set(t for _, t, _ in entries if t is not None))
    tags_str = ','.join(str(t) for t in tag_offsets[:5])
    # Show a few examples
    examples = entries[:3]
    print(f"  prefix [{prefix_hex}]  ({n} files)  tag_offsets=[{tags_str}]")
    for name, _, after in examples:
        after_hex = ' '.join(f'{b:02x}' for b in after)
        print(f"    {name:50s} after={after_hex}")
