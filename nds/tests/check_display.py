"""Compare the lower display after clean and prefilled-VRAM diagnostic boots.

Usage: python check_display.py CLEAN_IMAGES PREFILLED_IMAGES
The screenshots must come from tests/render_display.py.
"""
from pathlib import Path
import sys

from PIL import Image


reference, actual = (Path(arg) for arg in sys.argv[1:3])
for name in ("ssid", "password", "complete"):
    with Image.open(reference / (name + ".png")) as clean, Image.open(actual / (name + ".png")) as dirty:
        assert clean.size == dirty.size == (256, 384), f"Unexpected layout: {name}"

        offset = 256 * 192 * 3
        assert clean.convert("RGB").tobytes()[offset:] == dirty.convert("RGB").tobytes()[offset:], (
            f"Uninitialized VRAM changes the lower display: {name}"
        )
print("Lower display identical for clean and prefilled VRAM: SSID, password, keyboard hidden")
