// This file is part of MMOHotbar, a full fork of STB Hotkey System.
// SPDX-License-Identifier: GPL-3.0-only
#include "HotbarHUD.h"

#include "EquipDispatch.h"
#include "HotkeyManager.h"
#include "KeyConflict.h"

#include <SimpleIni.hpp>

#include <algorithm>
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

		// Everything that is not a weapon or a piece of armour, keyed on the form class
		// the sheet also has frames for. Returns "" for a form it has no honest frame
		// for, which the bar shows as the sheet's blank frame -- a wrong picture is worse
		// than no picture on a key you press in a fight.
		std::string MiscIconLabel(const RE::TESForm* a_form)
		{
			if (a_form->As<RE::ScrollItem>()) {
				return "default_scroll";
			}
			if (a_form->As<RE::IngredientItem>()) {
				return "default_ingredient";
			}
			if (a_form->As<RE::TESAmmo>()) {
				// Arrow and bolt share one animation type, so one frame has to stand for
				// both; the sheet's other bolt frame exists but is not reachable from the
				// form alone.
				return "weapon_arrow";
			}
			if (a_form->As<RE::TESShout>()) {
				return "default_shout";
			}
			if (const auto* book = a_form->As<RE::TESObjectBOOK>()) {
				// The form distinguishes a readable tome from a note scroll and nothing
				// finer, so journal and map share the note's frame.
				return book->IsBookTome() ? "book_tome" : "book_note";
			}
			// GetFormType() returns the enum by value, not a handle to it.
			switch (a_form->GetFormType()) {
			case RE::FormType::SoulGem:
				return "misc_soulgem";
			case RE::FormType::KeyMaster:
				return "key_house";
			default:
				break;
			}
			// Potions and spells land here. This CommonLibSSE-NG has no FormType for
			// potion and no spell-school accessor, so there is nothing honest to return
			// for them yet -- the sheet has default_potion, magic_fire and friends, but
			// picking one would be a guess.
			return {};
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

		// Layout, same file and same section. Clamped here rather than trusted: these are
		// read once per session, and an odd value would otherwise become a bar that is not
		// on screen, or a slot count nothing can fire.
		//
		// Named fBarScale/fBarX/fBarY rather than fScale/fX/fY on purpose: [Icons] already
		// uses those three names for the KEYCAPS drawn on inventory list rows, and two
		// unrelated numbers with the same key name in one INI is a trap to edit.
		const auto slots = ini.GetLongValue("Hotbar", "iVisibleSlots", static_cast<long>(_visibleSlots));
		_visibleSlots = std::clamp<std::size_t>(slots < 1 ? 1 : static_cast<std::size_t>(slots), kSlotCount);

		_barScalePercent = std::clamp(
			static_cast<float>(ini.GetDoubleValue("Hotbar", "fBarScale", _barScalePercent)), 25.0f, 400.0f);
		_barOffsetX = static_cast<float>(ini.GetDoubleValue("Hotbar", "fBarX", _barOffsetX));
		// Negative would put the bar's bottom edge under the screen edge, where half of it
		// is simply gone; allow an overhang, not a whole bar's worth.
		_barOffsetY = std::clamp(static_cast<float>(ini.GetDoubleValue("Hotbar", "fBarY", _barOffsetY)),
		                         0.0f, static_cast<float>(kSlotCount * 64));

		logger::info("MMO hotbar: {} of {} slots, {} preset banks, toggle 0x{:X}, "
		             "layout x {:+.0f} y {:.0f} scale {:.0f}%",
		             _visibleSlots, kSlotCount, kBankCount, _presetModifier,
		             static_cast<double>(_barOffsetX), static_cast<double>(_barOffsetY),
		             static_cast<double>(_barScalePercent));
	}

	std::uint32_t HotbarHUD::PresetModifier() { return _presetModifier; }
	void HotbarHUD::SetPresetModifier(std::uint32_t a_code) { _presetModifier = a_code; }

	std::size_t HotbarHUD::VisibleSlotCount() { return _visibleSlots; }
	float       HotbarHUD::BarScalePercent() { return _barScalePercent; }
	float       HotbarHUD::BarOffsetX() { return _barOffsetX; }
	float       HotbarHUD::BarOffsetY() { return _barOffsetY; }

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

	std::string HotbarHUD::ItemIconLabel(const HKS::ItemId& a_id)
	{
		const auto* form = RE::TESForm::LookupByID(a_id.form);
		if (!form) {
			return {};
		}

		// Weapons. The sheet's frames are named after Skyrim's own animation types, so
		// this lines up one for one. The types the sheet splits finer than the game does
		// -- mace and hammer share kOneHandMace, waraxe/pickaxe/woodaxe share
		// kOneHandAxe -- land on the frame that stands for the type.
		if (const auto* weapon = form->As<RE::TESObjectWEAP>()) {
			switch (weapon->GetWeaponType()) {
			case RE::WEAPON_TYPE::kOneHandSword:
				return "weapon_sword";
			case RE::WEAPON_TYPE::kTwoHandSword:
				return "weapon_greatsword";
			case RE::WEAPON_TYPE::kOneHandDagger:
				return "weapon_dagger";
			case RE::WEAPON_TYPE::kOneHandAxe:
				return "weapon_waraxe";
			case RE::WEAPON_TYPE::kTwoHandAxe:
				return "weapon_battleaxe";
			case RE::WEAPON_TYPE::kOneHandMace:
				return "weapon_mace";
			case RE::WEAPON_TYPE::kBow:
				return "weapon_bow";
			case RE::WEAPON_TYPE::kCrossbow:
				return "weapon_crossbow";
			case RE::WEAPON_TYPE::kStaff:
				return "weapon_staff";
			default:
				// Bare hands, and any type a later runtime adds: the sheet's own
				// "no weapon in particular" frame.
				return "default_weapon";
			}
		}

		// Armour. The sheet splits it by tier (lightarmor_/armor_/clothing_) and then by
		// body part, and the form already carries both: the biped slot mask and the
		// armour type. Shield first, because a shield's mask also sets kBody.
		const auto* armor = form->As<RE::BGSBipedObjectForm>();
		if (!armor) {
			return MiscIconLabel(form);
		}

		const char* tier = armor->IsLightArmor()  ? "lightarmor_" :
		                   armor->IsHeavyArmor() ? "armor_" :
		                   armor->IsClothing()   ? "clothing_" :
		                                           nullptr;
		if (!tier) {
			return {};
		}

		using Slot = RE::BGSBipedObjectForm::BipedObjectSlot;
		const char* part = nullptr;
		if (armor->HasPartOf(Slot::kShield)) {
			part = "shield";
		}
		else if (armor->HasPartOf(Slot::kAmulet)) {
			part = "amulet";
		}
		else if (armor->HasPartOf(Slot::kRing)) {
			part = "ring";
		}
		else if (armor->HasPartOf(Slot::kCirclet)) {
			part = "circlet";
		}
		else if (armor->HasPartOf(Slot::kHead)) {
			part = "head";
		}
		else if (armor->HasPartOf(Slot::kBody)) {
			part = "body";
		}
		else if (armor->HasPartOf(Slot::kHands)) {
			part = "hands";
		}
		else if (armor->HasPartOf(Slot::kForearms)) {
			part = "forearms";
		}
		else if (armor->HasPartOf(Slot::kFeet)) {
			part = "feet";
		}
		else if (armor->HasPartOf(Slot::kCalves)) {
			part = "calves";
		}
		if (!part) {
			// Hair, tails, jewellery the sheet has no frame for. Nothing, not a wrong
			// picture.
			return {};
		}

		return std::format("{}{}", tier, part);
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
		// A slot the bar does not show is a slot the player cannot see they are pressing;
		// refusing it keeps the visible bar and the usable bar the same thing.
		if (a_slot >= VisibleSlotCount()) {
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
