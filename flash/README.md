# Keycap SWF — adapting it to another UI

The plugin does not draw keycaps itself. It attaches one symbol from
`Interface/STB_Keycaps.swf` to each list row and jumps it to a frame. Swap that file and
you have restyled every keycap in the mod — no code changes, no rebuild.

## The contract

A keycaps SWF must export a clip named **`STBKeycap`** whose frames are laid out like this:

| input | frame |
|---|---|
| keyboard | the DX scancode itself (e.g. `2` = key **1**, `16` = **Q**, `42` = **Shift**) |
| mouse | `256 +` button index |
| gamepad | `266 +` button index |

Frame 1 must `stop()`. The mapping lives in [`ChordScancodes`](../src/InventoryIcons.cpp) —
the `+256 / +266` offsets are hardcoded there.

This is exactly SkyUI's `ButtonArt` layout, so **any SkyUI-derived UI works unmodified**.

Two sets are documented here, one per folder. **Neither finished file is in this
repository, and neither is in the installer.** The art is SkyUI's and Untarnished UI's; those
mods permit *modifying* their assets, not redistributing them, so a built set stays on your own
disk. This repository and its build artifacts contain exactly one SWF —
`dist/Interface/MMOHotbar/Hotbar.swf`, ours (see [`../tools/`](../tools)). What lives here is
the recipe, so you can produce a set for whichever UI you actually run:

| set | source | notes |
|---|---|---|
| [SkyUI](SkyUI/README.md) | `interface/skyui/buttonart.swf` | the usual case; a standalone file, two edits |
| [Untarnished UI](Untarnished/README.md) | `interface/favoritesmenu.swf` | the clip is embedded in a whole menu and has to be cut out |

The two are the same contract but different art (152 vs 147 shapes), so they are not
interchangeable: the SkyUI set draws full key faces and defines the `!` `@` `#` symbols,
the Untarnished set draws bare digits as characters 560–569.

## What happens with no keycap SWF

Nothing breaks. `InventoryIcons` asks the Scaleform loader for
`Interface/STB_Keycaps.swf` at menu-open time; if the file is absent the load fails,
`ImportData::ImportResources` finds no source movie and returns early, and no keycap is
attached. The chords themselves are unaffected — they are stored in the co-save and fired
from the input sink, neither of which touches Scaleform. The visible effect is that list rows
show no key glyph.

The hotbar's own HUD movie is independent of this: `Interface/MMOHotbar/Hotbar.swf` is
tracked and always installed, and it draws its own frames and key labels.

## Where the art lives

Vanilla Skyrim has **no keyboard keycap art at all** — it draws hotkeys as text
(`AppendHotkeyText` + `$EverywhereMediumFont`). Checked every interface file in both SE
and AE: only `Mouse` and `Gamepad` indicator frames exist. So there is nothing to take
from the base game; every keycap set in the wild descends from SkyUI.

- **SkyUI** — `interface/skyui/buttonart.swf`, inside `SkyUI_SE.bsa`. Character **153**,
  exported as `ButtonArt`, 323 frames, labels `Keyboard@1` `Mouse@256` `Gamepad@266`
  `Reserved@282` `Unused@290` `PS3@302`. 19 KB, 152 shapes — already a clean import source.
- **UI overhauls** ship reskins of that same file: Dear Diary → `buttonArtDD.swf`,
  DearDiaryHUD → `buttonArtDDW.swf`, and so on. Some (Untarnished) additionally embed a
  copy of the clip inside their `favoritesmenu.swf`.

## Building a set for another UI

You need [JPEXS FFDec](https://github.com/jindrapetrik/jpexs-decompiler) (`ffdec-cli.exe`).
FFDec trips over paths containing spaces or brackets — work in a plain temp folder.

**1. Find the clip.** Try the mod's `interface/skyui/buttonart*.swf` first. Otherwise dump
its `favoritesmenu.swf` and look for the keyboard section (that is the Untarnished case --
[`Untarnished/build.py`](Untarnished/build.py) does the whole cut-out end to end):

```bash
ffdec-cli -dumpSWF source.swf > tags.txt
grep -n 'FrameLabel (name: "Keyboard")' tags.txt      # then read the enclosing DefineSprite (chid: N)
```

**2. Check the frame layout.** Count `ShowFrame` tags inside that sprite up to each
`FrameLabel`. You want `Mouse` at 256 and `Gamepad` at 266. Every SkyUI-derived set has
this; if yours doesn't, the plugin's offsets won't line up and the wrong glyphs will show.

**3. Add the `STBKeycap` export.** If the clip already exports under another name
(SkyUI uses `ButtonArt`), **do not rename it** — menus import the real SkyUI asset under
that name and you would collide with it. Add a *second* export for the same character.
FFDec's CLI can't do this, so append the tag directly:

```python
import struct, zlib
SRC, DST, CHID, NAME = "in.swf", "out.swf", 153, b"STBKeycap"

raw = open(SRC, "rb").read()
ver = raw[3]
body = zlib.decompress(raw[8:]) if raw[:3] == b"CWS" else raw[8:]

pos = ((5 + 4 * (body[0] >> 3) + 7) // 8) + 4          # skip FrameSize RECT + rate + count
p = pos
while p < len(body):                                    # walk to the End tag
    tl, = struct.unpack_from("<H", body, p)
    code, ln = tl >> 6, tl & 0x3F
    hdr = 2
    if ln == 0x3F:
        ln, = struct.unpack_from("<I", body, p + 2); hdr = 6
    if code == 0:
        break
    p += hdr + ln

payload = struct.pack("<HH", 1, CHID) + NAME + b"\x00"  # ExportAssets: count, id, name
tag = struct.pack("<H", (56 << 6) | len(payload)) + payload
new = body[:p] + tag + body[p:]
open(DST, "wb").write(b"CWS" + bytes([ver]) + struct.pack("<I", 8 + len(new)) + zlib.compress(new, 9))
```

**3b. Cut the clip out, if it came from a whole menu.** Keep only the characters the clip's
frames place, transitively, and drop the leftover `DoAction` / `ImportAssets` / empty
`ExportAssets` scaffolding from the main timeline. Worked example with the exact commands
in [`Untarnished/README.md`](Untarnished/README.md).

**4. Flatten the number keys if needed.** Sets that draw the full US key face show `!1`,
`@2`, `$4` on the number row. The shifted symbol and the digit are separate shapes, so you
can blank the symbol and re-centre the digit — worked example with exact character ids in
[`SkyUI/README.md`](SkyUI/README.md).

**5. Verify.**

```bash
ffdec-cli -export symbolClass sym out.swf   # STBKeycap must be listed
ffdec-cli -export sprite     png out.swf    # 323 PNGs; spot-check digits, Shift, Space, mouse
```

**6. Install** it as `Data/Interface/STB_Keycaps.swf`. Keep it there and keep it out of any
repository you publish: it is a derivative of someone else's art.

## Permissions — read this

The art belongs to whoever made that UI, and terms differ per mod. What matters here is the
distinction between *modify* and *redistribute*: every one of these permits the first, and
none of them is being relied on for the second, which is why no built set is tracked here or
shipped in the installer.

- **SkyUI** allows using and modifying its assets **as long as SkyUI is credited**, and not
  in paid mods. Its "seek permission from Psychosteve and Jelidity" note applies to the
  `icons_*.swf` files, **not** to `buttonart.swf`.
- **Untarnished UI** allows modifying and redistributing its assets. Credit **Vor/Vorganger**
  and **uranreactor** — and the **SkyUI Team** as well, because most of that keycap art is
  not Untarnished's own: byte-identical shapes trace it back through Dear Diary Dark Mode
  to SkyUI. Don't sell it.
- **Other overhauls vary**, and a reskin's terms do not cover art it inherited. Hash the
  shapes against SkyUI's before assuming who you need to credit.

Building a set locally from a UI mod you already have installed is your own business — that
is unambiguously fine, and it is the route this repository recommends. Publishing the result
is what needs permission, so a built set belongs on your disk only. The one SWF this project
does ship, `dist/Interface/MMOHotbar/Hotbar.swf`, is generated by
[`tools/build_hud_swf.py`](../tools/build_hud_swf.py) from our own slot art and embeds no
third-party bitmap, font or SWF.

