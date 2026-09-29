# Untarnished UI — second set

The other recipe, for the harder case. Use it if you run
[Untarnished UI](https://www.nexusmods.com/skyrimspecialedition/mods/75188); otherwise
[build from SkyUI](../SkyUI/README.md), which is the common case.

**The built `STB_Keycaps.swf` is not committed here and not shipped in the installer** — the
art is Untarnished UI's (and largely SkyUI's), and those mods permit modifying it, not
redistributing it. Build it locally and install it to `Data/Interface/STB_Keycaps.swf`.

Unlike SkyUI, Untarnished has no standalone `buttonart.swf` — its keycaps live **inside**
`interface/favoritesmenu.swf`, mixed in with the whole menu. So this is the worked example
for the harder case in [`../README.md`](../README.md): pulling one clip out of a big SWF.

**Source:** `interface/favoritesmenu.swf` (120 KB, 599 characters) — `DefineSprite` **157**,
no export name of its own, 323 frames, `Keyboard@1` `Mouse@256` `Gamepad@266` `Unused@290`
`PS3@302`. The frame layout is the SkyUI one, so it drops straight in.

**Result:** 37 KB, 147 shapes, one sprite, one export, 323 frames — byte-identical to the
copy upstream publishes as the optional *"STB Hotkey System - Untarnished UI keycaps"* Nexus
download. Run the build below once per install; if a future Untarnished release moves the clip
off character **157**, the recipe in [`../README.md`](../README.md) covers finding it again.

The digits need no work here: Untarnished draws a bare glyph per key, centred, with no
shifted symbol above it — the `@2` / `$4` problem is specific to SkyUI's full key faces.

## Build it

Put your copy of `favoritesmenu.swf` in a plain folder (FFDec chokes on spaces and
brackets in paths) next to [`build.py`](build.py), and run it:

```bash
python build.py
```

It writes `STB_Keycaps.swf` and prints what it removed. Three steps:

**1. Find what the clip actually needs.** `ffdec-cli -dumpSWF` gives every tag with its
character id and nesting. Walking that gives, for each `DefineSprite`, the characters its
frames place; the transitive closure from 157 is the set to keep — 148 of 599. Everything
else is the favorites menu itself (list entries, buttons, scroll bars, fonts) and goes:

```bash
ffdec-cli -removeCharacter favoritesmenu.swf stripped.swf <the other 449 ids>
```

**2. Strip the scaffolding.** `-removeCharacter` leaves the main timeline's `DoAction`
(the menu's frame script), its `ImportAssets2` stub, and one empty `ExportAssets` per
character it deleted. The script drops tag codes 12, 56, 57 and 71 from the main timeline
by walking the raw tag stream — `DefineSprite` bodies stay opaque blobs, so "main timeline"
needs no guessing about the dump's indentation.

**3. Export the clip.** Append an `ExportAssets` mapping character 157 to `STBKeycap`
(same snippet as the parent README). The sprite has no export name in the source, so there
is nothing to collide with.

## Verify

```bash
ffdec-cli -export symbolClass sym STB_Keycaps.swf   # 157;"STBKeycap", and nothing else
ffdec-cli -format shape:png -export shape png STB_Keycaps.swf
```

Character **559** is Esc (scancode 1), so **560**–**569** are the digits 1…0 — check those
render as plain centred numerals. Every defined character should also be placed by the
clip: an orphan means the closure was computed wrong, an undefined-but-placed one means
too much was removed.

## Credit

Untarnished's permissions allow modifying and redistributing its assets. Credit
**Vor/Vorganger**, **uranreactor** and the **SkyUI Team**, and don't sell the asset. The
SWF is not covered by the plugin's GPL-3.0 — it is someone else's art, redistributed under
their terms.
