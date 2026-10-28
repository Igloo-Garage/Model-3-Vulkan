#!/usr/bin/env python3
"""Temporal-diff a capture burst to find where a flickering line/block lives.

A flickering artifact appears in one frame and is gone in the next, so the
per-pixel difference between consecutive frames concentrates where it flashed.
For each pair this prints the rows/columns whose total difference is an outlier,
plus the colour at the hottest pixel - enough to know where and what colour the
line is without seeing the image.

Usage:
  python Scripts/detect-flicker.py sdl3_reports/images/burst-road
"""
import sys
import glob
import os
from PIL import Image


def main():
    d = sys.argv[1]
    files = sorted(glob.glob(os.path.join(d, "*.jpg"))) + \
        sorted(glob.glob(os.path.join(d, "*.png")))
    if len(files) < 2:
        print("need >=2 images")
        return

    prev = Image.open(files[0]).convert("RGB")
    for i in range(1, len(files)):
        cur = Image.open(files[i]).convert("RGB")
        w, h = prev.size
        p0, p1 = prev.load(), cur.load()
        rowsum = [0] * h
        colsum = [0] * w
        hot = (0, 0, 0)
        hotv = -1
        for y in range(h):
            for x in range(w):
                r0, g0, b0 = p0[x, y]
                r1, g1, b1 = p1[x, y]
                d = abs(r0 - r1) + abs(g0 - g1) + abs(b0 - b1)
                if d > 0:
                    rowsum[y] += d
                    colsum[x] += d
                    if d > hotv:
                        hotv = d
                        hot = (r1, g1, b1)
        # a real flash concentrates in one row band or column band; report the
        # top outliers and whether they dominate their neighbourhood
        med_row = sorted(rowsum)[h // 2]
        med_col = sorted(colsum)[w // 2]
        row_out = [(y, rowsum[y]) for y in range(h) if rowsum[y] > med_row * 6 + 400]
        col_out = [(x, colsum[x]) for x in range(w) if colsum[x] > med_col * 6 + 400]
        if row_out or col_out:
            print(f"{os.path.basename(files[i])}: rows {row_out[:8]}  cols {col_out[:8]}  "
                  f"hotpix {hot} (diff {hotv})  medrow {med_row} medcol {med_col}")
        prev = cur


if __name__ == "__main__":
    main()
