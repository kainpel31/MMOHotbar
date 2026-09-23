# MMO Hotbar

MMO-style **24-slot hotbar** SKSE64 plugin — a **full fork** of
[STB Hotkey System](https://github.com/STB-Team/STB-Hotkey-System)
(**GPL-3.0-only**, see [LICENSE](LICENSE)) plus an MMO view layer, all in
**one SKSE DLL** (`MMOHotbar.dll`). No separate addon plugin needed.

Slots **1–12** = keyboard keys `1..=`, slots **13–24** = `X+1..X+=`
(preset modifier from `MMOHotbar.ini`, default `45 = X`).
Bind through the normal assign flow and the slot lights up automatically.

## Core (from STB, unchanged behaviour)

Chord binds (`modifier + key`, optional 2-key chords) assigned live from the
inventory / magic / favorites menus. Groups (second modifier stacks items onto
one key, one press equips the set). Keycaps drawn on list rows. Remembered
hand (`R`/`L`). Instance-aware binds. Co-save persistence. Vanilla 1–8
migration. Conflict warnings. EN/RU strings. Plugin API
(`api/STB_HotkeySystemAPI.h`, still exported for other mods).

See upstream [README](https://github.com/STB-Team/STB-Hotkey-System) and
[`flash/README.md`](flash/README.md) for the keycap recipe.

## MMO layer (new in this fork)

- `src/HotbarHUD.{h,cpp}` — 24-slot view over the chord table
  (`BindOfSlot` / `SlotOfBind` / `Snapshot` / `ExecuteSlot` / `SlotLabel`).
- `src/HotbarConsole.{h,cpp}` — Papyrus API `MMOHotbar.FireSlot(1-24)`
  and `MMOHotbar.DumpSlots()` so a HUD/SWF can fire slots by script.
- `dist/SKSE/Plugins/MMOHotbar.ini` — same keys as STB plus
  `[Hotbar] iPresetModifierScanCode = 45`.
- DLL/plugin renamed to `MMOHotbar` v2.0.0.

## Building

Same as upstream: `VCPKG_ROOT`, VS2022, triplet `x64-windows-static`.

```powershell
.\gen.ps1                       # configure (prompts for a deploy path)
cmake --build build --config Release
```

Deps from `vcpkg.json`: CommonLibSSE-NG, spdlog, nlohmann_json, xbyak, ...

## License & credits

**GPL-3.0-only** for the plugin code ([LICENSE](LICENSE)); this fork keeps every
upstream attribution header. Upstream by **STB**; SWF machinery adapted from
Dynamic Inventory Icon Injector by **JerryYOJ** (GPL-3.0, permission granted
upstream); inspiration **Vermunds** (Extended Hotkey System).

### The keycaps are someone else's art, under someone else's terms

`Interface/STB_Keycaps.swf` is the one file here we did not make, and it is **not
covered by our GPL-3.0**. It is cut out of **Untarnished UI**'s
`interface/favoritesmenu.swf` — credit **Vor / Vorganger** and **uranreactor** —
and most of those keycap shapes trace, shape for shape, back to **SkyUI**'s
`ButtonArt`, so the **SkyUI Team** is credited for them too. Untarnished's
permissions allow **modifying and redistributing** the asset **as long as the
authors are credited**, and forbid selling it or shipping it in a paid mod. That
permission is the *only* reason the art may be bundled at all — the GPL covers the
DLL, never the picture on the key.

So the credit has to travel with the file, which is what
`flash/<set>/credits.txt` is for: `package.ps1` copies it into the installer next
to the SWF as `Keycap art credits.txt`. Do not delete it, and keep the credit line
in your mod page description.

Both sets are committed here, and they are **not** interchangeable files — same
export contract, different art:

| set | file | source | size |
|---|---|---|---|
| SkyUI (the default) | `flash/SkyUI/STB_Keycaps.swf` | `interface/skyui/buttonart.swf`, character **153** (keeps its `ButtonArt` export, gains `STBKeycap`) | 18 KB |
| Untarnished UI | `flash/Untarnished/STB_Keycaps.swf` | `interface/favoritesmenu.swf`, character **157** cut out of the menu | 37 KB |

You can tell them apart by shape count (152 vs 147) or by the number keys: the
Untarnished set defines characters 560–569 for `1`…`0`, the SkyUI set defines
characters 4, 6, 8…22 for the `!` `@` `#` symbols it draws above the digits. Picking
the wrong one is not fatal — mismatched art just looks off — but the FOMOD asks
which UI you run for a reason.

### What "build it locally from the UI mod you have installed" means

Nothing in the C++ build touches the SWF: the plugin never draws a keycap, it only
attaches one clip from whatever `Interface/STB_Keycaps.swf` is installed and jumps
it to a frame. The mod runs with no SWF at all — you just get no key glyphs.

Upstream commits **no** `.swf` and asks you to produce it from a UI mod you already
have, because *building* the file from art on your own disk is unambiguously fine,
whereas *redistributing* it needs the author's permission. This fork commits both
sets because those permissions are granted (with credit, which we ship). The
upshot for you: **there is no build step to run.** `flash/` is documentation plus
the two finished files.

- **Untarnished UI** — done, `flash/Untarnished/STB_Keycaps.swf`. Upstream also
  publishes the identical file as the optional **"STB Hotkey System - Untarnished UI
  keycaps"** download on its Nexus page; either copy gives the same 37 KB SWF. Only
  re-run [`flash/Untarnished/build.py`](flash/Untarnished/build.py) if a future
  Untarnished release moves the clip to another character id.
- **SkyUI** — done, `flash/SkyUI/STB_Keycaps.swf` (the set STB ships as its default,
  taken from the installed mod). Rebuilding it from your own `SkyUI_SE.bsa` is only
  needed for a different SkyUI-derived UI: the two edits are an export alias and
  blanking/re-centring the `@2` `$4` key faces, exact character ids in
  [`flash/SkyUI/README.md`](flash/SkyUI/README.md).
- **Any other overhaul** — find the clip, confirm the `Mouse@256` / `Gamepad@266`
  frame layout, add the `STBKeycap` export; the full recipe with a copy-paste
  Python snippet is in [`flash/README.md`](flash/README.md).

