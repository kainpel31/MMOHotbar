# MMO Hotbar

MMO-style **12-slot hotbar** SKSE64 plugin — a **full fork** of
[STB Hotkey System](https://github.com/STB-Team/STB-Hotkey-System)
(**GPL-3.0-only**, see [LICENSE](LICENSE)) plus an MMO view layer, all in
**one SKSE DLL** (`MMOHotbar.dll`). No separate addon plugin needed.

The bar has **two independent 12-slot preset banks**. Tap `X` (configurable in
`MMOHotbar.ini`) to switch banks; the same key can carry a different item in each
preset. Bind through the normal assign flow and the visible bank updates automatically.

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

- `src/HotbarHUD.{h,cpp}` — 12-slot view over the active preset bank
  (`BindOfSlot` / `SlotOfBind` / `Snapshot` / `ExecuteSlot` / `SlotLabel`).
- `src/HotbarHUDView.{h,cpp}` — draws that view. Hooks `HUDMenu::AdvanceMovie`
  (vfunc `0x5`), creates an empty MovieClip on the HUD movie's `_root` and
  `loadMovie`s `Interface/MMOHotbar/Hotbar.swf` into it, then writes each slot's
  key into the SWF's own `key<i>` text field. It hides while a menu owns the
  screen, and re-attaches if the game rebuilds the HUD (save load / new game).
- `src/HotbarConsole.{h,cpp}` — Papyrus API `MMOHotbar.FireSlot(1-12)`,
  `MMOHotbar.GetPreset()` and `MMOHotbar.DumpSlots()` so a HUD/SWF can fire slots
  by script. `MMOHotbar.FireSlotSwap` remains as a deprecated alias of `FireSlot`.
- Preset toggle: tapping `X` alone flips preset 1 ↔ 2 (persisted in the co-save
  `PRST` record). `X` is reserved: it never forms a binding, and assignment,
  keycaps, firing, and plugin lookups always follow the visible bank.
- `dist/SKSE/Plugins/MMOHotbar.ini` — same keys as STB plus
  `[Hotbar] iPresetModifierScanCode = 45`.
- DLL/plugin renamed to `MMOHotbar` v2.0.0.

### The bar's movie, and what it does not draw yet

`dist/Interface/MMOHotbar/Hotbar.swf` is our own build (`python tools/build_hud_swf.py`,
see [Tools](#tools)) and is tracked in git, so the installer always ships it. Per slot it
defines `frame<i>` (the art), `key<i>` (a text field the plugin writes) and `icon<i>` (an
empty clip). **The `icon<i>` clips stay empty:** CommonLibSSE-NG exposes no per-form icon
index, so which frame of which icon sheet belongs to a given form cannot be derived here
without guessing. The frames and key labels are correct; a slot shows its frame and the key
bound to it, with no item picture.

## Tools

Two Python files build the hotbar's SWF. Nothing in the C++ build touches them — CI compiles
the plugin from `src/` and ships the committed `Hotbar.swf` — so you only need Python if you
are changing the bar's artwork or geometry.

**Requirements:** Python 3.9+, and [Pillow](https://python-pillow.org/) (`pip install pillow`).
[FFDec](https://github.com/jindrapetrik/jpexs-decompiler)'s `ffdec-cli.exe` is optional and
only used to verify the output; the script looks for it on `PATH`, then in
`%LOCALAPPDATA%\FFDec\`, then in `D:\FlashDecompire\`.

### `tools/swflib.py` — a minimal SWF writer

Not a general library: just the ~20 tags this project's movies need, written straight to
bytes with `struct` and `zlib`. It exists because the bar's SWF is built with **no
ActionScript at all** — the plugin drives it by addressing named display objects — so there
is no need for a full authoring toolchain or a Flash compiler.

| function | writes |
|---|---|
| `BitW` | MSB-first bit writer (`bit` / `bits` / `sbits` / `align`); the SWF format packs several fields into shared bitstreams, shape records especially |
| `tag_bytes(code, body)` | tag header — `u16 (code<<6 | len)`, or the long form `\| 0x3F` + `u32` when the body is ≥ 63 bytes |
| `rect`, `matrix`, `header` | the `RECT` / `MATRIX` / file-header primitives |
| `file_attributes`, `set_background` | tag 69, tag 9 |
| `define_bits_lossless2` | tag 98, a zlib-compressed ARGB bitmap (format 5) |
| `define_shape2` | tag 32, one fill of one bitmap, as a single continuous bitstream |
| `place_object2` | tag 26, places a character at a depth, optionally **naming** it — this is how `frame0`, `icon0`, `key0` get their names |
| `define_edit_text` | tag 37, a read-only dynamic text field. Deliberately carries **no font** (`HasFont`/`HasFontClass` clear), so the movie needs no `DefineFont2` and embeds no font |
| `define_sprite`, `define_empty_sprite`, `show_frame`, `end_tag` | tag 39 and friends |
| `export_assets` | tag 56, the export table |
| `save(path, w, h, tags, …)` | assembles header + tags, back-patches the real file length, writes it |

Two format details that are easy to get wrong and are commented in the source: the
`StyleChangeRecord` flag order in a shape is `NewStyles, LineStyle, FillStyle1, FillStyle0,
MoveTo` with the `MoveTo` data first, and an edge's `NumBits` field is `bits - 2`.

### `tools/build_hud_swf.py` — builds `dist/Interface/MMOHotbar/Hotbar.swf`

```bash
python tools/build_hud_swf.py                       # default frame art and output path
python tools/build_hud_swf.py --frame my_slot.png  # your own 64x64 slot art
python tools/build_hud_swf.py --out /tmp/Hotbar.swf
```

It flattens `--frame` to one 64×64 cell, embeds it once, and lays out 12 slots of three
named objects each. The geometry is a set of named constants at the top of the file
(`SLOTS`, `SLOT_PX`, `ICON_INSET`, `KEY_TOP`, `KEY_HEIGHT`, `TWIPS`), and Flash's unit is
1 px = 20 twips.

The output is character-id-addressed, which is the contract the C++ relies on:

| ids | tag | what it is |
|---|---|---|
| 1 | `DefineBitsLossless2` | the slot art, embedded once and shared |
| 2 | `DefineShape2` | paints that bitmap into a cell |
| 10…21 | `DefineSprite` | the 12 empty `icon<i>` clips |
| 30…41 | `DefineEditText` | the 12 `key<i>` labels, pre-filled with `1`…`12` |

`frame<i>`, `icon<i>` and `key<i>` are then placed on the main timeline with `PlaceObject2`
at depths `10 + 3i`, `11 + 3i`, `12 + 3i`, bottom layer first. **`kBarWidth`/`kBarHeight` in
[`src/HotbarHUDView.cpp`](src/HotbarHUDView.cpp) must stay in step with `SLOT_PX` and
`SLOTS`** — the C++ positions the clip from those numbers.

After writing, `verify()` round-trips the file through `ffdec -swf2xml` and asserts the tag
counts (1 bitmap, 1 shape, 12 sprites, 12 texts) and that every `frame<i>`/`icon<i>`/`key<i>`
name is present. It exits non-zero on a mismatch, so it works as a pre-commit check. With
FFDec absent it prints a warning and skips the check — the build still succeeds.

## Building

Deps come from `vcpkg.json` (CommonLibSSE-NG, spdlog, nlohmann_json, xbyak, simpleini, …),
triplet `x64-windows-static`, and CMake needs `VCPKG_ROOT` set.

### In the cloud — no Visual Studio on your PC

`.github/workflows/build.yml` builds the DLL on GitHub's `windows-latest` runner and
uploads **two artifacts**: `MMOHotbar-plugin` (the DLL + PDB) and `MMOHotbar-FOMOD` (the
installer `package.ps1` stages, keycap SWFs and their credit notes included). Push to
`main`, or trigger it by hand from **Actions → build → Run workflow**, then download the
artifacts from the run's summary page.

Two things the workflow does on purpose:

- it uses the **`ci`** CMake preset, not `default`. Only `default` pins the
  "Visual Studio 18 2026" generator and toolset **v145**, which is what a modern local
  install has; no GitHub runner ships them (`windows-latest` has Visual Studio 2022), so
  `ci` pins no generator at all and leaves `COPY_BUILD` off — `MMOHOTBAR_DEPLOY_DIR` is a
  path on your own PC.
- it builds **CommonLibSSE-NG from source** at the commit in `COMMONLIBSSE_NG_COMMIT`
  rather than taking the vcpkg port, for the SE/AE misclassification reason documented at
  the top of `CMakeLists.txt`.

This route needs no compiler, CMake or vcpkg locally. The first run is slow (vcpkg compiles
Boost and the NG dependencies); later runs restore them from the binary cache.

### On your own machine

```powershell
.\gen.ps1                       # configure (prompts for a deploy path)
cmake --build build --config Release
```

If your Visual Studio is 2022 or older, use the `ci` preset instead — the `default` preset
will not configure:

```powershell
cmake --preset ci -DCommonLibSSEPath_NG=<path to a CommonLibSSE-NG checkout>
```

### Getting this onto GitHub

This folder is already published: `origin` is `kainpel31/MMOHotbar`. Every push to `main`
runs the workflow above, and you install straight from the run's artifacts — no compiler,
CMake or vcpkg on your PC at any point. The routes below are kept for setting up a
different repository.

**Route 1 — VS Code, nothing extra to install.** VS Code already ships the GitHub sign-in
provider (`github-authentication`) and the Pull Requests extension, and `git.path` already
points at the git bundled with GitHub Desktop — which carries Git Credential Manager.

1. `File → Open Folder…` → this folder.
2. **Accounts** icon (bottom-left) → *Sign in with GitHub* → approve in the browser.
3. **Source Control** (`Ctrl+Shift+G`) → **Publish Branch**. Name it `MMOHotbar`, and pick
   **private** unless you want it public. VS Code creates the repository, sets `origin` and
   pushes `main`.
4. That push starts the build on its own. The **GitHub Actions** extension (already
   installed) lists the run behind the Actions icon in the sidebar; the same run is at
   `github.com/<your-name>/MMOHotbar/actions`. The **Actions → build → Run workflow** button
   there re-runs it without committing anything.
5. From the finished run's **Artifacts** box:
   - `MMOHotbar-plugin` — `MMOHotbar.dll` and `MMOHotbar.pdb`
   - `MMO-Hotbar-FOMOD` — the installer, with the keycap SWF and its credit note inside

If a push ever asks for a password, git hands off to the **Git Credential Manager** inside
GitHub Desktop's git (`credential.helper` is already `manager` there): one browser window,
then the token is remembered.

Two caveats worth knowing:

- `git.path` points inside `…\GitHubDesktop\app-<version>\…`, and a GitHub Desktop update
  changes `<version>` — VS Code would then say it cannot find git. Re-point the setting, or
  install Git for Windows and point at `C:\Program Files\Git\cmd\git.exe`.
- **Private is the safe default here.** `flash/*/STB_Keycaps.swf` is third-party art (SkyUI
  Team; Vor/Vorganger + uranreactor) that may be redistributed only with credit and must not
  be sold, which is exactly why each SWF is tracked together with its `credits.txt` — see
  [`flash/README.md`](flash/README.md). A private repo sidesteps the question entirely.

**Route 2 — GitHub Desktop** (already signed in on this PC): `File → Add Local Repository…`
→ this folder → **Publish repository**. Same result, using the token it already stores.

**Route 3 — by hand**, with any git on PATH:

```powershell
git remote add origin https://github.com/<your-name>/MMOHotbar.git
git push -u origin main
```

The repository name does not affect the build: the DLL, the artifacts and the installer take
their names from `CMakeLists.txt`, not from the repo.

## License & credits

**GPL-3.0-only** for the plugin code ([LICENSE](LICENSE)); this fork keeps every
upstream attribution header. Upstream by **STB**; SWF machinery adapted from
Dynamic Inventory Icon Injector by **JerryYOJ** (GPL-3.0, permission granted
upstream); inspiration **Vermunds** (Extended Hotkey System).


### No third-party art is tracked or shipped

The repository contains exactly **one** `.swf`: `dist/Interface/MMOHotbar/Hotbar.swf`. It is
generated by [`tools/build_hud_swf.py`](tools/build_hud_swf.py) from our own slot art and
embeds no third-party bitmap, font or SWF.

The keycap sets are the opposite case, and they are **not** in this repository and **not** in
the installer. `flash/SkyUI/STB_Keycaps.swf` and `flash/Untarnished/STB_Keycaps.swf` are
derivative works of SkyUI's `buttonart.swf` (character **153**) and Untarnished UI's
`favoritesmenu.swf` (character **157**). Those mods grant permission to *modify* their assets
— Untarnished also to redistribute, with credit — but neither grants a blanket right for us
to republish the result, and the art inherits terms from however many reskins it has passed
through. Rather than rely on a permission chain we would have to re-verify for every derived
set, the files stay off git and out of `package.ps1`, and the installer no longer offers a
keycap choice it cannot honour.

Nothing is lost functionally. The plugin never draws a keycap itself: it attaches one clip
from whatever `Data/Interface/STB_Keycaps.swf` is present and jumps it to a frame. With no
such file the import finds no movie, returns early, and the hotkeys work exactly as before —
the only difference is that list rows show no key glyph. The hotbar's own HUD movie is
independent of all this and is always installed.

If you want the glyphs, build a set from a UI mod you already have and install it yourself;
[`flash/README.md`](flash/README.md) has the full recipe plus the export contract (one clip
named `STBKeycap`, keyboard at the DX scancode, mouse at `256 +` button, gamepad at
`266 +` button). Building from art on your own disk is unambiguously fine; publishing the
result is what needs permission, so a built set belongs on your disk only.

## References

Open-source Skyrim SE mods that solve overlapping problems. **The licence column matters** —
several of the most useful ones ship no licence at all, which means *no permission to reuse
their code*, only to read it. Licence state below was checked against each repository's
`license` field and root file listing; star counts drift and are approximate.

| project | lang | licence | what it is worth here |
|---|---|---|---|
| [STB-Team/STB-Hotkey-System](https://github.com/STB-Team/STB-Hotkey-System) | C++ | GPL-3.0 | the upstream this is a fork of; our chord model, assign flow and plugin API come from it |
| [ahzaab/moreHUDSE](https://github.com/ahzaab/moreHUDSE) | C++ | **GPL-3.0** | the closest licensed precedent for `HotbarHUDView`: it loads its own `.swf` into the HUD Menu when the menu loads, and ships the SWF as build output — the same split of "our art, tracked" we use |
| [ceejbot/soulsy](https://github.com/ceejbot/soulsy) | Rust | **GPL-3.0** | "a minimal Souls-like HUD", SKSE plugin. Worth reading for how it decides when a HUD element is shown and how little it costs per frame — the same problem our per-frame diffing solves |
| [skyrim-multiplayer/skymp](https://github.com/skyrim-multiplayer/skymp) | C++ | none declared | the only real MMO hotbar in Skyrim, and the closest thing to this mod's goal. Read-only: there is no `LICENSE` file on `main`, so nothing here may be taken from it |
| [pWn3d1337/Skyrim_SpellHotbar2](https://github.com/pWn3d1337/Skyrim_SpellHotbar2) | C++ | none declared | a spell hotbar rebuilt as a pure SKSE plugin, WIP. Read-only, same reason |
| [pWn3d1337/Skyrim_SpellHotbar](https://github.com/pWn3d1337/Skyrim_SpellHotbar) | C++ | none declared | the earlier version; its `python_scripts/` and `SWF_Generator/` are the prior art for generating hotbar SWF assets from Python |
| [expired6978/SKSE64Plugins](https://github.com/expired6978/SKSE64Plugins) (`hudextension`) | C++ | none declared | the popular `createEmptyMovieClip` + `loadMovie`-into-`_root` approach, done with hand-patched SE offsets. Ours is the same four documented Scaleform calls, resolved through CommonLibSSE-NG relocations instead. Read-only |
| [schlangster/skyui](https://github.com/schlangster/skyui) | ActionScript | none declared | the menu/keycap contracts we document in `flash/README.md` — the `ButtonArt` frame layout and `icons.item.source` come from here. Read-only |

Three of these are GPL-3.0, the same as this project; the other five declare no licence at
all, which for a repository means all rights reserved by default. The practical rule we
follow: take *ideas* and *format contracts* from any of them, copy no code from the
unlicensed ones, and re-derive anything we keep on the relocation-based API this
project is built on.