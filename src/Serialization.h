#pragma once

namespace HKS::Serialization
{
	// MMOHotbar fork: keep the upstream co-save owner/record so existing STB saves
	// carry their binds over. The hotbar is a view, not extra save data.
	inline constexpr std::uint32_t kUniqueID = 'HKSY';  // co-save owner id
	inline constexpr std::uint32_t kRecordHotkeys = 'HOTK';
	// v3: one item per chord, written as (device, form, ench, uid, health, keys).
	// v4: a chord holds a LIST of items (groups), written as (device, keys, items).
	// v5: each item carries the hand(s) it was assigned in.
	// Older records are still read, so existing saves keep their hotkeys; what they do not
	// carry simply defaults (no remembered hand, one item per chord).
	inline constexpr std::uint32_t kVersion = 5;
	inline constexpr std::uint32_t kVersionNoHands = 4;
	inline constexpr std::uint32_t kVersionSingleItem = 3;

	void Register();  // call once from SKSEPlugin_Load

	void SaveCallback(SKSE::SerializationInterface* a_intfc);
	void LoadCallback(SKSE::SerializationInterface* a_intfc);
	void RevertCallback(SKSE::SerializationInterface* a_intfc);
}
