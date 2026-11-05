#!/usr/bin/env python3
"""Run vb_g3d_validate over the full G3D corpus in batches; aggregate per-category.
Usage: python3 run_corpus.py"""
import subprocess, os, collections

ASSETS = "assets"
BIN = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "vb_g3d_validate")

# collect all .g3d
files = []
for dp, _, fs in os.walk(ASSETS):
    for f in fs:
        if f.lower().endswith(".g3d"):
            files.append(os.path.join(dp, f))
files.sort()
print(f"total g3d: {len(files)}")

def category(path):
    rel = os.path.relpath(path, ASSETS)
    parts = rel.split(os.sep)
    # category = first dir under assets that's not a file; e.g. Items, Props, Tiles, Critters, Effects
    return parts[0] if len(parts) > 1 else "(root)"

# collect all .g3d grouped by immediate subdirectory of ASSETS
by_dir = collections.defaultdict(list)
for dp, _, fs in os.walk(ASSETS):
    for f in fs:
        if f.lower().endswith(".g3d"):
            by_dir[dp].append(os.path.join(dp, f))
dirs = sorted(by_dir)
total_files = sum(len(v) for v in by_dir.values())
print(f"total g3d: {total_files} across {len(dirs)} dirs")

ok_by_cat = collections.Counter()
tot_by_cat = collections.Counter()
fail_samples = collections.defaultdict(list)
all_ok = 0; all_tot = 0

def parse_out(out):
    global all_ok, all_tot
    for line in out.splitlines():
        if line.startswith("[OK] ") or line.startswith("[FAIL] "):
            status = line[1:3]
            body = line.split(" ", 1)[1]
            p = body.split(": ", 1)[0].rstrip(":").strip()
            cat = category(p)
            tot_by_cat[cat] += 1
            all_tot += 1
            if status == "OK":
                ok_by_cat[cat] += 1
                all_ok += 1
            else:
                fail_samples[cat].append(p)

for dp in dirs:
    # The validator takes ONE target: a file or a directory (non-recursive
    # list of .g3d in that dir). Call it once per directory.
    r = subprocess.run([BIN, dp], capture_output=True, text=True)
    parse_out(r.stdout)

print("\n=== PER-CATEGORY ===")
print(f"{'category':<15} {'ok':>6} {'tot':>6} {'rate':>8}")
for cat in sorted(tot_by_cat, key=lambda c: -tot_by_cat[c]):
    o, t = ok_by_cat[cat], tot_by_cat[cat]
    print(f"{cat:<15} {o:>6} {t:>6} {100.0*o/t:>7.1f}%")
print(f"{'TOTAL':<15} {all_ok:>6} {all_tot:>6} {100.0*all_ok/all_tot:>7.1f}%")

# show a few failure samples per category
for cat in sorted(fail_samples):
    print(f"\n{cat}: {len(fail_samples[cat])} failures, e.g.")
    for p in fail_samples[cat][:3]:
        print(f"   {p}")
