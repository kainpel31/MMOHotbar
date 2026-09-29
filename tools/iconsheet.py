#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# iconsheet.py - shared discovery + ordering for the hotbar's item icons.
#
# UNUSED. This belongs to the abandoned "bake our own sheet into Hotbar.swf" approach.
# The bar instead loads the PLAYER'S OWN icon sheet at run time (HotbarHUD.cpp
# ItemIconLabel / kIconSheets in HotbarHUDView.cpp) and addresses its frame labels, so no
# icon art lives in the repository and nothing needs baking. Kept alongside
# src/HotbarIconTable.h, which is the other half of the same approach and equally unused.
#
# The art lives in "Asset dont push to git/<category>/*.png": one 32x32 RGBA icon per
# item. It is local-only and never tracked (see .gitignore); tools/build_hud_swf.py
# bakes it into dist/Interface/MMOHotbar/Hotbar.swf and writes the C++ lookup table
# next to the movie, so nothing outside this folder is needed to build the plugin.
#
# The sheet is addressed by FRAME, not by name: frame 1 is the reserved empty frame
# and frame 2 + i is icons[i]. The plugin calls gotoAndStop() on a slot's icon clip
# with that number -- the same contract the keycap SWF already uses for scancodes.
#
# ITEM -> ICON is matched on the EditorID, not the display name. The display name is
# localized (a Russian game returns "Железный меч" for the iron sword), so matching on
# it would break outside English. The EditorID is not localized, and normalizing it
# the same way as our filenames collapses the two onto one key:
#
#     "Iron Sword.png" -> ironsword        TESForm::GetFormEditorID() "IronSword"
#                                         -> ironsword
#
# normalize() below and Normalize() in src/HotbarIconTable.h MUST stay identical: the
# C++ half does the lookup with whatever the table was built from.
import os

from PIL import Image

# Icons are authored at 32x32 but sit in a 64px slot with an 8px inset, i.e. a 48px
# window (ICON_INSET / SLOT_PX in build_hud_swf.py). Upscale to fill it.
ICON_PX = 48

# Subfolders of the asset root, in the order their icons are numbered. Sorted
# explicitly rather than by os.listdir so the frame numbering -- which the committed
# C++ table encodes -- is identical on every machine and every filesystem.
CATEGORIES = (
    "Equipment",
    "Food",
    "Material",
    "Misc",
    "Monster Part",
    "Ore & Gem",
    "Potion",
    "Weapon & Tool",
)

ALIAS_FILE = "icon_aliases.txt"


def normalize(a_name):
    """Lowercase and drop every non-alphanumeric character.

    "Iron Sword" -> "ironsword", "Wine 2" -> "wine2". Mirrored by Normalize() in
    src/HotbarIconTable.h; the two functions are a matched pair.
    """
    out = []
    for ch in a_name.lower():
        if ("a" <= ch <= "z") or ("0" <= ch <= "9"):
            out.append(ch)
    return "".join(out)


def key_for(a_path):
    """Lookup key for an icon file: its stem, normalized."""
    return normalize(os.path.splitext(os.path.basename(a_path))[0])


def discover(a_root):
    """Ordered [(key, path)] for every icon under a_root.

    Order is (category index, filename), which is stable and reproducible. The order
    IS the frame numbering, so it must never depend on directory iteration order.
    """
    found = []
    for cat_index, category in enumerate(CATEGORIES):
        cat_dir = os.path.join(a_root, category)
        if not os.path.isdir(cat_dir):
            continue
        names = sorted(
            n for n in os.listdir(cat_dir)
            if n.lower().endswith(".png") and not n.startswith(".")
        )
        for name in names:
            found.append((cat_index, key_for(name), os.path.join(cat_dir, name)))
    found.sort(key=lambda e: (e[0], e[1]))
    return [(key, path) for _cat, key, path in found]


def aliases(a_tools_dir):
    """Extra lookup keys from tools/icon_aliases.txt, as {alias: target_key}.

    Needed where a filename cannot spell the EditorID. "Wine 2.png" normalizes to
    "wine2", but the game's EditorID is "Wine02" -- zero padded -- so nothing would
    ever match it. Each line reads `alias = target`, where target is the key of an
    existing icon. Blank lines and `#` comments are ignored; a line naming an unknown
    target is a hard error rather than a silent no-op, because it would otherwise
    look like a working mapping that never fires.
    """
    path = os.path.join(a_tools_dir, ALIAS_FILE)
    if not os.path.exists(path):
        return {}
    out = {}
    with open(path, encoding="utf-8") as fh:
        for lineno, raw in enumerate(fh, 1):
            line = raw.split("#", 1)[0].strip()
            if not line or "=" not in line:
                continue
            alias, target = (p.strip() for p in line.split("=", 1))
            if not alias or not target:
                continue
            key = normalize(alias)
            if not key:
                continue
            if key in out:
                raise SystemExit("%s:%d: duplicate alias %r" % (path, lineno, key))
            out[key] = normalize(target)
    return out


def render(a_path, a_px=ICON_PX):
    """Load one icon, flatten it to a_px square RGBA, or None if unreadable."""
    try:
        im = Image.open(a_path).convert("RGBA")
    except OSError:
        return None
    if im.size != (a_px, a_px):
        im = im.resize((a_px, a_px), Image.LANCZOS)
    return im


def to_argb(a_im):
    """PIL hands back straight (non-premultiplied) RGBA; SWF Format 5 wants ARGB.

    Moved here from build_hud_swf.py because the icon sheet needs it too, and the
    alpha-first reorder is exactly the kind of detail that must not be written twice
    with one of the two copies drifting.
    """
    rgba = a_im.tobytes()
    n = len(rgba)
    argb = bytearray(n)
    argb[0::4] = rgba[2::4]  # blue
    argb[1::4] = rgba[3::4]  # alpha
    argb[2::4] = rgba[0::4]  # red
    argb[3::4] = rgba[1::4]  # green
    return bytes(argb)
