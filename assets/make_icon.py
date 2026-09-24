"""Renders assets/iconger.ico (and a preview PNG).

The mark: a 2x2 grid of app tiles where one tile - the tangerine one - lifts out
of its slot and turns, i.e. "swapping one icon". Drawn at 1024 px and downsampled
so every size stays clean.  Usage: python assets/make_icon.py
"""
import math
import os
from PIL import Image, ImageDraw

S = 1024
BG = (27, 24, 33, 255)          # theme::card
EDGE = (44, 39, 51, 255)        # theme::border
TILE = (74, 66, 85, 255)        # muted slots
GHOST = (74, 66, 85, 255)       # outline of the empty slot the tile left
ACCENT = (255, 138, 61, 255)    # theme::primary (tangerine)


def rounded_poly(cx, cy, size, radius, angle_deg, steps=12):
    """Points of a rounded square centred on (cx, cy), rotated by angle_deg."""
    a = math.radians(angle_deg)
    h = size / 2 - radius
    pts = []
    for i, (sx, sy) in enumerate(((1, -1), (1, 1), (-1, 1), (-1, -1))):
        start = -90 + 90 * i
        for k in range(steps + 1):
            t = math.radians(start + 90 * k / steps)
            x = sx * h + radius * math.cos(t)
            y = sy * h + radius * math.sin(t)
            pts.append((cx + x * math.cos(a) - y * math.sin(a), cy + x * math.sin(a) + y * math.cos(a)))
    return pts


def render():
    img = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((40, 40, S - 40, S - 40), radius=230, fill=BG, outline=EDGE, width=20)

    tile, gap = 250, 64
    x0 = (S - (tile * 2 + gap)) / 2
    centres = [(x0 + tile / 2 + c * (tile + gap), x0 + tile / 2 + r * (tile + gap)) for r in (0, 1) for c in (0, 1)]
    for i, (cx, cy) in enumerate(centres):
        if i == 1:  # top-right: the slot being swapped out
            d.polygon(rounded_poly(cx, cy, tile - 20, 56, 0), outline=GHOST, width=20)
        else:
            d.polygon(rounded_poly(cx, cy, tile, 64, 0), fill=TILE)

    # the lifted tile: nudged up/right and turned
    cx, cy = centres[1]
    lifted = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    ld = ImageDraw.Draw(lifted)
    ld.polygon(rounded_poly(cx + 52, cy - 52, tile + 30, 70, 14), fill=(0, 0, 0, 120))  # soft shadow
    lifted = lifted.resize((S // 8, S // 8), Image.LANCZOS).resize((S, S), Image.LANCZOS)
    img.alpha_composite(lifted)
    d = ImageDraw.Draw(img)
    d.polygon(rounded_poly(cx + 40, cy - 64, tile + 30, 70, 14), fill=ACCENT)
    return img


if __name__ == '__main__':
    here = os.path.dirname(os.path.abspath(__file__))
    img = render()
    img.save(os.path.join(here, 'iconger.ico'), sizes=[(s, s) for s in (16, 20, 24, 32, 40, 48, 64, 128, 256)])
    img.resize((256, 256), Image.LANCZOS).save(os.path.join(here, 'iconger-256.png'))
    # contact sheet of the small sizes, on a dark and a light taskbar
    sheet = Image.new('RGBA', (560, 140), (32, 32, 32, 255))
    ImageDraw.Draw(sheet).rectangle((0, 70, 560, 140), fill=(238, 238, 238, 255))
    x = 10
    for s in (16, 24, 32, 48, 64):
        small = img.resize((s, s), Image.LANCZOS)
        sheet.alpha_composite(small, (x, 35 - s // 2))
        sheet.alpha_composite(small, (x, 105 - s // 2))
        x += s + 30
    sheet.save(os.path.join(os.environ.get('TEMP', here), 'iconger-sizes.png'))
