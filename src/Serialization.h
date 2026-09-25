#pragma once

namespace HKS::Serialization
{
	// MMOHotbar fork: keep the upstream co-save owner/record so existing STB saves
	// carry their binds over. The active preset remains a separate small record.
	inline constexpr std::uint32_t kUniqueID = 'HKSY';  // co-save owner id
	inline constexpr std::uint32_t kRecordHotkeys = 'HOTK';
	inline constexpr std::uint32_t kRecordPreset = 'PRST';
	inline constexpr std::uint32_t kPresetVersion = 1;

	// v3: one item per chord, written as (device, form, ench, uid, health, keys).
	// v4: a chord holds a list of items, written as (device, keys, items).
	// v5: each item carries the hand(s) it was assigned in.
	// v6: each chord ends with its preset-bank byte (0 or 1).
	// Older records remain readable and load as preset 1, preserving existing binds.
	inline constexpr std::uint32_t kVersion = 6;
	inline constexpr std::uint32_t kVersionNoBanks = 5;
	inline constexpr std::uint32_t kVersionNoHands = 4;
	inline constexpr std::uint32_t kVersionSingleItem = 3;

	void Register();  // call once from SKSEPlugin_Load

	void SaveCallback(SKSE::SerializationInterface* a_intfc);
	void LoadCallback(SKSE::SerializationInterface* a_intfc);
	void RevertCallback(SKSE::SerializationInterface* a_intfc);
}
