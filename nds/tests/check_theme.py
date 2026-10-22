"""Check rendered UI colors, scroll state and the exact icon in the NDS banner.

Usage: python check_theme.py UI_IMAGES ROM.nds
"""
from pathlib import Path
import struct
import sys

from PIL import Image

images, nds = Path(sys.argv[1]), Path(sys.argv[2])
browser = Image.open(images / "browser.png").convert("RGB")
scrolled = Image.open(images / "scrolled.png").convert("RGB")
assert browser.size == scrolled.size == (256, 384)
assert browser.crop((0, 32, 256, 176)).tobytes() != scrolled.crop((0, 32, 256, 176)).tobytes(), "List did not scroll"
assert browser.getpixel((5, 85)) != browser.getpixel((0, 85)), "Selected game highlight is missing"
r, g, b = browser.getpixel((12, 364))
assert r > 80 and g > 60 and b > 180, "Purple download button is missing"
cover = browser.crop((8, 224, 96, 312))
assert len(set(cover.get_flattened_data())) > 900, "Native 88px RGB555 cover is missing"
assert all(b > 180 for _, _, b in cover.get_flattened_data()), "Cover sprite pieces have a gap or a bad edge"
gba = Image.open(images / "gba.png").convert("RGB")
assert gba.crop((0, 192, 256, 216)).tobytes() != browser.crop((0, 192, 256, 216)).tobytes(), "Tab strip did not slide from NDS/GBA to GBA/Others"
assert gba.getpixel((12, 196)) != gba.getpixel((140, 196)), "GBA tab did not become active on the left"
queue = Image.open(images / "queue.png").convert("RGB")
assert queue.getpixel((8, 60)) != queue.getpixel((0, 60)), "Queue selection is missing"
download = Image.open(images / "download.png").convert("RGB")
assert download.getpixel((30, 335)) != download.getpixel((160, 335)), "Progress bar is missing"
assert len(set(download.crop((100, 300, 158, 321)).get_flattened_data())) > 1, "Large percentage is missing"

data = nds.read_bytes()
assert data[:12].rstrip(b"\0") == b"DSiDrop", "Wrong NDS header title"
banner = struct.unpack_from("<I", data, 0x68)[0]
for language in range(6):
    start = banner + 0x240 + language * 0x100
    assert data[start:start + 0x100].decode("utf-16le").split("\0")[0] == "DSiDrop", "Wrong launcher name or unwanted subtitle"
icon = Image.open(Path(__file__).resolve().parents[1] / "assets/icon.png")
palette = icon.getpalette()
for color in range(16):
    r, g, b = palette[color * 3:color * 3 + 3]
    expected = (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)
    actual = struct.unpack_from("<H", data, banner + 0x220 + color * 2)[0]
    assert actual & 0x7fff == expected, f"Wrong banner palette entry {color}"
for y in range(32):
    for x in range(32):
        tile = (y // 8) * 4 + x // 8
        byte = data[banner + 0x20 + tile * 32 + (y % 8) * 4 + (x % 8) // 2]
        actual = (byte >> ((x & 1) * 4)) & 15
        assert actual == icon.getpixel((x, y)), f"Wrong banner icon pixel at {x},{y}"
print("DSi UI: sliding NDS/GBA/Others tabs, mixed queue, scrolling, purple buttons, progress/percentage and embedded icon passed")
