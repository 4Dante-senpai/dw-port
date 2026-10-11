#!/usr/bin/env python3
"""Candidate PS1 RAM pointers embedded in the extracted disc files.

Usage: tools/port/metrics/pointer_scan.py [DISK_DIR]   (default disks/us)

Counts 4-byte aligned little-endian words in 0x80010000-0x801FFFFF per file. Random data hits
that range with probability ~2^-11 per word, so the "density" column (hits per 1000 words) tells
real pointer tables (density well above ~0.5) from noise. These are candidates (hypotheses) to be
confirmed against the code that reads each format. Prints only aggregated numbers, no file data.
"""
import collections
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
LO, HI = 0x80010000, 0x80200000


def main():
    disk = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "disks", "us")
    per_dir = collections.defaultdict(lambda: [0, 0, 0, 0])  # files, files_with_hits, words, hits
    rows = []
    for dirpath, _, files in os.walk(disk):
        for fn in files:
            path = os.path.join(dirpath, fn)
            with open(path, "rb") as f:
                data = f.read()
            n = len(data) // 4
            if n == 0:
                continue
            hits = sum(1 for (w,) in struct.iter_unpack("<I", data[: n * 4]) if LO <= w < HI)
            rel = os.path.relpath(path, disk)
            top = rel.split(os.sep)[0] if os.sep in rel else "(raíz)"
            d = per_dir[top]
            d[0] += 1
            d[1] += hits > 0
            d[2] += n
            d[3] += hits
            rows.append((1000.0 * hits / n, hits, n, rel))
    print("| Directorio | Archivos | Archivos con candidatos | Palabras | Candidatos | Densidad (/1000) |")
    print("|---|---:|---:|---:|---:|---:|")
    for top, (nf, nh, nw, hits) in sorted(per_dir.items()):
        print(f"| {top} | {nf} | {nh} | {nw} | {hits} | {1000.0 * hits / nw:.2f} |")
    print("\nArchivos con mayor densidad (>= 16 candidatos):\n")
    print("| Archivo | Candidatos | Palabras | Densidad (/1000) |")
    print("|---|---:|---:|---:|")
    for dens, hits, n, rel in sorted((r for r in rows if r[1] >= 16), reverse=True)[:25]:
        print(f"| `{rel}` | {hits} | {n} | {dens:.2f} |")


if __name__ == "__main__":
    main()
