#include "VanillaMigration.h"

#include "HotkeyManager.h"
#include "InventoryIcons.h"
#include "Settings.h"

#include <cmath>

namespace HKS::VanillaMigration
{
	namespace
	{
		// Vanilla favorites slots are number keys 1..8 -> DX scancodes 0x02..0x09.
		constexpr std::uint32_t kKey1Scancode = 0x02;

		Bind NumberKeyBind(std::uint8_t a_slot)
		{
			Bind bind;
			bind.device = RE::INPUT_DEVICE::kKeyboard;
			bind.keys = { kKey1Scancode + a_slot };
			bind.Canonicalize();
			return bind;
		}

		// Is this chord already used by one of our binds? (Don't steal a key the player
		// has already assigned in our system.)
		bool BindTaken(const Bind& a_bind)
		{
			std::unordered_set<std::uint32_t> held(a_bind.keys.begin(), a_bind.keys.end());
			return HotkeyManager::GetSingleton()->ResolveChord(a_bind.device, held) != nullptr;
		}

		// Identity for one specific ExtraDataList (the exact instance carrying the hotkey).
		ItemId ListIdentity(RE::FormID a_form, RE::ExtraDataList* a_xl)
		{
			ItemId id;
			id.form = a_form;
			if (!a_xl) {
				return id;
			}
			if (auto* e = a_xl->GetByType<RE::ExtraEnchantment>(); e && e->enchantment) {
				id.ench = e->enchantment->GetFormID();
			}
			if (auto* u = a_xl->GetByType<RE::ExtraUniqueID>()) {
				id.uid = u->uniqueID;
			}
			if (auto* h = a_xl->GetByType<RE::ExtraHealth>()) {
				id.health = static_cast<std::int32_t>(std::lround(h->health * 100.0f));
			}
			return id;
		}

		// Item favorites: scan the player's inventory for ExtraHotkey slots.
		int MigrateItems()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* changes = player ? player->GetInventoryChanges() : nullptr;
			if (!changes || !changes->entryList) {
				return 0;
			}

			auto* mgr = HotkeyManager::GetSingleton();
			int   migrated = 0;

			for (auto* entry : *changes->entryList) {
				if (!entry || !entry->object || !entry->extraLists) {
					continue;
				}
				for (auto* xl : *entry->extraLists) {
					if (!xl) {
						continue;
					}
					auto* eh = xl->GetByType<RE::ExtraHotkey>();
					if (!eh || eh->hotkey == RE::ExtraHotkey::Hotkey::kUnbound) {
						continue;
					}

					const auto slot = static_cast<std::uint8_t>(eh->hotkey.get());
					if (slot > 7) {
						continue;
					}
					const ItemId id = ListIdentity(entry->object->GetFormID(), xl);
					const Bind   bind = NumberKeyBind(slot);

					// Keep our own bind if this item already has one; just retire the
					// vanilla slot. Otherwise adopt it, unless the key is already taken.
					const bool ours = mgr->FindByItem(id) != nullptr;
					if (!ours && !BindTaken(bind)) {
						mgr->Assign(bind, id);
						++migrated;
						logger::info("migrated vanilla item hotkey: slot {} -> key {} form {:08X} ench {:08X} uid {} hp {}",
							slot, slot + 1, id.form, id.ench, id.uid, id.health);
					}
					eh->hotkey = RE::ExtraHotkey::Hotkey::kUnbound;  // keep favorited, drop the vanilla key
				}
			}
			return migrated;
		}

		// Magic favorites: MagicFavorites::hotkeys[slot] -> form.
		int MigrateMagic()
		{
			auto* mf = RE::MagicFavorites::GetSingleton();
			if (!mf) {
				return 0;
			}

			auto* mgr = HotkeyManager::GetSingleton();
			int   migrated = 0;

			const auto count = mf->hotkeys.size();
			for (std::uint32_t i = 0; i < count && i < 8; ++i) {
				auto* form = mf->hotkeys[i];
				if (!form) {
					continue;
				}
				ItemId id;
				id.form = form->GetFormID();
				const Bind bind = NumberKeyBind(static_cast<std::uint8_t>(i));

				const bool ours = mgr->FindByForm(id.form) != nullptr;
				if (!ours && !BindTaken(bind)) {
					mgr->Assign(bind, id);
					++migrated;
					logger::info("migrated vanilla magic hotkey: slot {} -> key {} form {:08X}",
						i, i + 1, id.form);
				}
				mf->hotkeys[i] = nullptr;  // stays in `spells` (favorited), loses the vanilla key
			}
			return migrated;
		}
	}

	void Migrate()
	{
		if (!Settings::MigrateVanillaHotkeys()) {
			return;
		}

		const int items = MigrateItems();
		const int magic = MigrateMagic();
		if (items || magic) {
			InventoryIcons::MarkDirty();
			logger::info("vanilla hotkey migration: {} item(s), {} magic adopted", items, magic);
		}
	}
}
