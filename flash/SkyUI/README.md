# SkyUI — the default set

Worked example of the recipe in [`../README.md`](../README.md). This is what ships by
default, because SkyUI is the one UI practically everyone already has.

**Source:** `interface/skyui/buttonart.swf` from `SkyUI_SE.bsa` — character **153**,
exported as `ButtonArt`, 323 frames, `Keyboard@1` `Mouse@256` `Gamepad@266`. 19 KB and
nothing but the button art, so no stripping is needed: only two edits.

**Result:** [`STB_Keycaps.swf`](STB_Keycaps.swf) — 18 KB, 152 shapes, 323 frames, with both
`ButtonArt` and `STBKeycap` exports on character 153 and the number row already flattened:
the symbol shapes 4, 6, 8 … 22 are degenerate 1×1 paths (~25 bytes each) while the digits
5, 7, 9 … 23 still carry their outlines (35–167 bytes) — that is what "blanked" and
"re-centred" reduce to in the file. It is committed here, so these steps only need running
again if you are adapting the set to another SkyUI-derived UI (Dear Diary and friends) —
then follow the recipe below against that UI's own `buttonart*.swf`. Keep
[`credits.txt`](credits.txt) next to the file.

## 1. Export alias

Append an `ExportAssets` tag mapping character **153** to `STBKeycap`, keeping the existing
`ButtonArt` entry (see the snippet in the parent README). Result:

```
153;"ButtonArt"
153;"STBKeycap"
```

## 2. Flatten the number row

SkyUI draws the real US key face, so the digits come with their shifted symbol above:
`!1`, `@2`, `$4`… The symbol and the digit are **separate shapes**, each placed on exactly
one frame (2..11), so nothing else is affected.

| key | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 0 |
|---|---|---|---|---|---|---|---|---|---|---|
| frame | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 |
| symbol chid — blank it | 4 | 6 | 8 | 10 | 12 | 14 | 16 | 18 | 20 | 22 |
| digit chid — shift it | 5 | 7 | 9 | 11 | 13 | 15 | 17 | 19 | 21 | 23 |

**Blank the symbol:** replace its SVG with a degenerate path.

```xml
<svg xmlns:ffdec="https://www.free-decompiler.com/flash" ffdec:objectType="shape"
     height="1.0px" width="1.0px" xmlns="http://www.w3.org/2000/svg">
  <g transform="matrix(1.0, 0.0, 0.0, 1.0, 0.0, 0.0)">
    <path d="M0.0 0.0 L0.0 0.0 0.0 0.0 0.0 0.0" fill="#cccccc" />
  </g>
</svg>
```

**Re-centre the digit.** On a key face the digit sits bottom-right, so removing the symbol
alone leaves it off-centre by **dx +3.5px, dy +1.0..1.5px** — visibly shoved into the
corner. Fix it in the digit's own SVG group transform:

```xml
<g transform="matrix(1.0, 0.0, 0.0, 1.0, -3.50, -1.00)">
```

dy is **-1.0** for keys 1, 3, 5, 6, 8, 0 and **-1.5** for 2, 4, 7, 9.

Export the shapes first (`ffdec-cli -export shape svg buttonart.swf`), edit them, then
import all twenty in one pass:

```bash
ffdec-cli -importShapes in.swf out.swf <folder with 4.svg 5.svg 6.svg …>
```

## 3. Verify

The reference for "centred" is SkyUI's own `hotkeyIcon` — character **157** in its
`favoritesmenu.swf`, frames 1..8 = digits 1..8, which is what SkyUI shows for vanilla
favorites hotkeys. Render both and compare: the digit's ink centre should land within
±0.5px of the key box centre.

Final file: ~18 KB, 152 shapes, both exports, all 323 frames intact.

## Credit

SkyUI's terms require crediting SkyUI for asset use. Keep the line in the mod's README and
Nexus description.
