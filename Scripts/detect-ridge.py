#!/usr/bin/env python3
"""Count thin bright ridges (the flickering white line) per frame.

A thin bright line on the road raises a pixel well above a local horizontal
average, while a wide road marking lifts the average with it. So the score is
the number of pixels whose luminance exceeds a 9-pixel horizontal box average by
a large margin - a line detector that ignores broad white areas.

Usage:
  python Scripts/detect-ridge.py <dir> [<dir2> ...] [--thresh 55]
"""
import sys
import glob
import os
from PIL import Image

RADIUS = 4


def score(im, thresh):
    w, h = im.size
    px = im.load()
    lum = []
    for y in range(h):
        lum.append([0.299 * px[x, y][0] + 0.587 * px[x, y][1] + 0.114 * px[x, y][2]
                    for x in range(w)])
    hits = 0
    best = 0
    bestpos = None
    for y in range(0, h, 2):
        row = lum[y]
        pre = [0.0]
        for v in row:
            pre.append(pre[-1] + v)
        for x in range(0, w, 2):
            x0 = max(0, x - RADIUS)
            x1 = min(w, x + RADIUS + 1)
            avg = (pre[x1] - pre[x0]) / (x1 - x0)
            d = row[x] - avg
            if d >= thresh and row[x] >= 90:
                hits += 1
                if d > best:
                    best = d
                    bestpos = (x, y)
    return hits, best, bestpos


def main():
    thresh = 55.0
    if "--thresh" in sys.argv:
        thresh = float(sys.argv[sys.argv.index("--thresh") + 1])
    dirs = [a for a in sys.argv[1:] if not a.startswith("--")]
    for d in dirs:
        files = sorted(glob.glob(os.path.join(d, "*.jpg"))) + \
            sorted(glob.glob(os.path.join(d, "*.png")))
        if not files:
            continue
        totals = []
        for f in files:
            im = Image.open(f).convert("RGB")
            hits, best, pos = score(im, thresh)
            totals.append((hits, best, pos, os.path.basename(f)))
        totals.sort(key=lambda t: -t[0])
        mean = sum(t[0] for t in totals) / float(len(totals))
        nonzero = sum(1 for t in totals if t[0] > 0)
        print("== %s  frames %d  mean ridge px %.1f  frames with any %d  (%.0f%%)"
              % (os.path.basename(d), len(totals), mean, nonzero,
                 100.0 * nonzero / len(totals)))
        for hits, best, pos, name in totals[:5]:
            print("   %s  ridge px %d  peak %.0f at %s" % (name, hits, best, pos))


if __name__ == "__main__":
    main()
