// MMOHotbar — 24-slot MMO view over the STB chord table.
//
// This file is part of MMOHotbar, a full fork of STB Hotkey System.
// SPDX-License-Identifier: GPL-3.0-only
// Upstream: https://github.com/STB-Team/STB-Hotkey-System
#pragma once

#include "Hotkey.h"

#include <array>
#include <cstdint>
#include <string>
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

    // The chord backing slot a_slot (always valid, even when unbound).
    static HKS::Bind BindOfSlot(std::uint32_t a_slot);
    // Which slot a_bind sits on, or -1 when it is not a hotbar chord.
    static int SlotOfBind(const HKS::Bind& a_bind);

    // Snapshot of all 24 slots for a HUD/SWF/API consumer.
    static std::array<SlotView, kSlotCount> Snapshot();

    // Equip whatever slot a_slot holds (toggle/group semantics from STB).
    // Returns false when the slot is empty.
    static bool ExecuteSlot(std::uint32_t a_slot);

    // Human label for a slot, e.g. "5" or "X+5".
    static std::string SlotLabel(std::uint32_t a_slot);

private:
    static inline std::uint32_t _presetModifier = 45;  // X
};
}  // namespace MMO
