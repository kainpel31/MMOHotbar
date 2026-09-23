// MMOHotbar — 24-slot MMO view over the STB chord table.
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
#include <unordered_set>
#include <vector>

namespace MMO
{
// A 24-slot MMO hotbar is a VIEW over the STB chord table — no extra save data.
// Slots 0-11  = keyboard single keys 1..12  (scancodes 2..13).
// Slots 12-23 = keyboard chord {presetModifier, 2..13} (default 45 = X).
// Bind one of those chords through the normal STB assign flow and it shows up
// on the hotbar automatically; firing a slot reuses HKS::EquipDispatch.
class HotbarHUD
{
public:
    static constexpr std::size_t kSlotCount = 24;
    static constexpr std::size_t kBaseSlots = 12;

    struct SlotView
    {
        std::uint32_t     slot = 0;         // 0..23
        bool              bound = false;    // a chord in the table matches this slot
        bool              isGroup = false;  // chord holds >1 item
        HKS::Bind         bind;             // the chord backing this slot
        std::vector<HKS::ItemId> items;     // copies, safe off-lock
        std::string       label;            // display label, e.g. "1" or "X+1"
        std::string       names;            // first item name(s) for HUD text
    };

    static void Register();

    // Preset modifier scancode (default 45 = X). Read live from INI [Hotbar].
    static std::uint32_t PresetModifier();
    static void          SetPresetModifier(std::uint32_t a_code);

    // The two preset planes. Slots 0-11 are the bare row keys of the ACTIVE plane;
    // slots 12-23 are the same keys backed by the {presetModifier, key} chord. The
    // plane is a toggle: one press of the modifier flips between them (no hold), so
    // both planes play from the same 12 physical keys. Persisted in the co-save.
    // Stored chords never change -- only which ones a bare key resolves to does.
    static std::uint32_t ActivePreset();                 // 1 or 2
    static void          SetActivePreset(std::uint32_t a_preset);
    static void          TogglePreset();

    // True when a_scancode is one of the 12 physical row keys (1..=).
    static bool IsRowKey(std::uint32_t a_scancode);

    // Firing translation for the toggle. With plane 2 active a bare row key must
    // resolve to its {modifier, key} chord and a modifier+key press to the bare
    // chord (the modifier inverts the plane). Mutates a_held in place before the
    // store is asked to resolve. Non-row keys and other devices are left alone.
    static void TranslateForFire(RE::INPUT_DEVICE a_device,
        std::unordered_set<std::uint32_t>& a_held, std::uint32_t a_key);

    // Capture translation for binding. With plane 2 active, a bare row key bound
    // under Ctrl is stored as the {presetModifier, key} chord, so Ctrl+1 lands on
    // the preset you are looking at. Anything else (a chord that already names
    // the modifier, non-row keys, other devices) is left alone.
    static void TranslateForCapture(HKS::Bind& a_bind);

    // The chord backing slot a_slot (always valid, even when unbound).
    static HKS::Bind BindOfSlot(std::uint32_t a_slot);
    // Which slot a_bind sits on, or -1 when it is not a hotbar chord.
    static int SlotOfBind(const HKS::Bind& a_bind);

    // Snapshot of all 24 slots for a HUD/SWF/API consumer.
    static std::array<SlotView, kSlotCount> Snapshot();

    // Equip whatever slot a_slot holds (toggle/group semantics from STB).
    // a_mode = kSwapHands moves a one-handed item to the other hand (RMB+hotkey).
    // Returns false when the slot is empty.
    static bool ExecuteSlot(std::uint32_t a_slot,
        HKS::EquipDispatch::FireMode a_mode = HKS::EquipDispatch::FireMode::kNormal);

    // Human label for a slot, e.g. "5" or "X+5".
    static std::string SlotLabel(std::uint32_t a_slot);

private:
    static inline std::uint32_t _presetModifier = 45;  // X
    static inline std::uint32_t _activePreset = 1;     // toggled plane, 1 or 2
};
}  // namespace MMO
