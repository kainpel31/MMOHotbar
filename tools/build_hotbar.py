#!/usr/bin/env python3
# build_hotbar.py - compose 16 category icons into dist\Interface\MMOHotbar\Hotbar.swf
# (AS2 v8, single exported symbol "ico" = 16-frame sprite; frame 1 = empty).
# Verify: ffdec-cli -swf2xml must parse the file fully (16 bitmaps, 1 sprite).
import argparse, os, subprocess, sys
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import swflib

W = 64
NFRAMES = 16
# character ids: bitmaps 1..16, shapes 17..32, sprite 33
ID_BITMAP = 1
ID_SHAPE = 17
ID_SPRITE = 33
SYMBOL = "ico"


def coin_base():
    im = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    d = ImageDraw(im)
    return im


def ImageDraw(im):
    from PIL import ImageDraw as D
    d = D.Draw(im)
    d.ellipse([1, 1, W - 2, W - 2], fill=(18, 24, 34, 235), outline=(255, 215, 0, 220), width=3)
    return im


def compose(icon_path):
    im = coin_base()
    g = Image.open(icon_path).convert("RGBA")
    if g.size != (52, 52):
        g = g.resize((52, 52))
    im.alpha_composite(g, (6, 6))
    return im


def build(icons_dir, out):
    frames = []
    for i in range(NFRAMES):
        p = os.path.join(icons_dir, "icon_%d.png" % i)
        if not os.path.exists(p):
            print("missing %s" % p, file=sys.stderr)
            return 1
        frames.append(compose(p))

    tags = [swflib.file_attributes(), swflib.set_background(0, 0, 0)]
    for i in range(NFRAMES):
        argb = frames[i].tobytes("raw", "ARGB")
        tags.append(swflib.define_bits_lossless2(ID_BITMAP + i, argb, W, W))
    for i in range(NFRAMES):
        tags.append(swflib.define_shape2(ID_SHAPE + i, ID_BITMAP + i))

    inner = []
    for i in range(NFRAMES):
        inner.append(swflib.place_object2(ID_SHAPE + i, 1))
        inner.append(swflib.show_frame())
    inner.append(swflib.end_tag())
    tags.append(swflib.define_sprite(ID_SPRITE, NFRAMES, inner))
    tags.append(swflib.export_assets([(ID_SPRITE, SYMBOL)]))
    tags.append(swflib.show_frame())
    tags.append(swflib.end_tag())

    # stage 768x768 twips (38.4px at 20 twips/px -- GFx menu standard small stage)
    size = swflib.save(out, 15360, 15360, tags, fps=30, frames=1)
    print("wrote %s (%d bytes)" % (out, size))

    ffd = os.path.join(os.environ["LOCALAPPDATA"], "FFDec", "ffdec-cli.exe")
    if os.path.exists(ffd):
        v = subprocess.run([ffd, "-swf2xml", out, out + ".check.xml"],
                           capture_output=True, text=True)
        xml = ""
        if os.path.exists(out + ".check.xml"):
            with open(out + ".check.xml", encoding="utf-8", errors="replace") as f:
                xml = f.read()
        ok = (v.returncode == 0
              and xml.count("DefineBitsLossless2Tag") == NFRAMES
              and xml.count("DefineShape2Tag") == NFRAMES
              and "DefineSpriteTag" in xml and SYMBOL in xml
              and xml.rstrip().endswith("</swf>"))
        print("ffdec verify: rc=%d xml=%dB bitmaps=%d shapes=%d sprite=%s symbol=%s -> %s"
              % (v.returncode, len(xml),
                 xml.count("DefineBitsLossless2Tag"), xml.count("DefineShape2Tag"),
                 "DefineSpriteTag" in xml, SYMBOL in xml, "OK" if ok else "FAIL"))
        if not ok:
            print(v.stdout[-2000:], v.stderr[-2000:])
            return 1
        os.remove(out + ".check.xml")
    else:
        print("ffdec-cli not found; skipped verify")
    return 0


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser()
    ap.add_argument("--icons", default=os.path.join(here, "_out"))
    ap.add_argument("--out", default=os.path.join(here, "..", "dist", "Interface", "MMOHotbar", "Hotbar.swf"))
    a = ap.parse_args()
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    sys.exit(build(a.icons, os.path.abspath(a.out)))
