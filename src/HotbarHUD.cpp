// This file is part of MMOHotbar, a full fork of STB Hotkey System.
// SPDX-License-Identifier: GPL-3.0-only
#include "HotbarHUD.h"

#include "EquipDispatch.h"
#include "HotkeyManager.h"
#include "KeyConflict.h"

#include <SimpleIni.hpp>

#include <format>

namespace MMO
{
	namespace
	{
		int DisplayRank(std::uint32_t a_key)
		{
			switch (a_key) {
			case 0x1D:
			case 0x9D:
				return 0;  // Ctrl
			case 0x38:
			case 0xB8:
				return 1;  // Alt
			case 0x2A:
			case 0x36:
				return 2;  // Shift
			default:
				return 3;
			}
		}

		std::string DeviceKeyName(const HKS::Bind& a_bind, std::uint32_t a_key)
		{
			switch (a_bind.device) {
			case RE::INPUT_DEVICE::kMouse:
				return std::format("Mouse {}", a_key + 1);
			case RE::INPUT_DEVICE::kGamepad:
				return std::format("Pad {}", a_key + 1);
			default:
				return HKS::KeyConflict::KeyName(a_key);
			}
		}

		std::string BindLabel(const HKS::Bind& a_bind)
		{
			if (!a_bind.IsValid()) {
				return {};
			}
			auto keys = a_bind.keys;
			std::sort(keys.begin(), keys.end(), [](std::uint32_t a, std::uint32_t b) {
				const int ra = DisplayRank(a);
				const int rb = DisplayRank(b);
				return ra != rb ? ra < rb : a < b;
			});

			std::string label;
			for (const auto key : keys) {
				if (!label.empty()) {
					label += '+';
				}
				label += DeviceKeyName(a_bind, key);
			}
			return label;
		}

		std::string ItemName(const HKS::ItemId& a_id)
		{
			if (auto* form = RE::TESForm::LookupByID(a_id.form)) {
				if (const char* name = form->GetName(); name && *name) {
					return name;
				}
			}
			return "???";
		}
	}

	void HotbarHUD::Register()
	{
		CSimpleIniA ini;
		ini.SetUnicode();
		ini.LoadFile(L"Data/SKSE/Plugins/STB_HotkeySystem.ini");
		ini.LoadFile(L"Data/SKSE/Plugins/MMOHotbar.ini");
		_presetModifier = static_cast<std::uint32_t>(
			ini.GetLongValue("Hotbar", "iPresetModifierScanCode", static_cast<long>(_presetModifier)));
		logger::info("MMO hotbar: {} slots, {} preset banks, toggle 0x{:X}",
			kSlotCount, kBankCount, _presetModifier);
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
		logger::info("hotbar preset bank -> {} (toggle 0x{:X})", _activePreset, _presetModifier);
	}

	std::uint8_t HotbarHUD::ActiveBank()
	{
		return static_cast<std::uint8_t>(_activePreset - 1);
	}

	HKS::Bind HotbarHUD::BindOfSlot(std::uint32_t a_slot)
	{
		if (a_slot >= kSlotCount) {
			return {};
		}
		const auto table = HKS::HotkeyManager::GetSingleton()->Snapshot(ActiveBank());
		return a_slot < table.size() ? table[a_slot].bind : HKS::Bind{};
	}

	int HotbarHUD::SlotOfBind(const HKS::Bind& a_bind)
	{
		const auto table = HKS::HotkeyManager::GetSingleton()->Snapshot(ActiveBank());
		for (std::size_t i = 0; i < table.size(); ++i) {
			if (table[i].bind == a_bind) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	int HotbarHUD::SlotOfItem(const HKS::ItemId& a_id)
	{
		const auto table = HKS::HotkeyManager::GetSingleton()->Snapshot(ActiveBank());
		for (std::size_t i = 0; i < table.size(); ++i) {
			if (table[i].Has(a_id)) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	std::string HotbarHUD::SlotLabel(std::uint32_t a_slot)
	{
		if (a_slot >= kSlotCount) {
			return "?";
		}
		const auto bind = BindOfSlot(a_slot);
		return bind.IsValid() ? BindLabel(bind) : std::to_string(a_slot + 1);
	}

	std::array<HotbarHUD::SlotView, HotbarHUD::kSlotCount> HotbarHUD::Snapshot()
	{
		std::array<SlotView, kSlotCount> out;
		const auto table = HKS::HotkeyManager::GetSingleton()->Snapshot(ActiveBank());
		for (std::uint32_t slot = 0; slot < kSlotCount; ++slot) {
			auto& view = out[slot];
			view.slot = slot;
			view.label = std::to_string(slot + 1);
			if (slot < table.size()) {
				const auto& hotkey = table[slot];
				view.bound = true;
				view.isGroup = hotkey.IsGroup();
				view.bind = hotkey.bind;
				view.items = hotkey.items;
				view.label = BindLabel(hotkey.bind);
			}

			if (!view.items.empty()) {
				view.names = ItemName(view.items.front());
				if (view.items.size() > 1) {
					view.names += std::format(" (+{})", view.items.size() - 1);
				}
			} else {
				view.names = "(empty)";
			}
		}
		return out;
	}

	bool HotbarHUD::ExecuteSlot(std::uint32_t a_slot)
	{
		if (a_slot >= kSlotCount) {
			return false;
		}
		const auto table = HKS::HotkeyManager::GetSingleton()->Snapshot(ActiveBank());
		if (a_slot >= table.size()) {
			return false;
		}

		auto items = table[a_slot].items;
		std::erase_if(items, [](const HKS::ItemId& a_id) { return HKS::EquipDispatch::IsClaimed(a_id); });
		if (items.empty()) {
			return false;
		}
		HKS::EquipDispatch::Fire(std::move(items));
		return true;
	}

	std::vector<std::uint32_t> HotbarHUD::SlotKeycapFrames(std::uint32_t a_slot)
	{
		const auto bind = BindOfSlot(a_slot);
		if (!bind.IsValid()) {
			return {};
		}
		const auto offset = bind.device == RE::INPUT_DEVICE::kMouse ? 256U :
			bind.device == RE::INPUT_DEVICE::kGamepad ? 266U : 0U;
		std::vector<std::uint32_t> frames;
		frames.reserve(bind.keys.size());
		for (const auto key : bind.keys) {
			frames.push_back(key + offset);
		}
		return frames;
	}
}
