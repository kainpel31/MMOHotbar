// MMOHotbar — 12-slot MMO hotbar with two preset banks.
//
// This file is part of MMOHotbar, a full fork of STB Hotkey System.
// SPDX-License-Identifier: GPL-3.0-only
// Upstream: https://github.com/STB-Team/STB-Hotkey-System
#pragma once

#include "Hotkey.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace MMO
{
// The hotbar is a VIEW over the STB chord table -- its only own save data is the
// active preset number.
//
// The player has TWO banks of bindings (preset 1 and preset 2). A bank holds its
// own chords, on whatever keys the player picked (F, Z, F1, Home, Alt+G, ...).
// The 12 slots are just the bank's bindings in the order they were assigned, so
// the layout is the player's own and never has to be configured twice.
//
// The preset key (default X) ONLY flips which bank is live: it is never part of a
// chord and never has to be held. Firing a slot reuses HKS::EquipDispatch, exactly
// as pressing the bound chord would.
class HotbarHUD
{
public:
    static constexpr std::size_t kSlotCount = 12;
    static constexpr std::size_t kBankCount = HKS::kBankCount;

    struct SlotView
    {
        std::uint32_t     slot = 0;         // 0..11
        bool              bound = false;    // the live bank has a binding here
        bool              isGroup = false;  // chord holds >1 item
        HKS::Bind         bind;             // the chord backing this slot
        std::vector<HKS::ItemId> items;     // copies, safe off-lock
        std::string       label;            // keycap text, e.g. "F" or "Alt+G"
        std::string       names;            // first item name(s) for HUD text
    };

    static void Register();

    // Preset modifier scancode (default 45 = X). Read live from INI [Hotbar].
    // It is ONLY a toggle: never part of a chord, never held to fire a slot.
    static std::uint32_t PresetModifier();
    static void          SetPresetModifier(std::uint32_t a_code);

    // Which bank is live: 1 or 2. Persisted in the co-save (record PRST).
    static std::uint32_t ActivePreset();
    static void          SetActivePreset(std::uint32_t a_preset);
    static void          TogglePreset();

    // 0-based bank index of the active preset (0 = preset 1).
    static std::uint8_t ActiveBank();

    // The chord backing slot a_slot of the ACTIVE bank. Invalid (no keys) when the
    // slot has nothing bound, or when a_slot is out of range.
    static HKS::Bind BindOfSlot(std::uint32_t a_slot);

    // Index of a_bind / a_id inside the ACTIVE bank, or -1 when it is not there.
    static int SlotOfBind(const HKS::Bind& a_bind);
    static int SlotOfItem(const HKS::ItemId& a_id);

    // The 12 slots of the active bank, in assignment order.
    static std::array<SlotView, kSlotCount> Snapshot();

    // Frame LABEL for an item's kind in the icon sheet the HUD loads from the player's
    // own UI files (see HotbarHUDView): "weapon_greatsword", "armor_head", ... Empty
    // when the form is not a weapon or an armour piece -- the categories the sheet
    // listing covers so far. A slot shows the frame for its FIRST item, so a group
    // reads as the kind of the thing you press first.
    //
    // This is a mapping on the form's TYPE, not on the item: every iron sword is
    // "weapon_sword". That is deliberate and it is what makes the icons possible at
    // all -- the per-form icon index lives in the game's ItemMenu, which CommonLibSSE-NG
    // does not expose -- and it is also how the sheet itself is organised.
    [[nodiscard]] static std::string ItemIconLabel(const HKS::ItemId& a_id);

    // Equip whatever slot a_slot holds (toggle/group semantics from STB).
    // Returns false when the slot is empty.
    static bool ExecuteSlot(std::uint32_t a_slot);

    // Keycap text for a slot: the real bound chord ("F", "Alt+G"). An unbound slot
    // reads as its position ("1".."12") so an empty hotbar still shows its shape.
    static std::string SlotLabel(std::uint32_t a_slot);

    // Keycap frames for a slot, in the Interface/STB_Keycaps.swf contract
    // (keyboard = scancode, mouse = 256 + button, gamepad = 266 + button), so a HUD
    // can drive the SWF with the keys the player actually bound.
    static std::vector<std::uint32_t> SlotKeycapFrames(std::uint32_t a_slot);

    // How many of the 12 slots the bar shows and accepts ([Hotbar] iVisibleSlots,
    // clamped to 1..12). The SWF is always built with 12 slots -- they are hidden, not
    // rebuilt -- so shrinking the bar is free and needs no re-run of the SWF builder.
    // Binds that already sit in a hidden slot keep working on their own chord; they are
    // simply not on the bar.
    static std::size_t VisibleSlotCount();

    // Where and how big the bar is, from [Hotbar]. Offsets are in stage pixels and are
    // read against the VISIBLE stage, so the same numbers hold at 16:9 and ultrawide:
    //   fBarX     horizontal offset from the stage's middle. 0 = centred, -200 = 200px
    //             left. Prefixed because [Icons] fX means something else entirely.
    //   fBarY     height of the bar's BOTTOM edge above the bottom of the stage. 32 is
    //             the vanilla-ish default the bar shipped with.
    //   fBarScale size as a percentage (100 = as drawn, clamped to 25..400). The bar is
    //             anchored by its drawn bottom-centre, so scaling does not walk it across
    //             the screen.
    static float BarScalePercent();
    static float BarOffsetX();
    static float BarOffsetY();

    // [Hotbar] bShowItemIcons -- fill the slots from the player's icon sheet. OFF by
    // default and not out of caution for the art: it is twelve synchronous loadMovie()
    // calls into a large external SWF, performed on the HUD frame, and that cost has never
    // been measured in game. Switch it on once you know it is cheap on your setup.
    static bool ItemIconsEnabled();

private:
    static inline std::uint32_t _presetModifier = 45;  // X
    static inline std::uint32_t _activePreset = 1;     // live bank, 1 or 2
    static inline std::size_t   _visibleSlots = kSlotCount;
    static inline float         _barScalePercent = 100.0f;
    static inline float         _barOffsetX = 0.0f;
    static inline float         _barOffsetY = 32.0f;
    static inline bool          _itemIcons = false;
		static inline bool          _hudEnabled = true;

		// [Hotbar] bEnableHUD -- the 12-slot HUD renderer. Set to 0 to leave HUDMenu
		// untouched, which is the first thing to try if the game misbehaves: it is the
		// only part of this plugin that draws inside the game's own movie.
		static bool HudEnabled() { return _hudEnabled; }
};
}  // namespace MMO
