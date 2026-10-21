"""Build a native-vs-widescreen comparison for the README.

GitHub does not run JavaScript, so an in-image click-to-toggle slider is not possible there.
This script produces the three things that do work:

    compare-aspect.png   native on top, widescreen below, each labelled
    compare-aspect.gif   the two alternate automatically (works everywhere)
    compare-aspect.webp  the same animation at higher quality (modern browsers)

Usage, from the project root:

    python Scripts/make_aspect_comparison.py native.png widescreen.png
    python Scripts/make_aspect_comparison.py native.png widescreen.png --out Docs/Images

Both images keep their own aspect ratio and are scaled to the same width, so the height
difference in the stack is the aspect-ratio difference.
"""

from __future__ import annotations

import argparse
from math import gcd
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

BACKGROUND = (16, 16, 16)
FOREGROUND = (240, 240, 240)
HEADER = 64
GAP = 28
FRAME_MS = 1800


def load_font(size: int) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    for path in (
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    ):
        try:
            return ImageFont.truetype(path, size)
        except OSError:
            continue
    return ImageFont.load_default()


def resolution(image: Image.Image) -> str:
    return f"{image.width} x {image.height}"


def describe(image: Image.Image) -> str:
    divisor = gcd(image.width, image.height)
    return f"{resolution(image)}  ({image.width // divisor}:{image.height // divisor})"


def scale_to_width(image: Image.Image, width: int) -> Image.Image:
    height = round(image.height * width / image.width)
    return image.resize((width, height), Image.LANCZOS)


def add_label(image: Image.Image, text: str) -> Image.Image:
    canvas = Image.new("RGB", (image.width, image.height + HEADER), BACKGROUND)
    draw = ImageDraw.Draw(canvas)
    draw.text((28, 16), text, font=load_font(32), fill=FOREGROUND)
    canvas.paste(image, (0, HEADER))
    return canvas


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("native", type=Path, help="4:3 / native-aspect screenshot")
    parser.add_argument("widescreen", type=Path, help="widescreen screenshot")
    parser.add_argument("--out", type=Path, default=Path("Docs") / "Images")
    parser.add_argument("--width", type=int, default=1400, help="output width in pixels")
    parser.add_argument("--native-label", default="", help="override the top label")
    parser.add_argument("--wide-label", default="", help="override the bottom label")
    args = parser.parse_args()

    native = Image.open(args.native).convert("RGB")
    wide = Image.open(args.widescreen).convert("RGB")
    args.out.mkdir(parents=True, exist_ok=True)

    frames = [
        add_label(
            scale_to_width(native, args.width),
            args.native_label or f"Native - {resolution(native)}",
        ),
        add_label(
            scale_to_width(wide, args.width),
            args.wide_label or f"Widescreen - {resolution(wide)}",
        ),
    ]

    stacked = Image.new("RGB", (args.width, sum(f.height for f in frames) + GAP), BACKGROUND)
    y = 0
    for frame in frames:
        stacked.paste(frame, (0, y))
        y += frame.height + GAP
    stacked_path = args.out / "compare-aspect.png"
    stacked.save(stacked_path, optimize=True)

    # Animated frames must share one canvas size, and the two aspect ratios give different
    # heights at a common width. Top-align them instead of stretching either picture.
    anim_width = max(frame.width for frame in frames)
    anim_height = max(frame.height for frame in frames)
    anim_frames = []
    for frame in frames:
        canvas = Image.new("RGB", (anim_width, anim_height), BACKGROUND)
        canvas.paste(frame, (0, 0))
        anim_frames.append(canvas)

    anim_frames[0].save(
        args.out / "compare-aspect.gif",
        save_all=True,
        append_images=anim_frames[1:],
        duration=FRAME_MS,
        loop=0,
    )
    anim_frames[0].save(
        args.out / "compare-aspect.webp",
        save_all=True,
        append_images=anim_frames[1:],
        duration=FRAME_MS,
        loop=0,
        quality=90,
        method=6,
    )

    print(f"native     : {describe(native)}")
    print(f"widescreen : {describe(wide)}")
    print(f"stacked    : {stacked_path} ({stacked.width}x{stacked.height})")
    print(f"animation  : {args.out / 'compare-aspect.gif'}")
    print(f"animation  : {args.out / 'compare-aspect.webp'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
