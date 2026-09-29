# Repository Form — MMO Hotbar

> A structured summary of **what this repository is** and **what it does**.
> Every claim here points at the file it comes from, so it can be checked.
> Last reviewed against commit `2eb14ff`.

---

## 1. Identity

| field | value |
|---|---|
| **Name** | MMO Hotbar |
| **One-liner** | MMO-style 12-slot hotbar for Skyrim SE, built on a chord-based favorites hotkey core |
| **Version** | 2.0.0 (`fomod/info.xml`, `src/main.cpp` → `PluginVersion(2)`) |
| **License** | GPL-3.0-only (`LICENSE`) |
| **Author** | MMOHotbar — a fork of STB Hotkey System by **STB** |
| **Upstream** | [STB-Team/STB-Hotkey-System](https://github.com/STB-Team/STB-Hotkey-System) |
| **Repository** | `https://github.com/kainpel31/MMOHotbar.git` |
| **Language** | C++ (plugin), Python (SWF tooling), PowerShell (packaging) |
| **Platform** | Skyrim **Special Edition** (SE 1.5.97) and **Anniversary Edition** (AE 1.6.629+) via SKSE64 |
| **Artifact** | one DLL — `MMOHotbar.dll` — plus a FOMOD installer |
| **Groups** | Gameplay, User Interface (`fomod/info.xml`) |

---

## 2. What this repository is

A **full fork** of the STB Hotkey System with an MMO-style action-bar layer bolted on top.

STB solves the problem that *vanilla Skyrim favorites hotkeys are unreliable* — the engine
renumbers and clears them on stack splits, equips and container moves. This mod stores a
binding as a plugin-owned `chord → form` table mirrored into a co-save record, so binds survive
everything the engine does to vanilla.

The fork adds what an MMO player expects: a **12-slot bar rendered on the HUD**, with **two
independent preset banks** you flip between with a single tap. Everything stays in **one
DLL** — there is no separate addon plugin to install or keep in sync.

**It is not** a multiplayer mod, and it is not a reskin of SkyUI — it is a gameplay/UI plugin
that draws its own SWF into the game's HUD menu.

---

## 3. Architecture

```
MMOHotbar.dll   (one SKSE64 plugin, no companion addon)
├── SKSEPlugin_Load            → Serialization::Register, log init   (src/main.cpp)
├── kDataLoaded                → Settings, Localization, FavoritesHook, InventoryIcons,
│                                MenuInputBlock, InputHandler, PickupWatch,
│                                HotbarHUD, HotbarHUDView, HotbarConsole
└── kPostLoadGame / kNewGame   → VanillaMigration, ModifierConflict prompt
```

| path | role |
|---|---|
| `src/HotbarHUD.*` | the model — a **view** over the STB chord table |
| `src/HotbarHUDView.*` | the renderer — hooks `HUDMenu::AdvanceMovie`, loads the SWF |
| `src/HotbarConsole.*` | Papyrus scripting API |
| `src/InputHandler.*` | key-down sink; resolves chords against live pressed state |
| `src/EquipDispatch.*` | equip/toggle/group semantics, hand-aware |
| `src/InventoryIcons.*` | keycap drawing on inventory/magic/favorites rows |
| `src/Serialization.*` | co-save records (`HKSY`/`HOTK` binds, `PRST` active preset) |
| `src/PluginAPI.cpp` | the cross-DLL C++ API (v1) |
| `api/` | header + Papyrus `.psc` + worked example |
| `tools/` | Python SWF/icon pipeline |
| `dist/` | the one shipped SWF + the INI |
| `fomod/` | installer manifest |
| `src/RE/Offsets.Ext.h` | game offsets |
| `src/swfhelper/` | vendored SWF writer (attributed) |

---

## 4. Features

### 4.1 The MMO hotbar layer — new in this fork

- **12-slot bar** on the HUD, driven by `Interface/MMOHotbar/Hotbar.swf`
- **Two independent 12-slot preset banks.** The same physical key (`F`, `Z`, `F1`, `Home`…)
  can carry a **different item in each preset**
- **One-tap toggle** between banks — `X` by default (`iPresetModifierScanCode = 45`).
  It is *reserved*: it never forms part of a chord and never has to be held
- **Everything follows the visible bank** — assignment, keycaps, firing, plugin lookups.
  A displayed key equips exactly what the bar shows
- **Slots are your own layout.** The 12 slots are just the active bank's bindings in
  assignment order, so nothing is configured twice
- **HUD-safe lifecycle:** hides while a menu owns the screen, and re-attaches when the game
  rebuilds the HUD (save load / new game)

### 4.2 Chord core — inherited from STB, behaviour unchanged

- **Chord binds** — `modifier + key`, with optional **2-key chords** (`bEnableChords`)
- **Live assignment** from the inventory, magic and favorites menus
- **Groups** — a second modifier stacks items onto one key; one press equips the set
- **Keycaps drawn on list rows** in every supported menu, individually toggleable
- **Remembered hand** (`R`/`L`) — put every bound entry back the way you had it
- **Instance-aware binds** — a plain sword and its enchanted copy are separate bindings
- **Co-save persistence** — binds live in the save, not in the game world
- **Vanilla 1–8 migration** — existing number-row favorites are moved over on load
- **Conflict warnings** before you overwrite a key the game already uses
- **EN / RU** localization

### 4.3 Scripting & plugin API

**Papyrus** (`api/MMOHotbarPapyrus.psc`):

| call | does |
|---|---|
| `MMOHotbar.FireSlot(1..12)` | fire a slot on the active preset |
| `MMOHotbar.GetPreset()` | which bank is live |
| `MMOHotbar.DumpSlots()` | dump the slot table to the log |
| `MMOHotbar.FireSlotSwap()` | *deprecated* alias of `FireSlot`, kept so old scripts keep working |

**C++ API** (`api/STB_HotkeySystemAPI.h`, v1) — plain structs and counts only, so no
CommonLib or STL type crosses the DLL boundary and version skew is impossible:

| call | does |
|---|---|
| `Resolve(device, key, out, max)` | what pressing that key fires *right now*, chords included |
| `EquipNow(binding)` | equip synchronously, and **claim the press** so it is not fired twice |
| `GetHotkey(form)` | the chord a form sits on, any instance |
| `GetHotkeyExact(binding)` | the same for one specific instance — what an item list wants |

Versioned: a future `IVersion2` is a separate class; asking for `1` keeps working.

### 4.4 Tooling

- `tools/swflib.py` — a minimal SWF writer (~20 tags, `struct` + `zlib`, **no ActionScript**)
- `tools/build_hud_swf.py` — builds `dist/Interface/MMOHotbar/Hotbar.swf`, then
  **verifies it** by round-tripping through FFDec and asserting the tag counts
- `tools/iconsheet.py` · `tools/build_hotbar.py` · `tools/render_icons.py` ·
  `tools/icon_aliases.txt` — the icon pipeline, keyed on **EditorID** so it is
  locale-independent (`GetName()` is localized; EditorIDs are not)
- `package.ps1` — stages the FOMOD; `gen.ps1` — local CMake configure

### 4.5 Configuration — `dist/SKSE/Plugins/MMOHotbar.ini`

| section | controls |
|---|---|
| `Assignment` | bind modifier, group modifier, chords, menu-key blocking, conflict warnings |
| `Icons` / `IconsContainer` / `IconsMagic` / `IconsFavorites` | keycap on/off, scale, position, hand label — per menu |
| `Gameplay` | `bRememberHand` |
| `Compatibility` | vanilla 1–8 migration |
| `Debug` | verbose log |
| `Hotbar` | `iPresetModifierScanCode` (the X toggle) |

`STB_HotkeySystem.ini` is still read as a base layer, so existing STB settings carry over.

---

## 5. Requirements

| for | needs |
|---|---|
| **to play** | Skyrim SE 1.5.97, or AE 1.6.629+, + SKSE64 |
| **to build** | Visual Studio 2022 **or** 2026, CMake, vcpkg with `VCPKG_ROOT` set, triplet `x64-windows-static` |
| **to rebuild the SWF** | Python 3.9+ and Pillow; FFDec's `ffdec-cli.exe` optional (verification only) |
| **to build in the cloud** | nothing — GitHub Actions does it |

Build deps: spdlog, nlohmann_json, xbyak, simpleini, boost — plus a **checkout** of
CommonLibSSE-NG ([`alandtse/CommonLibSSE-NG`](https://github.com/alandtse/CommonLibSSE-NG)),
which is deliberately not a vcpkg dependency.

---

## 6. How a build ships

Push to `main` → `.github/workflows/build.yml` builds on `windows-latest` and uploads
**two artifacts**:

- **`MMOHotbar-plugin`** — `MMOHotbar.dll` + `.pdb`
- **`MMO-Hotbar-FOMOD`** — the installer

Two deliberate choices in the workflow: it uses the **`ci`** preset (no generator pinned, so
it configures on the runner's VS 2022), and it builds **CommonLibSSE-NG from source** —
[`alandtse/CommonLibSSE-NG`](https://github.com/alandtse/CommonLibSSE-NG), the maintained
fork — at the commit pinned in `COMMONLIBSSE_NG_COMMIT`, rather than taking the vcpkg port,
which is no longer a dependency at all.

Locally: `.\gen.ps1` then `cmake --build build --config Release`.

---

## 7. Known limitations — read this before expecting the icons

| limitation | detail |
|---|---|
| **Hotbar icons are per KIND, not per item** | The `icon<i>` clips are filled at run time from an icon sheet already on the player's disk (`Interface/SkyUI/IconsItem_PsychoSweve.swf` and friends), addressed by the sheet's own frame labels. Picking one needs only the form's kind — `TESObjectWEAP::GetWeaponType()`, or the biped slot mask plus armour type — because the per-form icon index lives in the game's `ItemMenu`, which CommonLibSSE-NG does not expose. Every iron sword shows the same sword icon. |
| **Spells and potions share one frame each** | Schools (`default_alteration` … `default_restoration`) need the MGEF effect type and potion effects (`potion_health`, `potion_poison`, …) need the potion's effect list — neither is exposed by CommonLibSSE-NG, so spells get `default_effect` and potions `default_potion`. Potions are matched on the raw form type 0x2E because this library names that slot `AlchemyItem` and has no `Potion` member; both Papyrus references put `Potion` there. |
| **No icon sheet on disk → no icons** | Not an error. With no UI overhaul installed, no sheet loads and the bar keeps its frames and keycaps; the log says so once at attach. |
| **Keycaps are no longer bundled** | `STB_Keycaps.swf` is not shipped. SkyUI and Untarnished UI permit *modifying* their art, not redistributing it, so the files are off git and out of the installer. With no `Interface/STB_Keycaps.swf` the keycap import returns early and the hotkeys work exactly as before — list rows simply show no key glyph. |
| **`src/HotbarIconTable.h` is unused** | It was a stub for the abandoned "bake our own sheet into the SWF" approach, which the run-time approach above replaced. Nothing includes it, `kTable` is empty and `Raw()` does not normalize. Safe to delete. |
| **AE below 1.6.629 unsupported** | `UsesStructsPost629(true)`. SE is unaffected. |

---

## 8. Credits

- **STB** — [STB Hotkey System](https://github.com/STB-Team/STB-Hotkey-System), GPL-3.0. This
  fork keeps every upstream attribution header; the chord model, assign flow and plugin API
  come from there
- **JerryYOJ** — Dynamic Inventory Icon Injector, GPL-3.0 (permission granted upstream), the
  source of the SWF machinery in `src/swfhelper/`
- **Vermunds** — Extended Hotkey System, the inspiration
- Inspiration for the HUD approach: [moreHUDSE](https://github.com/ahzaab/moreHUDSE),
  [soulsy](https://github.com/ceejbot/soulsy), [skymp](https://github.com/skyrim-multiplayer/skymp)

---

## 9. Legal position on art

The repository contains **exactly one** `.swf`: `dist/Interface/MMOHotbar/Hotbar.swf`, our
own build from our own slot art, embedding no third-party bitmap, font or SWF. No keycap art
is tracked or shipped — see §7 and [`flash/README.md`](flash/README.md) for how to build a
keycap set for your own UI from a UI mod you have installed.
