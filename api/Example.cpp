// STB Hotkey System -- API usage, both directions.
//
// Illustrative, not a compilable project: drop the pieces into your own plugin. The one
// real consumer is STB Quick Hotkey Cast, whose whole main.cpp is roughly this file.

#include <Windows.h>

#include "STB_HotkeySystemAPI.h"

namespace API = STB::HotkeySystem;

namespace
{
	const API::IVersion1* g_hotkeys = nullptr;

	// --- 1. Getting the interface ---------------------------------------------------------
	// From kPostLoad or later: every plugin's DLL is loaded by then. Null just means the mod
	// is not installed -- carry on without it rather than failing to load.
	void FetchAPI()
	{
		auto* mod = GetModuleHandleA(API::kModuleName);
		if (!mod) {
			logger::info("{} not present", API::kModuleName);
			return;
		}
		auto request = reinterpret_cast<API::RequestAPI_t>(
			GetProcAddress(mod, API::kRequestFunction));
		if (!request) {
			logger::warn("{} predates the API", API::kModuleName);
			return;
		}
		g_hotkeys = static_cast<const API::IVersion1*>(request(1));
	}

	// --- 2. Key -> what it fires ----------------------------------------------------------
	// Ask while handling the key-down: the answer depends on which keys are held right now,
	// because a binding can be a two-key chord.
	//
	// This is the shape STB Quick Hotkey Cast uses inside ShoutHandler::CanProcess. EquipNow
	// must succeed before the press is claimed -- handing the key to the vanilla handler with
	// the old shout still in the voice slot would charge and fire the wrong one.
	bool OnKeyDown(RE::INPUT_DEVICE a_device, std::uint32_t a_key)
	{
		if (!g_hotkeys) {
			return false;
		}

		API::Binding bound[8]{};
		const auto   total = g_hotkeys->Resolve(static_cast<API::Device>(a_device), a_key,
              bound, static_cast<std::uint32_t>(std::size(bound)));
		// Resolve reports the true count, which can exceed the buffer for a large group.
		const auto count = (std::min)(total, static_cast<std::uint32_t>(std::size(bound)));

		for (std::uint32_t i = 0; i < count; ++i) {
			auto* form = RE::TESForm::LookupByID(bound[i].form);
			if (!form || !form->Is(RE::FormType::Shout)) {
				continue;
			}
			// Equips synchronously and takes the press off the mod's hands, so it will not
			// equip the same entry again a moment later from its own input sink.
			return g_hotkeys->EquipNow(bound[i]);
		}
		return false;
	}

	// --- 3. Entry -> which key it is on ---------------------------------------------------
	// For labelling a row or a HUD widget. keyCount 0 means unbound, so this is also how you
	// ask whether something has a hotkey at all.
	void LabelInventoryRow(RE::InventoryEntryData* a_entry)
	{
		if (!g_hotkeys || !a_entry || !a_entry->object) {
			return;
		}

		// An enchanted or tempered copy is a different binding from a plain one, so fill the
		// instance in from the row's extra data and use the exact lookup. Leave ench and
		// health at 0 for a plain copy.
		API::Binding id{};
		id.form = a_entry->object->GetFormID();
		if (a_entry->extraLists) {
			for (auto* xl : *a_entry->extraLists) {
				if (!xl) {
					continue;
				}
				if (auto* e = xl->GetByType<RE::ExtraEnchantment>(); e && e->enchantment) {
					id.ench = e->enchantment->GetFormID();
				}
				if (auto* h = xl->GetByType<RE::ExtraHealth>()) {
					id.health = static_cast<std::int32_t>(std::lround(h->health * 100.0f));
				}
			}
		}

		const auto chord = g_hotkeys->GetHotkeyExact(id);
		if (chord.keyCount == 0) {
			return;  // not bound
		}

		// chord.keys holds device-native codes, sorted ascending: DirectInput scancodes for
		// the keyboard, button indices for the mouse, key masks for the gamepad.
		for (std::uint32_t i = 0; i < chord.keyCount; ++i) {
			Draw(chord.device, chord.keys[i]);
		}

		// By base form instead, when any instance will do:
		//     const auto any = g_hotkeys->GetHotkey(a_entry->object->GetFormID());
	}
}
