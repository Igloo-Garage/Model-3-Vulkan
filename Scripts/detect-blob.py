#!/usr/bin/env python3
"""Find compact near-white blobs (the ground artifact) with connected components.

The road is dark and the only large white things are scenery, so a *compact*
near-white blob inside the road band is the artifact. Components are found on a
4x-decimated grid and reported with their exact bounding box and mean colour.

Usage:
  python Scripts/detect-blob.py sdl3_reports/images/burst-blob [--step 4] [--every 2]
"""
import sys
import glob
import os
from PIL import Image

STEP = 4
EVERY = 2
MIN_AREA = 150      # in downsampled cells
MAX_AREA = 4000


def components(cells, w, h):
    """cells: set of (x,y) in grid space. Returns list of (bbox, area)."""
    seen = set()
    out = []
    for start in cells:
        if start in seen:
            continue
        stack = [start]
        seen.add(start)
        minx = maxx = start[0]
        miny = maxy = start[1]
        area = 0
        while stack:
            x, y = stack.pop()
            area += 1
            if x < minx: minx = x
            if x > maxx: maxx = x
            if y < miny: miny = y
            if y > maxy: maxy = y
            for nx, ny in ((x+1, y), (x-1, y), (x, y+1), (x, y-1)):
                if 0 <= nx < w and 0 <= ny < h and (nx, ny) not in seen and (nx, ny) in cells:
                    seen.add((nx, ny))
                    stack.append((nx, ny))
        out.append(((minx, miny, maxx, maxy), area))
    return out


def main():
    d = sys.argv[1]
    step = STEP
    every = EVERY
    if "--step" in sys.argv:
        step = int(sys.argv[sys.argv.index("--step") + 1])
    if "--every" in sys.argv:
        every = int(sys.argv[sys.argv.index("--every") + 1])

    files = sorted(glob.glob(os.path.join(d, "*.jpg"))) + \
        sorted(glob.glob(os.path.join(d, "*.png")))
    files = files[::every]
    if not files:
        print("no images")
        return

    hits = 0
    for f in files:
        im = Image.open(f).convert("RGB")
        W, H = im.size
        px = im.load()
        gw, gh = W // step, H // step
        cells = set()
        for gy in range(gh):
            y = gy * step
            for gx in range(gw):
                x = gx * step
                r, g, b = px[x, y]
                if (r + g + b) / 3.0 >= 195 and (max(r, g, b) - min(r, g, b)) <= 40:
                    cells.add((gx, gy))
        comps = components(cells, gw, gh)
        interesting = []
        for (minx, miny, maxx, maxy), area in comps:
            if area < MIN_AREA or area > MAX_AREA:
                continue
            bw = maxx - minx + 1
            bh = maxy - miny + 1
            fill = area / float(bw * bh)
            # a quad is filled; a line is thin and very wide
            interesting.append((minx * step, miny * step, maxx * step, maxy * step,
                                bw * step, bh * step, area * step * step, fill))
        if interesting:
            hits += 1
            interesting.sort(key=lambda t: -t[6])
            print(os.path.basename(f))
            for t in interesting[:4]:
                print("   bbox (%d,%d)-(%d,%d) %dx%d area %d fill %.2f" %
                      (t[0], t[1], t[2], t[3], t[4], t[5], t[6], t[7]))
    print("frames with a compact blob: %d / %d" % (hits, len(files)))


if __name__ == "__main__":
    main()
