"""Build original, palette-preserving NDS backgrounds and raster pictograms."""
from pathlib import Path
from itertools import groupby
import math

from PIL import Image, ImageDraw, ImageFont, ImageOps

root = Path(__file__).resolve().parents[1]


def preview_font(bold=False):
    names = (
        ("C:/Windows/Fonts/segoeuib.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
         "/usr/share/fonts/truetype/liberation2/LiberationSans-Bold.ttf", "/System/Library/Fonts/Supplemental/Arial Bold.ttf")
        if bold else
        ("C:/Windows/Fonts/segoeui.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
         "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf", "/System/Library/Fonts/Supplemental/Arial.ttf")
    )
    return next((name for name in names if Path(name).is_file()), names[0])


palette = [
    (0, 0, 0), (8, 9, 13), (18, 20, 28), (35, 39, 53),
    (135, 80, 255), (191, 159, 255), (155, 158, 174), (232, 234, 242),
    (36, 33, 69), (51, 56, 73), (13, 15, 21), (240, 126, 135),
    (164, 131, 255), (107, 0, 250), (85, 89, 112), (255, 255, 255),
]
palette_image = Image.new("P", (1, 1))
palette_image.putpalette([channel for color in palette[:15] for channel in color] + [0] * (768 - 45))


def background(feature=False, bottom=False):
    image = Image.new("RGB", (256, 192), palette[1])
    draw = ImageDraw.Draw(image)
    draw.polygon([(0, 0), (256, 0), (256, 28), (0, 28)], fill=palette[2])
    draw.line((0, 27, 255, 27), fill=palette[3])
    if bottom:
        draw.rectangle((0, 163, 255, 191), fill=palette[10])
        draw.line((0, 163, 255, 163), fill=palette[3])
    else:
        draw.polygon([(0, 150), (54, 172), (202, 172), (256, 150), (256, 192), (0, 192)], fill=palette[10])
        draw.polygon([(0, 35), (18, 43), (18, 131), (0, 140)], fill=palette[2])
        draw.polygon([(256, 35), (238, 43), (238, 131), (256, 140)], fill=palette[2])
    if feature:
        for left, angle in ((45, 8), (187, -8)):
            layer = Image.new("RGBA", (30, 100))
            face = ImageDraw.Draw(layer)
            face.rounded_rectangle((1, 1, 28, 96), radius=4, fill=palette[2] + (255,), outline=palette[3] + (255,))
            face.line((5, 7, 5, 88), fill=palette[9] + (255,))
            face.rounded_rectangle((10, 15, 22, 76), radius=2, fill=palette[8] + (255,))
            layer = layer.rotate(angle, resample=Image.Resampling.BICUBIC, expand=True)
            image.paste(layer, (left, 31), layer)
        draw = ImageDraw.Draw(image)
        for left in (2, 186):
            draw.rounded_rectangle((left + 1, 48, left + 68, 115), radius=4, fill=palette[10])
            draw.rounded_rectangle((left, 46, left + 67, 113), radius=4, fill=palette[3])
            draw.rounded_rectangle((left + 1, 47, left + 66, 112), radius=3, fill=palette[2])
        draw.rounded_rectangle((70, 25, 188, 141), radius=6, fill=palette[10])
        draw.rounded_rectangle((68, 20, 187, 139), radius=6, fill=palette[3])
        draw.rounded_rectangle((69, 21, 186, 138), radius=5, fill=palette[2])
        draw.line((75, 21, 180, 21), fill=palette[9])
        draw.line((108, 139, 147, 139), fill=palette[4], width=2)
    return image.quantize(palette=palette_image, dither=Image.Dither.NONE)


def icon(kind):
    # Draw from one optical grid, then filter directly to the display size.
    # Keeping a native 16px atlas avoids runtime nearest-neighbour reduction.
    scale = 6
    image = Image.new("L", (24 * scale, 24 * scale))
    draw = ImageDraw.Draw(image)

    def line(points, width=2, fill=255):
        scaled = [(round(x * scale), round(y * scale)) for x, y in points]
        draw.line(scaled, fill=fill, width=round(width * scale), joint="curve")
        radius = width * scale / 2
        for x, y in scaled:
            draw.ellipse((round(x - radius), round(y - radius), round(x + radius), round(y + radius)), fill=fill)

    def polygon(points, fill=255):
        draw.polygon([(round(x * scale), round(y * scale)) for x, y in points], fill=fill)

    def box(bounds, radius=2, fill=255):
        draw.rounded_rectangle(tuple(round(value * scale) for value in bounds), radius=radius * scale, fill=fill)

    def outline(bounds, radius=2, width=2):
        draw.rounded_rectangle(tuple(round(value * scale) for value in bounds), radius=radius * scale, outline=255, width=round(width * scale))

    def ellipse(bounds, width=2):
        draw.ellipse(tuple(round(value * scale) for value in bounds), outline=255, width=round(width * scale))

    if kind == 0:
        line([(3, 4), (8, 4), (12, 6), (16, 4), (21, 4), (21, 19), (16, 19), (12, 21), (8, 19), (3, 19), (3, 4)])
        line([(12, 6), (12, 21)], 1.5)
    elif kind == 1:
        line([(8, 3), (19, 3), (21, 5), (21, 21), (3, 21), (3, 8), (8, 3)])
        for column in (10, 14, 18):
            line([(column, 5), (column, 8)], 1.5)
        outline((7, 13, 17, 18), 1, 1)
    elif kind == 2:
        line([(3, 7), (21, 7), (17, 3)])
        line([(21, 7), (17, 11)])
        line([(21, 17), (3, 17), (7, 13)])
        line([(3, 17), (7, 21)])
    elif kind == 3:
        for spoke in range(8):
            angle = spoke * math.pi / 4
            line([(12 + math.cos(angle) * 7, 12 + math.sin(angle) * 7),
                  (12 + math.cos(angle) * 10, 12 + math.sin(angle) * 10)], 2.5)
        ellipse((5, 5, 19, 19))
        ellipse((9, 9, 15, 15), 1.5)
    elif kind == 4:
        line([(3, 3), (18, 3), (21, 6), (21, 21), (3, 21), (3, 3)])
        outline((7, 3, 16, 9), 1, 1)
        outline((7, 14, 17, 21), 1, 1)
    elif kind == 5:
        ellipse((3, 3, 17, 17))
        line([(16, 16), (21, 21)])
    elif kind == 6:
        line([(3, 6), (6, 6), (17, 18), (21, 18)])
        line([(3, 18), (6, 18), (10, 14)])
        line([(14, 10), (17, 6), (21, 6)])
        line([(18, 3), (21, 6), (18, 9)])
        line([(18, 15), (21, 18), (18, 21)])
    elif kind == 7:
        line([(3, 4), (21, 4), (14, 12), (14, 20), (10, 18), (10, 12), (3, 4)])
    elif kind == 8:
        line([(20, 12), (4, 12)])
        line([(10, 6), (4, 12), (10, 18)])
    elif kind == 9:  # Wi-Fi
        for radius in (10, 6):
            draw.arc(((12 - radius) * scale, (18 - radius) * scale, (12 + radius) * scale, (18 + radius) * scale), 220, 320, fill=255, width=2 * scale)
        draw.ellipse((10.5 * scale, 16.5 * scale, 13.5 * scale, 19.5 * scale), fill=255)
    elif kind == 10:  # Server
        for top in (3, 14):
            outline((3, top, 21, top + 7), 2)
            box((6, top + 3, 7, top + 4), 0)
            line([(13, top + 3.5), (17, top + 3.5)], 1.5)
    elif kind == 11:  # Update
        line([(12, 15), (12, 3), (7, 8)])
        line([(12, 3), (17, 8)])
        line([(4, 14), (4, 21), (20, 21), (20, 14)])
    elif kind == 12 or kind == 23:  # Refresh / restore
        draw.arc((3 * scale, 3 * scale, 21 * scale, 21 * scale), 45, 325, fill=255, width=2 * scale)
        line([(16, 3), (21, 6), (21, 1)])
        if kind == 23:
            line([(12, 7), (12, 12), (16, 14)], 1.5)
    elif kind == 13:  # Delete
        line([(3, 6), (21, 6)])
        line([(9, 6), (9, 3), (15, 3), (15, 6)], 1.5)
        line([(5, 7), (6, 21), (18, 21), (19, 7)])
        line([(10, 10), (10, 17)], 1.5)
        line([(14, 10), (14, 17)], 1.5)
    elif kind == 14:  # Confirm
        line([(4, 12), (9, 17), (20, 6)], 2.5)
    elif kind == 15:  # Close
        line([(5, 5), (19, 19)])
        line([(19, 5), (5, 19)])
    elif kind == 16:  # Download
        line([(12, 3), (12, 15), (7, 10)])
        line([(12, 15), (17, 10)])
        line([(4, 15), (4, 21), (20, 21), (20, 15)])
    elif kind == 17:  # Add to queue
        line([(3, 5), (16, 5)])
        line([(3, 11), (12, 11)])
        line([(3, 17), (9, 17)])
        line([(17, 12), (17, 22)])
        line([(12, 17), (22, 17)])
    elif kind == 18:  # Dual screen
        outline((4, 2, 20, 11), 2)
        outline((4, 13, 20, 22), 2)
        line([(10, 7), (14, 7)], 1)
        line([(10, 18), (14, 18)], 1)
    elif kind == 19:  # Favorite
        points = []
        for step in range(10):
            angle = -math.pi / 2 + step * math.pi / 5
            radius = 10 if step % 2 == 0 else 4.5
            points.append((12 + math.cos(angle) * radius, 12 + math.sin(angle) * radius))
        line(points + points[:1], 1.7)
    elif kind == 20:  # Pause
        box((5, 4, 8, 20), 1)
        box((16, 4, 19, 20), 1)
    elif kind == 21:  # Play
        line([(6, 3), (21, 12), (6, 21), (6, 3)], 2)
    elif kind == 22:  # Information
        ellipse((2, 2, 22, 22))
        line([(12, 11), (12, 17)])
        box((11, 6, 13, 8), 1)
    elif kind == 24:  # Automatic backups
        line([(4, 12), (7, 8), (12, 8)], 2)
        line([(3, 6), (3, 12), (9, 12)], 1.5)
        line([(20, 12), (17, 17), (12, 17)], 2)
        line([(21, 18), (21, 12), (15, 12)], 1.5)
    image = image.resize((16, 16), Image.Resampling.LANCZOS)
    levels = [(value * 3 + 127) // 255 for value in image.get_flattened_data()]
    packed = [sum(levels[index + offset] << (offset * 2) for offset in range(4)) for index in range(0, len(levels), 4)]
    return image, packed


images = [background(), background(feature=True), background(bottom=True)]
source = "#pragma once\n"
for name, image in zip(("uiArtTop", "uiArtFeature", "uiArtBottom"), images):
    words = []
    for tile_y in range(24):
        for tile_x in range(32):
            for row in range(8):
                for group in range(2):
                    words.append(sum(image.getpixel((tile_x * 8 + group * 4 + offset, tile_y * 8 + row)) << (offset * 4) for offset in range(4)))
    runs = [(len(list(values)), word) for word, values in groupby(words)]
    source += f"static const unsigned short {name}[] = {{\n"
    source += "\n".join("    " + ",".join(f"{count},0x{word:04x}" for count, word in runs[index:index + 8]) + "," for index in range(0, len(runs), 8))
    source += "\n    0,0\n};\n"
icons = [icon(kind) for kind in range(25)]
source += "static const unsigned char uiArtIcons[25][64] = {\n"
source += "\n".join("    {" + ",".join(map(str, packed)) + "}," for _, packed in icons)
source += "\n};\n"
(root / "include/ui_art.h").write_text(source, encoding="ascii")
sheet = Image.new("RGB", (768, 192))
for index, image in enumerate(images):
    sheet.paste(image.convert("RGB"), (index * 256, 0))
sheet.save(root / "assets/ui-art.png")

preview = Image.new("RGB", (256, 384))
preview.paste(images[1].convert("RGB"), (0, 0))
preview.paste(images[2].convert("RGB"), (0, 192))
body = ImageFont.truetype(preview_font(), 12)
caption = ImageFont.truetype(preview_font(), 11)
heading = ImageFont.truetype(preview_font(bold=True), 16)


def native_text(text, left, top, width, font, *, centered=False, large=False, color=7):
    cell = 24 if large else 16

    def advance(character):
        return min(cell, max(2, round(font.getlength(character)) + 1))

    if sum(advance(character) for character in text) > width:
        while text and sum(advance(character) for character in text + "...") > width:
            text = text[:-1]
        text += "..."
    if centered:
        left += (width - sum(advance(character) for character in text)) // 2
    for character in text:
        glyph = Image.new("L", (cell, cell))
        ImageDraw.Draw(glyph).text((0, 18 if large else 12), character, font=font, fill=255, anchor="ls")
        levels = glyph.point(lambda value: (value * 3 + 127) // 255)
        for row in range(cell):
            for col in range(cell):
                coverage = levels.getpixel((col, row))
                if not coverage or not (0 <= left + col < 256 and 0 <= top + row < 384):
                    continue
                background_color = preview.getpixel((left + col, top + row))
                blended = tuple((background_color[channel] * (3 - coverage) + palette[color][channel] * coverage) // 3 for channel in range(3))
                nearest = min(range(1, 15), key=lambda index: sum((palette[index][channel] - blended[channel]) ** 2 for channel in range(3)))
                preview.putpixel((left + col, top + row), palette[nearest])
        left += advance(character)


cover_paths = sorted((root.parent / "public/downloads/apps/covers").glob("*.png"))
title = "Your next favorite"
if cover_paths:
    cover = ImageOps.contain(Image.open(cover_paths[0]).convert("RGBA"), (112, 112))
    preview.paste(cover, (72 + (112 - cover.width) // 2, 24 + (112 - cover.height) // 2), cover)
    title = {"dsorganize": "DSOrganize", "notpipe-dsi": "notPipe DSi", "pkmn-chest": "pkmn-chest"}.get(cover_paths[0].stem, cover_paths[0].stem)
    if len(cover_paths) > 1:
        for left, cover_path in ((4, cover_paths[-1]), (188, cover_paths[1])):
            neighbour = ImageOps.contain(Image.open(cover_path).convert("RGBA"), (64, 64))
            preview.paste(neighbour, (left + (64 - neighbour.width) // 2, 48 + (64 - neighbour.height) // 2), neighbour)
logo = Image.open(root / "assets/icon.png").convert("RGBA")
preview.paste(logo, (216, 0), logo)
draw = ImageDraw.Draw(preview)
native_text("DSiDrop", 16, 0, 84, heading, large=True)
draw.rectangle((96, 0, 211, 23), fill=palette[1])
native_text("Apps 1/3", 100, 5, 108, caption, color=6)
native_text(title, 16, 140, 224, body, centered=True)
draw.rectangle((8, 172, 247, 191), fill=palette[10])
native_text("Game 4 MiB / SD 12 GiB free", 12, 176, 232, caption, centered=True)
native_text("GBA", 16, 197, 44, body, color=6)
native_text("Others", 80, 197, 44, body)
draw.line((80, 216, 123, 216), fill=palette[4], width=2)
for index, label in enumerate((title, "notPipe DSi", "pkmn-chest", "Homebrew library", "Your uploads", "Themes")):
    top = 222 + index * 22
    if index == 0:
        draw.rounded_rectangle((8, top, 247, top + 20), radius=3, fill=palette[8])
        draw.line((8, top + 4, 8, top + 16), fill=palette[4], width=2)
    native_text(f"{index + 1:02}", 18, top + 4, 20, caption, color=5 if index == 0 else 6)
    native_text(label, 42, top + 2, 194, body, color=7 if index == 0 else 6)
for index, label in enumerate(("Catalog", "SD card", "Transfers", "Settings")):
    mask = icons[index][0].resize((16, 16), Image.Resampling.LANCZOS)
    preview.paste(palette[5] if index == 0 else palette[6], (index * 64 + 24, 357, index * 64 + 40, 373), mask)
    native_text(label, index * 64, 371, 64, caption, centered=True, color=5 if index == 0 else 6)
preview.save(root / "assets/ui-preview.png")
print("Built embedded NDS artwork and 25 native 16px antialiased pictograms; palette unchanged")
