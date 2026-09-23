// This file is part of MMOHotbar, a full fork of STB Hotkey System.
// SPDX-License-Identifier: GPL-3.0-only
#include "HotbarHUD.h"

#include "EquipDispatch.h"
#include "HotkeyManager.h"

#include <SimpleIni.hpp>

#include <algorithm>
#include <format>

namespace MMO
{
namespace
{
// Keyboard scancodes for number/row keys "1".."=" (DX set 1: 2..13).
constexpr std::uint32_t kRowKeys[HotbarHUD::kBaseSlots] = { 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 };

std::string KeyName(std::uint32_t a_scancode)
{
    switch (a_scancode) {
    case 2: return "1"; case 3: return "2"; case 4: return "3"; case 5: return "4";
    case 6: return "5"; case 7: return "6"; case 8: return "7"; case 9: return "8";
    case 10: return "9"; case 11: return "0"; case 12: return "-"; case 13: return "=";
    case 45: return "X";
    default: return std::format("K{}", a_scancode);
    }
}

std::string ItemName(const HKS::ItemId& a_id)
{
    if (auto* form = RE::TESForm::LookupByID(a_id.form)) {
        if (const char* n = form->GetName(); n && *n) {
            return n;
        }
    }
    return "???";
}
}  // namespace

void HotbarHUD::Register()
{
    // Preset modifier lives in the shared INI so users keep one config file. Section
    // [Hotbar], iPresetModifierScanCode (default 45 = X). Same layering as Settings::Load:
    // the legacy STB name is the base and MMOHotbar.ini is read on top of it.
    CSimpleIniA ini;
    ini.SetUnicode();
    ini.LoadFile(L"Data/SKSE/Plugins/STB_HotkeySystem.ini");
    ini.LoadFile(L"Data/SKSE/Plugins/MMOHotbar.ini");
    _presetModifier = static_cast<std::uint32_t>(
        ini.GetLongValue("Hotbar", "iPresetModifierScanCode", static_cast<long>(_presetModifier)));
    logger::info("MMO hotbar: 24 slots, preset modifier 0x{:X}", _presetModifier);
}

std::uint32_t HotbarHUD::PresetModifier() { return _presetModifier; }
void HotbarHUD::SetPresetModifier(std::uint32_t a_code) { _presetModifier = a_code; }

std::uint32_t HotbarHUD::ActivePreset() { return _activePreset; }

void HotbarHUD::SetActivePreset(std::uint32_t a_preset)
{
    _activePreset = a_preset == 2 ? 2 : 1;
}

void HotbarHUD::TogglePreset()
{
    _activePreset = _activePreset == 1 ? 2 : 1;
    logger::info("hotbar preset plane -> {} (modifier 0x{:X})", _activePreset, _presetModifier);
}

bool HotbarHUD::IsRowKey(std::uint32_t a_scancode)
{
    return std::find(std::begin(kRowKeys), std::end(kRowKeys), a_scancode) != std::end(kRowKeys);
}

void HotbarHUD::TranslateForCapture(HKS::Bind& a_bind)
{
    if (a_bind.device != RE::INPUT_DEVICE::kKeyboard || _activePreset != 2 || _presetModifier == 0) {
        return;
    }
    if (a_bind.keys.size() != 1 || !IsRowKey(a_bind.keys[0])) {
        return;
    }
    a_bind.keys.push_back(_presetModifier);
    a_bind.Canonicalize();
}

void HotbarHUD::TranslateForFire(RE::INPUT_DEVICE a_device,
    std::unordered_set<std::uint32_t>& a_held, std::uint32_t a_key)
{
    // Only the plane-2 read-through needs translating; plane 1 resolves exactly as
    // stored. Chords on keys outside the hotbar row are never rewritten, so a
    // modifier the player reused for an unrelated chord keeps its own meaning.
    if (a_device != RE::INPUT_DEVICE::kKeyboard || _activePreset != 2 || _presetModifier == 0) {
        return;
    }
    if (std::find(std::begin(kRowKeys), std::end(kRowKeys), a_key) == std::end(kRowKeys)) {
        return;
    }
    if (a_held.contains(_presetModifier)) {
        a_held.erase(_presetModifier);   // modifier+key on plane 2 -> plane-1 slot
    } else {
        a_held.insert(_presetModifier);  // bare key on plane 2 -> plane-2 slot
    }
}

HKS::Bind HotbarHUD::BindOfSlot(std::uint32_t a_slot)
{
    HKS::Bind b;
    b.device = RE::INPUT_DEVICE::kKeyboard;
    if (a_slot >= kSlotCount) {
        return b;
    }
    b.keys.push_back(kRowKeys[a_slot % kBaseSlots]);
    if (a_slot >= kBaseSlots) {
        b.keys.push_back(_presetModifier);
    }
    b.Canonicalize();
    return b;
}

int HotbarHUD::SlotOfBind(const HKS::Bind& a_bind)
{
    if (a_bind.device != RE::INPUT_DEVICE::kKeyboard) {
        return -1;
    }
    if (a_bind.keys.size() == 1) {
        auto it = std::find(std::begin(kRowKeys), std::end(kRowKeys), a_bind.keys[0]);
        if (it == std::end(kRowKeys)) {
            return -1;
        }
        return static_cast<int>(std::distance(std::begin(kRowKeys), it));
    }
    if (a_bind.keys.size() == 2) {
        std::uint32_t other = 0;
        bool hasMod = false;
        for (auto k : a_bind.keys) {
            if (k == _presetModifier) {
                hasMod = true;
            } else {
                other = k;
            }
        }
        if (!hasMod) {
            return -1;
        }
        auto it = std::find(std::begin(kRowKeys), std::end(kRowKeys), other);
        if (it == std::end(kRowKeys)) {
            return -1;
        }
        return static_cast<int>(kBaseSlots + std::distance(std::begin(kRowKeys), it));
    }
    return -1;
}

std::string HotbarHUD::SlotLabel(std::uint32_t a_slot)
{
    if (a_slot >= kSlotCount) {
        return "?";
    }
    std::string base = KeyName(kRowKeys[a_slot % kBaseSlots]);
    if (a_slot >= kBaseSlots) {
        return KeyName(_presetModifier) + "+" + base;
    }
    return base;
}

std::array<HotbarHUD::SlotView, HotbarHUD::kSlotCount> HotbarHUD::Snapshot()
{
    std::array<SlotView, kSlotCount> out;
    auto* mgr = HKS::HotkeyManager::GetSingleton();
    auto table = mgr->Snapshot();
    for (std::uint32_t s = 0; s < kSlotCount; ++s) {
        SlotView v;
        v.slot = s;
        v.bind = BindOfSlot(s);
        v.label = SlotLabel(s);
        for (const auto& h : table) {
            if (h.bind == v.bind) {
                v.bound = true;
                v.isGroup = h.IsGroup();
                v.items = h.items;
                break;
            }
        }
        if (v.bound && !v.items.empty()) {
            v.names = ItemName(v.items.front());
            if (v.items.size() > 1) {
                v.names += std::format(" (+{})", v.items.size() - 1);
            }
        } else {
            v.names = "(empty)";
        }
        out[s] = std::move(v);
    }
    return out;
}

bool HotbarHUD::ExecuteSlot(std::uint32_t a_slot, HKS::EquipDispatch::FireMode a_mode)
{
    if (a_slot >= kSlotCount) {
        return false;
    }
    auto want = BindOfSlot(a_slot);
    auto* mgr = HKS::HotkeyManager::GetSingleton();
    auto table = mgr->Snapshot();
    for (const auto& h : table) {
        if (h.bind == want && !h.items.empty()) {
            // Claim-check like the keyboard path so a double-fire can't happen.
            auto items = h.items;
            std::erase_if(items, [](const HKS::ItemId& a_id) { return HKS::EquipDispatch::IsClaimed(a_id); });
            if (items.empty()) {
                return false;
            }
            HKS::EquipDispatch::Fire(std::move(items), a_mode);
            return true;
        }
    }
    return false;
}
}  // namespace MMO
