#include "Serialization.h"

#include "HotkeyManager.h"

namespace HKS::Serialization
{
	namespace
	{
		template <class T>
		bool Write(SKSE::SerializationInterface* a_intfc, const T& a_value)
		{
			return a_intfc->WriteRecordData(&a_value, sizeof(T));
		}

		template <class T>
		bool Read(SKSE::SerializationInterface* a_intfc, T& a_out)
		{
			return a_intfc->ReadRecordData(&a_out, sizeof(T)) == sizeof(T);
		}
	}

	void SaveCallback(SKSE::SerializationInterface* a_intfc)
	{
		auto* mgr = HotkeyManager::GetSingleton();
		const auto& hotkeys = mgr->GetAll();

		if (!a_intfc->OpenRecord(kRecordHotkeys, kVersion)) {
			logger::error("failed to open HOTK record");
			return;
		}

		Write<std::uint32_t>(a_intfc, static_cast<std::uint32_t>(hotkeys.size()));

		for (const auto& h : hotkeys) {
			Write<std::uint32_t>(a_intfc, static_cast<std::uint32_t>(h.bind.device));
			Write<std::uint32_t>(a_intfc, static_cast<std::uint32_t>(h.bind.keys.size()));
			for (auto k : h.bind.keys) {
				Write<std::uint32_t>(a_intfc, k);
			}
			Write<std::uint32_t>(a_intfc, static_cast<std::uint32_t>(h.items.size()));
			for (const auto& id : h.items) {
				Write<RE::FormID>(a_intfc, id.form);
				Write<RE::FormID>(a_intfc, id.ench);
				Write<std::uint16_t>(a_intfc, id.uid);
				Write<std::int32_t>(a_intfc, id.health);
				Write<std::uint8_t>(a_intfc, id.hands);
			}
		}

		logger::info("saved {} hotkeys", hotkeys.size());
	}

	namespace
	{
		// Resolve one saved item identity across the current load order. Returns false when
		// the source plugin is gone -- that item is dropped, never the whole record.
		bool ResolveItem(SKSE::SerializationInterface* a_intfc, const ItemId& a_saved, ItemId& a_out)
		{
			RE::FormID resolved = 0;
			if (!a_intfc->ResolveFormID(a_saved.form, resolved)) {
				logger::warn("could not resolve form {:08X}, dropping item", a_saved.form);
				return false;
			}
			if (!RE::TESForm::LookupByID(resolved)) {
				logger::warn("form {:08X} not found, dropping item", resolved);
				return false;
			}
			// The enchantment is a FormID too. uid is a per-save counter and health a
			// fixed-point value -- both kept verbatim.
			RE::FormID resolvedEnch = 0;
			if (a_saved.ench != 0) {
				a_intfc->ResolveFormID(a_saved.ench, resolvedEnch);
			}
			a_out = ItemId{ resolved, resolvedEnch, a_saved.uid, a_saved.health, a_saved.hands };
			return true;
		}

		bool ReadItem(SKSE::SerializationInterface* a_intfc, ItemId& a_out, bool a_withHands)
		{
			if (!Read(a_intfc, a_out.form) || !Read(a_intfc, a_out.ench) ||
				!Read(a_intfc, a_out.uid) || !Read(a_intfc, a_out.health)) {
				return false;
			}
			return !a_withHands || Read(a_intfc, a_out.hands);
		}

		bool ReadBind(SKSE::SerializationInterface* a_intfc, Bind& a_out)
		{
			std::uint32_t deviceRaw = 0;
			std::uint32_t nKeys = 0;
			if (!Read(a_intfc, deviceRaw) || !Read(a_intfc, nKeys)) {
				return false;
			}
			a_out.device = static_cast<RE::INPUT_DEVICE>(deviceRaw);
			a_out.keys.reserve(nKeys);
			for (std::uint32_t k = 0; k < nKeys; ++k) {
				std::uint32_t key = 0;
				if (!Read(a_intfc, key)) {
					return false;
				}
				a_out.keys.push_back(key);
			}
			a_out.Canonicalize();
			return true;
		}
	}

	void LoadCallback(SKSE::SerializationInterface* a_intfc)
	{
		std::vector<Hotkey> loaded;

		std::uint32_t type;
		std::uint32_t version;
		std::uint32_t length;

		while (a_intfc->GetNextRecordInfo(type, version, length)) {
			if (type != kRecordHotkeys) {
				logger::warn("unknown co-save record {:08X}, skipping", type);
				continue;
			}
			if (version != kVersion && version != kVersionNoHands && version != kVersionSingleItem) {
				logger::warn("HOTK version {} is not readable (expected {}, {} or {}), ignoring",
					version, kVersion, kVersionNoHands, kVersionSingleItem);
				continue;
			}
			const bool withHands = version >= kVersion;

			std::uint32_t count = 0;
			if (!Read(a_intfc, count)) {
				logger::error("failed reading hotkey count");
				break;
			}

			loaded.reserve(count);
			for (std::uint32_t i = 0; i < count; ++i) {
				Bind                bind;
				std::vector<ItemId> saved;

				if (version == kVersionSingleItem) {
					// v3 layout: device, one item, then the chord.
					std::uint32_t deviceRaw = 0;
					ItemId        one;
					std::uint32_t nKeys = 0;
					if (!Read(a_intfc, deviceRaw) || !ReadItem(a_intfc, one, false) || !Read(a_intfc, nKeys)) {
						logger::error("truncated hotkey #{}", i);
						break;
					}
					bind.device = static_cast<RE::INPUT_DEVICE>(deviceRaw);
					bool keysOk = true;
					for (std::uint32_t k = 0; k < nKeys; ++k) {
						std::uint32_t key = 0;
						if (!Read(a_intfc, key)) {
							keysOk = false;
							break;
						}
						bind.keys.push_back(key);
					}
					if (!keysOk) {
						logger::error("truncated chord on hotkey #{}", i);
						break;
					}
					bind.Canonicalize();
					saved.push_back(one);
				} else {
					if (!ReadBind(a_intfc, bind)) {
						logger::error("truncated chord on hotkey #{}", i);
						break;
					}
					std::uint32_t nItems = 0;
					if (!Read(a_intfc, nItems)) {
						logger::error("truncated item count on hotkey #{}", i);
						break;
					}
					bool itemsOk = true;
					saved.reserve(nItems);
					for (std::uint32_t n = 0; n < nItems; ++n) {
						ItemId id;
						if (!ReadItem(a_intfc, id, withHands)) {
							itemsOk = false;
							break;
						}
						saved.push_back(id);
					}
					if (!itemsOk) {
						logger::error("truncated items on hotkey #{}", i);
						break;
					}
				}

				if (!bind.IsValid()) {
					continue;
				}

				// Resolve each member. A member whose plugin is gone is dropped on its own;
				// only a chord left with nothing goes away entirely. Never fail the whole
				// load over one bad form -- that is how the reference mod wiped everything.
				std::vector<ItemId> items;
				items.reserve(saved.size());
				for (const auto& id : saved) {
					ItemId resolved;
					if (ResolveItem(a_intfc, id, resolved)) {
						items.push_back(resolved);
					}
				}
				if (items.empty()) {
					continue;
				}

				loaded.push_back(Hotkey{ std::move(bind), std::move(items) });
			}
		}

		HotkeyManager::GetSingleton()->ReplaceAll(std::move(loaded));
		logger::info("loaded {} hotkeys", HotkeyManager::GetSingleton()->GetAll().size());
	}

	void RevertCallback(SKSE::SerializationInterface*)
	{
		HotkeyManager::GetSingleton()->Clear();
	}

	void Register()
	{
		auto* ser = SKSE::GetSerializationInterface();
		ser->SetUniqueID(kUniqueID);
		ser->SetSaveCallback(SaveCallback);
		ser->SetLoadCallback(LoadCallback);
		ser->SetRevertCallback(RevertCallback);
	}
}
