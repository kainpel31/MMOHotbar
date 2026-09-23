#include "Favorites.h"

#include "HotkeyManager.h"
#include "InventoryIcons.h"
#include "Settings.h"

#include <chrono>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace HKS::Favorites
{
	namespace
	{
		// Forms with an EnsureFavorited task still in flight. PruneUnfavorited must skip
		// them: the favorite hasn't landed yet, so IsFavorited() would read false and drop
		// the binding that was just assigned (the live prune runs every few frames, so it
		// can and does interleave with the task queue).
		std::mutex                     g_pendingLock;
		std::unordered_set<RE::FormID> g_pendingFavorite;

		// Forms already reported as "none held" -- keeps the per-frame prune from logging
		// the same waiting binding over and over. Cleared when the form comes back.
		std::unordered_set<RE::FormID> g_absentLogged;

		// Grace window after we favorite something. The prune runs every few frames, and
		// a favorite needs the list rebuild to settle before it reads back reliably; without
		// this, a just-assigned binding could be pruned as "un-favorited" moments later.
		constexpr auto kFavoriteGrace = std::chrono::seconds(3);
		std::mutex     g_recentLock;
		std::unordered_map<RE::FormID, std::chrono::steady_clock::time_point> g_recentFavorite;

		void MarkFavorited(RE::FormID a_form)
		{
			std::scoped_lock lk(g_recentLock);
			g_recentFavorite[a_form] = std::chrono::steady_clock::now();
		}

		bool WithinFavoriteGrace(RE::FormID a_form)
		{
			std::scoped_lock lk(g_recentLock);
			const auto       it = g_recentFavorite.find(a_form);
			if (it == g_recentFavorite.end()) {
				return false;
			}
			if (std::chrono::steady_clock::now() - it->second > kFavoriteGrace) {
				g_recentFavorite.erase(it);
				return false;
			}
			return true;
		}

		// Make the open item menu re-read favorite state so the star/badge shows live
		// (otherwise it only appears after reopening). Mirrors the list-refresh the
		// game's own favorite handler does.
		void RefreshOpenItemList()
		{
			auto* ui = RE::UI::GetSingleton();
			if (!ui) {
				return;
			}
			static constexpr const char* kMenus[] = {
				"InventoryMenu", "ContainerMenu", "MagicMenu", "GiftMenu", "BarterMenu"
			};
			for (auto* m : kMenus) {
				auto menu = ui->GetMenu(m);
				if (!menu || !menu->uiMovie) {
					continue;
				}
				RE::GFxValue list;
				if (menu->uiMovie->GetVariable(&list, "_root.Menu_mc.inventoryLists.panelContainer.itemList") &&
					list.IsObject()) {
					list.Invoke("InvalidateData");
					return;
				}
			}
		}

		// The magic menu has its own list-refresh (different from the inventory one).
		void RefreshMagicMenu()
		{
			auto* ui = RE::UI::GetSingleton();
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!ui || !player) {
				return;
			}
			auto menu = ui->GetMenu<RE::MagicMenu>();
			if (!menu) {
				return;
			}
			void* magicList = menu->GetRuntimeData().unk30;
			if (!magicList) {
				return;
			}
			using Refresh_t = void (*)(void*, RE::PlayerCharacter*);
			static REL::Relocation<Refresh_t> refresh{ REL::RelocationID(51223, 52098) };
			refresh(magicList, player);
		}

		void FavoriteNow(RE::FormID a_form)
		{
			auto* form = RE::TESForm::LookupByID(a_form);
			if (!form) {
				return;
			}

			const auto ft = form->GetFormType();
			if (ft == RE::FormType::Spell || ft == RE::FormType::Shout) {
				if (auto* mf = RE::MagicFavorites::GetSingleton()) {
					mf->SetFavorite(form);
					MarkFavorited(a_form);
					logger::info("favorited magic {:08X}", a_form);
				}
				RefreshMagicMenu();
				// The refresh rebuilds the list entries, dropping our scancode stamps;
				// flag a re-stamp so the keycap reappears without a menu reopen.
				InventoryIcons::MarkDirty();
				return;
			}

			auto* bound = form->As<RE::TESBoundObject>();
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!bound || !player) {
				return;
			}
			auto* changes = player->GetInventoryChanges();
			if (!changes) {
				logger::warn("favorite: no inventory changes for {:08X}", a_form);
				return;
			}

			// Plain items aren't in changes->entryList yet, but the game's SetFavorite
			// finds-or-creates the real entry from object alone -- so hand it a throwaway
			// entry keyed only by the bound object (extraLists stays null -> dtor is a
			// no-op). This mirrors how the inventory menu favorites an item.
			RE::InventoryEntryData ied{ bound, 0 };
			changes->SetFavorite(std::addressof(ied), nullptr);
			MarkFavorited(a_form);
			logger::info("favorited item {:08X}", a_form);
			RefreshOpenItemList();
		}
	}

	void EnsureFavorited(RE::FormID a_form)
	{
		if (!a_form) {
			return;
		}
		auto* task = SKSE::GetTaskInterface();
		if (!task) {
			return;
		}
		{
			std::scoped_lock lk(g_pendingLock);
			g_pendingFavorite.insert(a_form);
		}
		task->AddTask([a_form]() {
			FavoriteNow(a_form);
			std::scoped_lock lk(g_pendingLock);
			g_pendingFavorite.erase(a_form);
		});
	}

	State Query(RE::FormID a_form)
	{
		auto* form = RE::TESForm::LookupByID(a_form);
		if (!form) {
			return State::kAbsent;
		}

		const auto ft = form->GetFormType();
		if (ft == RE::FormType::Spell || ft == RE::FormType::Shout) {
			// The player can LOSE a spell/power/ability/shout (perk or quest removed it,
			// race change, a temporary blessing expiring). MagicFavorites is not cleaned up
			// when that happens, so a stale entry there would keep the hotkey casting
			// something the player no longer has. Actor::HasSpell walks addedSpells + the
			// base/race lists, which is exactly where granted abilities live.
			if (auto* player = RE::PlayerCharacter::GetSingleton()) {
				bool known = false;
				if (auto* spell = form->As<RE::SpellItem>()) {
					known = player->HasSpell(spell);
				} else if (auto* shout = form->As<RE::TESShout>()) {
					known = player->HasShout(shout);
				}
				if (!known) {
					// Treated like an emptied stack: don't cast, but KEEP the binding -- the
					// power may come back (werewolf form, a quest re-granting it).
					return State::kAbsent;
				}
			}
			if (auto* mf = RE::MagicFavorites::GetSingleton()) {
				for (auto* s : mf->spells) {
					if (s == form) {
						return State::kFavorited;
					}
				}
			}
			// The player still knows the spell/shout, it's just not favorited.
			return State::kUnfavorited;
		}

		auto* bound = form->As<RE::TESBoundObject>();
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!bound || !player) {
			return State::kAbsent;
		}

		// Authoritative "do we hold any" (base container + changes). An emptied stack can
		// leave a changes row behind, so presence must not be inferred from the row alone.
		auto               counts = player->GetInventoryCounts(
            [&](RE::TESBoundObject& a_obj) { return std::addressof(a_obj) == bound; });
		const std::int32_t held = counts.empty() ? 0 : counts.begin()->second;
		if (held <= 0) {
			return State::kAbsent;
		}

		auto* changes = player->GetInventoryChanges();
		if (changes && changes->entryList) {
			for (auto* entry : *changes->entryList) {
				if (entry && entry->object == bound) {
					return entry->IsFavorited() ? State::kFavorited : State::kUnfavorited;
				}
			}
		}
		return State::kUnfavorited;  // held, but no changes row -> can't carry a hotkey
	}

	bool IsFavorited(RE::FormID a_form)
	{
		return Query(a_form) == State::kFavorited;
	}

	bool PruneUnfavorited()
	{
		auto* mgr = HotkeyManager::GetSingleton();
		bool  changed = false;
		for (const auto& h : mgr->Snapshot()) {
			// Members are judged one at a time: un-favoriting one piece of a group should
			// take that piece out and leave the rest of the loadout alone.
			for (const auto& id : h.items) {
				{
					std::scoped_lock lk(g_pendingLock);
					if (g_pendingFavorite.contains(id.form)) {
						continue;  // auto-favorite from a fresh assignment still in flight
					}
				}
				if (WithinFavoriteGrace(id.form)) {
					continue;  // just favorited -- let the list settle before judging it
				}

				const auto state = Query(id.form);
				if (state == State::kAbsent) {
					// Out of stock, not un-favorited: keep the binding. PickupWatch
					// re-favorites the form when it returns. Logged once per transition so
					// the per-frame prune doesn't spam.
					if (g_absentLogged.insert(id.form).second && Settings::DebugLog()) {
						logger::info("hotkey {:08X}: none held -> binding KEPT (waiting for pickup)",
							id.form);
					}
					continue;
				}
				g_absentLogged.erase(id.form);

				if (state == State::kUnfavorited) {
					mgr->RemoveByItem(id);
					logger::info("pruned hotkey {:08X} ench {:08X} hp {} (un-favorited by player)",
						id.form, id.ench, id.health);
					changed = true;
				}
			}
		}
		return changed;
	}

	bool FavoriteSelectedItem(RE::FormID a_expected)
	{
		if (!a_expected) {
			return false;
		}

		// All of this used to be hand-rolled against SE-1.5.97 addresses (ItemList
		// get_selected id 50086 + a raw refresh pointer), which is why it bailed out on
		// anything but SE. CommonLibSSE-NG exposes both: GetSelectedItem() is pure C++
		// (it just reads selectedIndex off the GFx root and indexes the item array, the
		// same thing the native function did), and Update() carries the dual-runtime id.
		// So the instant-star path now works on AE too.
		RE::ItemList* itemList = nullptr;
		if (auto* ui = RE::UI::GetSingleton()) {
			if (auto menu = ui->GetMenu<RE::InventoryMenu>()) {
				itemList = menu->GetRuntimeData().itemList;
			} else if (auto cont = ui->GetMenu<RE::ContainerMenu>()) {
				itemList = cont->GetRuntimeData().itemList;
			}
		}
		if (!itemList) {
			return false;
		}

		auto* item = itemList->GetSelectedItem();
		if (!item) {
			return false;
		}
		auto* entry = item->data.objDesc;
		if (!entry || !entry->object || entry->object->GetFormID() != a_expected) {
			return false;  // selection moved off the locked item
		}

		bool alreadyFav = false;
		RE::ExtraDataList* firstList = nullptr;
		if (entry->extraLists) {
			for (auto* xl : *entry->extraLists) {
				if (!xl) {
					continue;
				}
				if (!firstList) {
					firstList = xl;
				}
				if (xl->HasType(RE::ExtraDataType::kHotkey)) {
					alreadyFav = true;
					break;
				}
			}
		}

		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* changes = player ? player->GetInventoryChanges() : nullptr;
		if (!changes) {
			return false;
		}
		if (!alreadyFav) {
			changes->SetFavorite(entry, firstList);
		}

		// Live list refresh, exactly what the game's favorite handler calls.
		itemList->Update(player);

		MarkFavorited(a_expected);
		logger::info("favorited (real entry) {:08X}", a_expected);
		return true;
	}
}
