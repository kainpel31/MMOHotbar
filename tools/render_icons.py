#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# render_icons.py - generate hotbar icon glyphs + bar bg PNG (default: tools/_out).
# Pure PIL: zero external art, license-free. Swap --fa font.ttf for nicer glyphs
# (needs fonttools). Output: icon_0.png..icon_15.png (64x64 RGBA) + bar.png (768x128).
import argparse, math, os
from PIL import Image, ImageDraw

ICON_W = ICON_H = 64
BAR_W, BAR_H = 768, 128  # two stacked 64px strips: slots 0-11 (y=0), 12-23 (y=64)

# (name, tint, fallback-kind); index 0 == transparent/empty
CATS = [
    ("empty",    (0, 0, 0, 0),          "empty"),
    ("weapon",   (225, 195, 120, 255),  "sword"),
    ("armor",    (175, 205, 235, 255),  "shield"),
    ("potion",   (40, 200, 90, 255),    "flask"),
    ("food",     (210, 120, 40, 255),   "round"),
    ("scroll",   (240, 240, 225, 255),  "rect"),
    ("book",     (120, 220, 150, 255),  "book"),
    ("key",      (215, 195, 120, 255),  "key"),
    ("magic",    (120, 200, 255, 255),  "wand"),
    ("shout",    (180, 140, 255, 255),  "cone"),
    ("soulgem",  (40, 160, 255, 255),   "gem"),
    ("misc",     (165, 165, 175, 255),  "hexagon"),
    ("ammo",     (235, 235, 235, 255),  "arrow"),
    ("gold",     (255, 215, 60, 255),   "coins"),
    ("quest",    (255, 200, 60, 255),   "star"),
    ("crafting", (150, 190, 230, 255), "plus"),
]


def _glyph(kind, tint):
    im = Image.new("RGBA", (ICON_W, ICON_H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    cx, cy = ICON_W / 2, ICON_H / 2
    if kind == "empty":
        return im
    if kind == "sword":
        d.polygon([(cx, 10), (cx + 9, cy - 6), (cx + 9, cy + 12)], fill=tint)
        d.rectangle([cx - 5, cy - 4, cx + 7, cy + 4], fill=tint)
        d.line([(cx - 2, cy + 4), (cx - 2, cy + 18)], fill=tint, width=3)
    elif kind == "shield":
        d.ellipse([cx - 14, 8, cx + 14, 56], fill=tint, outline=tint)
    elif kind == "flask":
        d.ellipse([cx - 11, 14, cx + 11, 46], fill=tint)
        d.rectangle([cx - 5, 6, cx + 5, 22], fill=tint)
    elif kind == "round":
        d.ellipse([cx - 14, 12, cx + 14, 50], fill=tint)
    elif kind == "rect":
        d.rectangle([cx - 13, 12, cx + 13, 50], fill=tint)
        d.line([(cx - 10, 26), (cx + 10, 26)], fill=(0, 0, 0, 180), width=1)
    elif kind == "book":
        d.rectangle([cx - 13, 14, cx + 13, 50], fill=tint)
        d.line([(cx - 10, 26), (cx + 10, 26)], fill=(0, 0, 0, 180), width=1)
        d.line([(cx - 10, 34), (cx + 10, 34)], fill=(0, 0, 0, 180), width=1)
    elif kind == "key":
        d.ellipse([cx - 9, 14, cx + 9, 34], fill=tint)
        d.rectangle([cx - 3, 30, cx + 3, 54], fill=tint)
        d.ellipse([cx - 4, 28, cx + 4, 38], fill=tint)
    elif kind == "wand":
        d.line([(cx, 12), (cx, 48)], fill=tint, width=3)
        d.polygon([(cx - 6, 12), (cx + 6, 12), (cx, 2)], fill=tint)
    elif kind == "cone":
        d.polygon([(cx, 12), (cx - 15, 48), (cx + 15, 48)], fill=tint)
    elif kind == "gem":
        d.polygon([(cx, 10), (cx + 13, 32), (cx, 54), (cx - 13, 32)], fill=tint)
        d.line([(cx, 10), (cx, 54)], fill=(255, 255, 255, 180), width=1)
    elif kind == "hexagon":
        pts = [(cx + 14 * math.cos(a), cy + 14 * math.sin(a)) for a in [i * 60 for i in range(6)]]
        d.polygon(pts, fill=tint)
    elif kind == "arrow":
        d.polygon([(cx, 14), (cx + 9, 34)], fill=tint)
        d.polygon([(cx, 14), (cx - 9, 34)], fill=tint)
        d.line([(cx, 34), (cx, 48)], fill=tint, width=3)
    elif kind == "coins":
        d.ellipse([cx - 15, 30, cx - 1, 50], fill=tint)
        d.ellipse([cx + 1, 30, cx + 15, 50], fill=tint)
    elif kind == "star":
        pts = []
        for i in range(10):
            a = (i - 2) * 36 * math.pi / 180
            r = 17 if i % 2 == 0 else int(17 * 0.48)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
        d.polygon(pts, fill=tint)
    elif kind == "plus":
        d.rectangle([cx - 3, 12, cx + 3, 50], fill=tint)
        d.rectangle([cx - 14, 31, cx + 14, 39], fill=tint)
    return im


def make_bar():
    strip = Image.new("RGBA", (BAR_W, 64), (0, 0, 0, 0))
    d = ImageDraw.Draw(strip)
    gap, cell = 2, (BAR_W - 2) // 12
    d.line([(0, 2), (BAR_W, 2)], fill=(255, 255, 255, 90), width=1)
    d.line([(0, 62), (BAR_W, 62)], fill=(0, 0, 0, 140), width=1)
    for i in range(13):
        x = i * (cell + gap)
        d.line([(x, 2), (x, 62)], fill=(255, 255, 255, 30), width=1)
    overlay = Image.new("RGBA", (BAR_W, 64), (20, 24, 36, 170))
    strip = Image.alpha_composite(strip, overlay)
    full = Image.new("RGBA", (BAR_W, BAR_H), (0, 0, 0, 0))
    full.paste(strip, (0, 0))
    full.paste(strip, (0, 64))
    return full


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "_out"))
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    for i, (_name, tint, kind) in enumerate(CATS):
        im = _glyph(kind, tint)
        im.save(os.path.join(a.out, "icon_%d.png" % i))
    make_bar().save(os.path.join(a.out, "bar.png"))
    print("wrote %d icons + bar to %s" % (len(CATS), a.out))


if __name__ == "__main__":
    main()
