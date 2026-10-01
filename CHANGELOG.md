# Changelog

Player-facing changes. Build and infrastructure notes live in the commit history.

## How this file works

* One section per release. The newest at the top.
* Everything under **Unreleased** is on `main` but **not yet verified in game**.
* **Large changes go on their own branch**, cut from `main` only while `main`'s latest
  commit is green in CI — so there is always a known-good build to fall back to.

---

## Unreleased

Builds clean. **Not yet tested in game**, and there is one open bug below.

### Known issues

* **The game freezes when starting a new game or loading a save** (menu → New Game /
  Load Clean Game). Reported on two machines. The last log we have stops before the freeze,
  so the cause is not yet pinned down. Workaround to try: set `[Hotbar] bEnableHUD = 0`.
* The FOMOD installer has been reported as unreliable; what exactly goes wrong is not
  established yet.

### Fixed

* The HUD bar no longer risks a use-after-free when the game rebuilds the HUD menu on a
  save load or new game. The clip is now released only while its movie is alive, and
  rebuild detection no longer depends on the new movie having a different address.
* A slot that loses its binding now shows the empty slot number again, instead of leaving
  the previous chord's keycap on screen.
* The hotbar attach no longer repeats on every frame while the HUD movie is still coming
  up; it backs off and says so if it keeps failing.
* Co-save reads are bounded, so a corrupt or truncated save can no longer make the game
  allocate an absurd amount of memory and die on load.
* `EquipNow` in the plugin API refuses calls from any thread but the game's, instead of
  touching the actor's equipment state from elsewhere.

### Added

* `[Hotbar] iVisibleSlots` — how many of the 12 slots the bar shows and accepts (1–12).
  The rest are hidden, not removed.
* `[Hotbar] fBarScale`, `fBarX`, `fBarY` — size, horizontal position and height of the
  bar. The bar grows around its bottom-centre anchor, so the same numbers hold at any
  aspect ratio.
* `[Icons] iKeycapSource` — which keycap art the list rows draw: `0` for
  `STB_Keycaps.swf`, `1` for SkyUI's own `buttonart.swf`. Both use the same frame layout,
  so it only picks the file. Option `1` is the one a stock SkyUI install can use, since
  the STB file is not shipped.
* `[Hotbar] bShowItemIcons` — fill the slots from the player's own icon sheet. **Off by
  default**: it loads a large external SWF on the HUD frame and that cost was never
  measured.
* `[Hotbar] bEnableHUD` — turn the bar off entirely, leaving the game's HUD menu alone.
* The preset toggle key is reported **by name** in the log at start-up, and a collision
  with the assign or group modifier is warned about instead of silently killing the
  toggle.
* MMOHotbar now refuses to load when the original STB Hotkey System is also installed,
  instead of fighting it over the shared co-save id.

### Icons on the bar

Slots can show a picture for the item they hold, taken from an icon sheet already on the
player's disk — nothing is bundled or redistributed. The picture is chosen by the item's
**kind**, not by the item: every iron sword shows the same sword icon. That is because the
per-item icon index lives in the game's own `ItemMenu`, which CommonLibSSE-NG does not
expose. Weapons, shields, armour, jewellery, scrolls, ingredients, arrows, tomes, notes,
shouts, spells, potions, soul gems and house keys are mapped; spells and potions share one
frame each for the same reason. With no icon sheet installed, the bar simply has no
pictures.