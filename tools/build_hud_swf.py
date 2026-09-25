#!/usr/bin/env python3
# build_hud_swf.py - build dist/Interface/MMOHotbar/Hotbar.swf
#
# The movie is a 12-slot MMO hotbar row. It is deliberately built from OUR OWN
# art only (Asset dont push to git/Hotbar.png) so the repository stays free of
# third-party SWF assets:
#
#   * the slot frame comes from Hotbar.png, embedded here as a bitmap;
#   * the keycap label is a dynamic text field the plugin writes into;
#   * the icon is an empty MovieClip the plugin calls loadMovie() on at run
#     time, so the player's own SkyUI icon sheet supplies the picture.
#
# No icon, keycap, font or SWF belonging to another mod is embedded, extracted
# or shipped. SkyUI is a *runtime requirement* only.
#
# Structure (SWF v8, no ActionScript -- everything is driven from C++ by
# addressing the named display objects):
#
#   char 1           DefineBitsLossless2  slot frame
#   char 2           DefineShape2         paints char 1
#   char 10..21      DefineSprite         empty icon clips  (icon0..icon11)
#   char 30..41      DefineEditText       keycap labels    (key0..key11)
#
# Verify: ffdec -swf2xml must parse the file completely and report every tag.
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import swflib  # noqa: E402

# --- layout (Flash is 1 px == 20 twips) ---
TWIPS = 20
SLOTS = 12
SLOT_PX = 64          # one slot cell, matches the frame art
ICON_INSET = 8        # icon is inset inside the frame
KEY_TOP = 48          # keycap text sits over the lower part of the frame
KEY_HEIGHT = 15

ID_BITMAP = 1
ID_SHAPE = 2
ID_ICON_BASE = 10
ID_KEY_BASE = 30

OUT_REL = os.path.join("dist", "Interface", "MMOHotbar", "Hotbar.swf")
DEFAULT_FRAME = os.path.join("Asset dont push to git", "Hotbar.png")


def find_ffdec():
    """Locate ffdec-cli: PATH first, then the two usual Windows installs."""
    exe = "ffdec-cli.exe" if os.name == "nt" else "ffdec"
    found = shutil.which(exe)
    if found:
        return found
    local = os.environ.get("LOCALAPPDATA")
    for cand in (os.path.join(local, "FFDec", "ffdec-cli.exe") if local else None,
                 r"D:\FlashDecompire\ffdec-cli.exe"):
        if cand and os.path.exists(cand):
            return cand
    return None


def frame_bitmap(frame_path):
    """Load the slot art and flatten it to the exact cell size."""
    im = Image.open(frame_path).convert("RGBA")
    if im.size != (SLOT_PX, SLOT_PX):
        im = im.resize((SLOT_PX, SLOT_PX), Image.LANCZOS)
    return im


def to_argb(im):
    """PIL hands back straight (non-premultiplied) RGBA; SWF Format 5 wants
    ARGB, i.e. alpha first. Reorder explicitly instead of relying on a raw
    packer name that PIL may not have."""
    rgba = im.tobytes()
    n = len(rgba)
    argb = bytearray(n)
    argb[0::4] = rgba[2::4]  # blue
    argb[1::4] = rgba[3::4]  # alpha
    argb[2::4] = rgba[0::4]  # red
    argb[3::4] = rgba[1::4]  # green
    return bytes(argb)



def build(frame_path, out_path):
    cell = SLOT_PX * TWIPS
    icon_inset = ICON_INSET * TWIPS
    stage_w = SLOTS * SLOT_PX * TWIPS
    stage_h = SLOT_PX * TWIPS

    im = frame_bitmap(frame_path)
    tags = [swflib.file_attributes()]

    # One shared frame bitmap + shape, reused by all 12 slots.
    tags.append(swflib.define_bits_lossless2(ID_BITMAP, to_argb(im), SLOT_PX, SLOT_PX))
    tags.append(swflib.define_shape2(ID_SHAPE, ID_BITMAP, (0, cell, -cell, 0), scale=TWIPS))

    for i in range(SLOTS):
        tags.append(swflib.define_empty_sprite(ID_ICON_BASE + i))
    for i in range(SLOTS):
        # Placeholder text is the slot number, so an empty hotbar still shows
        # its own shape; the plugin overwrites this with the real chord.
        tags.append(swflib.define_edit_text(
            ID_KEY_BASE + i,
            (i * cell, (i + 1) * cell, -(KEY_TOP + KEY_HEIGHT) * TWIPS, -KEY_TOP * TWIPS),
            str(i + 1)))

    for i in range(SLOTS):
        x = i * cell
        base = 10 + i * 3
        # frame (bottom layer) -> icon -> keycap (top layer)
        tags.append(swflib.place_object2(ID_SHAPE, base,
                                          swflib.matrix(translate=(x, 0)), name="frame%d" % i))
        tags.append(swflib.place_object2(ID_ICON_BASE + i, base + 1,
                                          swflib.matrix(translate=(x + icon_inset, -icon_inset)),
                                          name="icon%d" % i))
        tags.append(swflib.place_object2(ID_KEY_BASE + i, base + 2,
                                          swflib.matrix(translate=(x, 0)), name="key%d" % i))

    tags.append(swflib.show_frame())
    tags.append(swflib.end_tag())

    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    size = swflib.save(out_path, stage_w, stage_h, tags, fps=30, frames=1)
    print("wrote %s (%d bytes, %d slots, stage %dx%d twips)"
          % (out_path, size, SLOTS, stage_w, stage_h))
    return verify(out_path)


def verify(swf_path):
    """Parse the movie back with FFDec and assert the expected structure."""
    ffdec = find_ffdec()
    if not ffdec:
        print("WARNING: ffdec-cli not found -- structure NOT verified")
        return 0

    tmp = tempfile.mkdtemp(prefix="mmohotbar_swfcheck_")
    xml_path = os.path.join(tmp, "check.xml")
    try:
        proc = subprocess.run([ffdec, "-swf2xml", swf_path, xml_path],
                              capture_output=True, text=True)
        xml = ""
        if os.path.exists(xml_path):
            with open(xml_path, encoding="utf-8", errors="replace") as fh:
                xml = fh.read()

        counts = {
            "bitmaps": xml.count("DefineBitsLossless2Tag"),
            "shapes": xml.count("DefineShape2Tag"),
            "sprites": xml.count("DefineSpriteTag"),
            "texts": xml.count("DefineEditTextTag"),
        }
        missing = [n for i in range(SLOTS) for n in ("key%d" % i, "icon%d" % i) if n not in xml]

        ok = (proc.returncode == 0
              and xml.rstrip().endswith("</swf>")
              and counts == {"bitmaps": 1, "shapes": 1, "sprites": SLOTS, "texts": SLOTS}
              and not missing
              and "SEVERE" not in (proc.stderr or "")
              and "Exception" not in (proc.stderr or ""))

        print("ffdec verify: rc=%d xml=%dB %s missing=%s -> %s"
              % (proc.returncode, len(xml), counts, missing or "none", "OK" if ok else "FAIL"))
        if not ok:
            print((proc.stdout or "")[-2000:])
            print((proc.stderr or "")[-2000:])
        return 0 if ok else 1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    ap = argparse.ArgumentParser(description="Build the MMOHotbar 12-slot HUD SWF")
    ap.add_argument("--frame", default=os.path.join(root, DEFAULT_FRAME),
                    help="slot frame art (ours; not shipped)")
    ap.add_argument("--out", default=os.path.join(root, OUT_REL))
    args = ap.parse_args()

    if not os.path.exists(args.frame):
        print("missing slot art: %s" % args.frame, file=sys.stderr)
        return 1
    return build(os.path.abspath(args.frame), os.path.abspath(args.out))


if __name__ == "__main__":
    sys.exit(main())
