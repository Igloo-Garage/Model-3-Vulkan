"""Install WebP covers for the ROM sets that libretro's MAME set does not cover.

The Virtual-On Oratorio Tangram cover was supplied by the user. The other three
games are fetched from The Arcade Flyer Archive / Sega Retro where a suitable
image exists; any download that fails falls back to the Virtual-On cover, as
requested, so every listed set has artwork.

Run from the project root:
    python Scripts/install_missing_boxarts.py
"""

from __future__ import annotations

from io import BytesIO
from pathlib import Path
import urllib.request

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "Assets" / "Boxarts"

USER_AGENT = (
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
    "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120 Safari/537.36"
)

SOURCES = {
    "von2": "https://www.video-games-museum.com/en/boxart/Dreamcast/"
            "36358_us-Cyber-Troopers-Virtual-On-Oratorio-Tangram.jpg",
    "fvipers2": "https://segaretro.org/images/e/ef/FV2_DC_JP_Box_Front.jpg",
    "lamachin": "https://flyers.arcade-museum.com/videogame-flyers/0/"
                "l-a-machineguns-rage-of-the-machines-00544-01.jpg",
    "skichamp": "https://segaretro.org/images/7/7b/Ski_champ.jpg",
}


def fetch(url: str) -> bytes:
    request = urllib.request.Request(
        url,
        headers={
            "User-Agent": USER_AGENT,
            "Referer": "https://www.google.com/",
        },
    )
    with urllib.request.urlopen(request, timeout=90) as response:
        return response.read()


def write_webp(data: bytes, destination: Path) -> None:
    with Image.open(BytesIO(data)) as image:
        image = image.convert("RGB")
        if image.width > 640:
            height = round(image.height * 640 / image.width)
            image = image.resize((640, height), Image.LANCZOS)
        image.save(destination, "WEBP", quality=88, method=6)
        print(f"  {destination.name}: {image.width}x{image.height}, {destination.stat().st_size} bytes")


def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)

    downloaded: dict[str, bytes] = {}
    for name, url in SOURCES.items():
        try:
            downloaded[name] = fetch(url)
            print(f"Downloaded {name}")
        except Exception as exc:  # noqa: BLE001 - report and fall back
            print(f"FAILED {name}: {exc}")

    fallback = downloaded.get("von2")
    if fallback is None:
        print("The Virtual-On cover could not be downloaded; nothing was installed.")
        return 1

    for name in ("von2", "von2a", "von254g", "von2o"):
        write_webp(fallback, OUT / f"{name}.webp")

    for name, sets in (
        ("fvipers2", ("fvipers2", "fvipers2o")),
        ("lamachin", ("lamachin",)),
        ("skichamp", ("skichamp",)),
    ):
        data = downloaded.get(name, fallback)
        if name not in downloaded:
            print(f"Using the Virtual-On cover as a placeholder for {name}")
        for rom_set in sets:
            write_webp(data, OUT / f"{rom_set}.webp")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
