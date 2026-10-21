#!/usr/bin/env python3
"""
Download libretro MAME boxarts for the ROM sets in Config/Games.xml.

The thumbnails repository stores MAME boxarts under descriptive names
("Daytona USA 2 (Revision A).png"), not MAME short names, so each of our set
names is mapped to the file that matches its title and version below. Clones
with no artwork of their own reuse the closest parent variant.

The downloaded files are named after our ROM set, which is what the emulator
would look up. SOURCES.txt records the original libretro filename for each one
so the mapping can be checked or refreshed later.

libretro serves PNGs. With Pillow installed the download is converted to WebP
(about a quarter of the size, and the format the emulator and build expect);
without Pillow the PNGs are left in place and are not packaged.

Usage:
    python Scripts/fetch_boxarts.py
"""

from __future__ import annotations

import sys
import time
import urllib.parse
import urllib.request
from pathlib import Path

try:
    from PIL import Image
except ImportError:  # Pillow is only needed to convert the download to WebP.
    Image = None

ROOT = Path(__file__).resolve().parent.parent

# libretro's path for this collection. The repository is large; only the files
# named here are fetched.
RAW_BASE = (
    "https://raw.githubusercontent.com/libretro/libretro-thumbnails/"
    "master/MAME/Named_Boxarts/"
)

# Our set name -> the name used in MAME/Named_Boxarts.
# None means the collection has no boxart for that game yet.
BOXARTS = {
    "bassdx": "Sega Bass Fishing.png",
    "getbassdx": "Sega Bass Fishing Deluxe (Japan).png",
    "getbassur": "Get Bass.png",
    "getbass": "Get Bass.png",
    "daytona2": "Daytona USA 2 (Revision A).png",
    "dayto2pe": "Daytona USA 2 Power Edition.png",
    "dirtdvls": "Dirt Devils (Export, Revision A).png",
    "dirtdvlsu": "Dirt Devils (Revision A).png",
    "dirtdvlsau": "Dirt Devils (Australia, Revision A).png",
    "dirtdvlsj": "Dirt Devils (Revision A).png",
    "dirtdvlsg": "Dirt Devils (Export, Revision A).png",
    "eca": "Emergency Call Ambulance (Export).png",
    "ecap": "Emergency Call Ambulance (USA).png",
    "ecau": "Emergency Call Ambulance (USA).png",
    "ecaj": "Emergency Call Ambulance.png",
    "fvipers2": None,
    "fvipers2o": None,
    "harley": "Harley-Davidson and L.A. Riders (Revision B).png",
    "harleya": "Harley-Davidson and L.A. Riders (Revision A).png",
    "lamachin": None,
    "lemans24": "Le Mans 24 (Revision B).png",
    "lostwsga": "The Lost World (Japan, Revision A).png",
    "lostwsgp": "The Lost World.png",
    "magtruck": "Magical Truck Adventure (Japan).png",
    "mgtrkbad": "Magical Truck Adventure.png",
    "oceanhun": "The Ocean Hunter.png",
    "oceanhuna": "The Ocean Hunter.png",
    "scud": "Scud Race (Export).png",
    "scudau": "Scud Race (Australia).png",
    "scuddx": "Scud Race (Export).png",
    "scuddxo": "Scud Race (Export).png",
    "scudplus": "Scud Race Plus (Revision A).png",
    "scudplusa": "Scud Race Plus (Revision A).png",
    "skichamp": None,
    "spikeofe": "Spikeout Final Edition.png",
    "spikeout": "Spikeout (Revision C).png",
    "srally2": "Sega Rally 2.png",
    "srally2p": "Sega Rally 2.png",
    "srally2pa": "Sega Rally 2.png",
    "srally2dx": "Sega Rally 2 DX.png",
    "swtrilgy": "Star Wars Trilogy (Revision A).png",
    "swtrilgya": "Star Wars Trilogy.png",
    "swtrilgyp": "Star Wars Trilogy.png",
    "vf3": "Virtua Fighter 3 (Revision C).png",
    "vf3a": "Virtua Fighter 3 (Revision C).png",
    "vf3c": "Virtua Fighter 3 (Revision C).png",
    "vf3tb": "Virtua Fighter 3 Team Battle.png",
    "von2": None,
    "von254g": None,
    "von2a": None,
    "von2o": None,
    "vs2": "Virtua Striker 2 (Step 2.0).png",
    "vs215": "Virtua Striker 2 (Step 1.5).png",
    "vs215o": "Virtua Striker 2 (Step 1.5).png",
    "vs298": "Virtua Striker 2 '98 (Step 2.0).png",
    "vs29815": "Virtua Striker 2 '98 (Step 1.5).png",
    "vs2v991": "Virtua Striker 2 '99.1 (Revision B).png",
    "vs299a": "Virtua Striker 2 '99.png",
    "vs299": "Virtua Striker 2 '99.png",
    "vs299j": "Virtua Striker 2 '99.1 (Revision B).png",
    "vs29915": "Virtua Striker 2 '99.1 (Revision B).png",
    "vs29915a": "Virtua Striker 2 '99.png",
    "vs29915j": "Virtua Striker 2 '99.1 (Revision B).png",
}


def fetch(url: str, destination: Path) -> None:
    request = urllib.request.Request(
        url,
        headers={"User-Agent": "Supermodel-VK boxart fetch"},
    )
    with urllib.request.urlopen(request, timeout=60) as response:
        destination.write_bytes(response.read())


def main() -> int:
    out_dir = ROOT / "Assets" / "Boxarts"
    out_dir.mkdir(parents=True, exist_ok=True)

    downloaded = []
    missing = []
    failed = []

    for set_name, libretro_name in BOXARTS.items():
        if libretro_name is None:
            missing.append(set_name)
            continue

        url = RAW_BASE + urllib.parse.quote(libretro_name)
        png_path = out_dir / f"{set_name}.png"
        try:
            fetch(url, png_path)
            if Image is None:
                saved_name = png_path.name
            else:
                webp_path = out_dir / f"{set_name}.webp"
                with Image.open(png_path) as image:
                    image.save(webp_path, "WEBP", quality=88, method=6)
                png_path.unlink()
                saved_name = webp_path.name
        except Exception as exc:  # noqa: BLE001 - report and continue
            failed.append((set_name, libretro_name, str(exc)))
            print(f"FAILED {set_name}: {exc}", file=sys.stderr)
        else:
            downloaded.append((set_name, saved_name, libretro_name))
            print(f"  ok  {saved_name}  <- {libretro_name}")
            time.sleep(0.05)

    with (out_dir / "SOURCES.txt").open("w", encoding="utf-8") as sources:
        sources.write(
            "Boxarts downloaded from libretro/libretro-thumbnails,\n"
            "MAME/Named_Boxarts. Files are named after Supermodel-VK ROM sets.\n\n"
        )
        for set_name, saved_name, libretro_name in downloaded:
            sources.write(f"{saved_name} = {libretro_name}\n")
        if missing:
            sources.write("\nNo boxart in that collection for:\n")
            for set_name in missing:
                sources.write(f"{set_name}\n")

    print()
    if Image is None and downloaded:
        print("Pillow not found: left the downloads as PNG, which the game list does not read.")
    print(f"Downloaded {len(downloaded)} boxarts to {out_dir}")
    if missing:
        print("No artwork available for: " + ", ".join(missing))
    if failed:
        print(f"{len(failed)} download(s) failed.")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
