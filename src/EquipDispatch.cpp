#include "EquipDispatch.h"

#include "Favorites.h"
#include "HotkeyManager.h"
#include "Settings.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <utility>
#include <vector>

namespace HKS::EquipDispatch
{
	namespace
	{
		// Core equip-slot forms (Skyrim.esm). Looked up by ID so we never go through
		// GetObject<T>'s RTTI cast on a possibly-bad default-object entry.
		RE::BGSEquipSlot* EquipSlot(RE::FormID a_id)
		{
			auto* form = RE::TESForm::LookupByID(a_id);
			return form ? form->As<RE::BGSEquipSlot>() : nullptr;
		}

		// Forms for which "which hand" is a real choice: one-handed weapons, staves, spells
		// and scrolls. A two-hander, bow or crossbow reports itself in both hands, so a bind
		// made while holding one used to remember "both" -- and then got forced into the
		// explicit right-hand slot instead of its own both-hands slot, a broken equip state
		// the engine's "restore the previous weapon" logic then trips over. Shields and
		// torches only ever go left and armour has no hand at all. None of those remember.
		[[nodiscard]] bool SupportsHandMemory(RE::TESForm* a_form)
		{
			if (!a_form) {
				return false;
			}
			if (auto* weap = a_form->As<RE::TESObjectWEAP>()) {
				return weap->IsOneHandedSword() || weap->IsOneHandedDagger() ||
				       weap->IsOneHandedAxe() || weap->IsOneHandedMace() || weap->IsStaff();
			}
			return a_form->Is(RE::FormType::Spell) || a_form->Is(RE::FormType::Scroll);
		}

		// Does the item fill the race's shield slot? Actor::GetShieldBiped (REL::ID 19204)
		// reads the slot off the race; the menu tests the armour's biped mask against it.
		[[nodiscard]] bool FillsShieldSlot(RE::Actor* a_actor, RE::TESBoundObject* a_object)
		{
			auto* armo = a_object->As<RE::TESObjectARMO>();
			auto* race = armo ? a_actor->GetRace() : nullptr;
			if (!race) {
				return false;
			}
			const auto slot = static_cast<std::uint32_t>(race->data.shieldObject.get());
			return slot < 32 && (std::to_underlying(armo->GetSlotMask()) & (1u << slot)) != 0;
		}

		// What the game does after every favorites equip or unequip, in the menu
		// (FavoritesMenu::UseQuickslotItem, REL::ID 50654) and on hotkeys 1-8 alike: bring
		// the actor's models up to date there and then, and for a shield run the step that
		// sets its biped part's flags to match the weapon state (REL::ID 39347).
		void FinishLikeVanilla(RE::Actor* a_actor, RE::TESBoundObject* a_object)
		{
			auto* process = a_actor->GetActorRuntimeData().currentProcess;
			if (!process) {
				return;
			}
			process->Update3DModel(a_actor);
			if (FillsShieldSlot(a_actor, a_object)) {
				using func_t = void (*)(RE::Actor*);
				static REL::Relocation<func_t> shieldStep{ REL::RelocationID(39347, 40418) };
				shieldStep(a_actor);
			}
		}

		// The inventory stack an item is equipped from (a_worn = false) or taken off from
		// (true), for binds that do not name an exact instance.
		//
		// The game's own toggle (REL::ID 37951) always hands EquipObject a stack's extra
		// list, and the equip event is built from it: the reference handle and unique ID in
		// that list are how Papyrus finds the script instance of the item in the inventory.
		// Without a list the event names no instance, so an item's own OnEquipped never
		// runs -- Hypercube's MISC item, which opens its storage chest from OnEquipped,
		// worked from the inventory and did nothing from a hotkey.
		[[nodiscard]] RE::ExtraDataList* StackList(RE::Actor* a_actor, RE::TESBoundObject* a_object, bool a_worn)
		{
			auto* changes = a_actor->GetInventoryChanges();
			if (!changes || !changes->entryList) {
				return nullptr;
			}
			for (auto* entry : *changes->entryList) {
				if (!entry || entry->object != a_object || !entry->extraLists) {
					continue;
				}
				for (auto* xl : *entry->extraLists) {
					if (!xl) {
						continue;
					}
					const bool worn = xl->HasType(RE::ExtraDataType::kWorn) || xl->HasType(RE::ExtraDataType::kWornLeft);
					if (worn == a_worn) {
						return xl;
					}
				}
			}
			return nullptr;
		}

		// Every item equip goes through here.
		//
		// The game's favorites never queue: both paths above equip through
		// ActorEquipManager's toggle (REL::ID 37951) as EquipObject(..., slot, false, false,
		// true, false) and finish with FinishLikeVanilla. Doing only half of that is what
		// kept losing meshes:
		//
		//  - queued (EquipObject's default): after a bow or other two-hander has been put
		//    away, a shield or torch going back into the left hand is equipped but never
		//    attached. Diagnosis from RavenKZP (Immersive Weapon Switch), on Extended Hotkey
		//    System's bug tracker.
		//  - immediate without the model update: with weapons drawn, the second of a dual
		//    pair or a two-hander replacing another comes in invisible, and a shield next
		//    to a right-hand weapon still does.
		//
		// A deferred Update3DModel after a queued swap was tried as well and stripped the
		// enchantment glow off weapons; this is the synchronous one, right after an
		// immediate equip, in the order the game uses. iEquipMode keeps the older modes.
		//
		// In the equip core (REL::ID 37963) the queue flag lands in params+0x20 and picks
		// Character::sub_1405F82E0 over sub_14060B9E0. CommonLib's ObjectEquipParams calls
		// that byte playEquipSounds; the name is wrong.
		//
		// Main thread only: hotkey fires run from the SKSE task queue, EquipNow from an
		// input handler.
		void EquipItem(RE::ActorEquipManager* a_em, RE::Actor* a_actor, RE::TESBoundObject* a_object,
			RE::ExtraDataList* a_xl = nullptr, std::uint32_t a_count = 1, const RE::BGSEquipSlot* a_slot = nullptr)
		{
			const auto mode = Settings::EquipMode();

			// Only wearables can go through the queue. The queued branch (REL::ID 36676)
			// hands books, food and potions to the direct one and silently drops the rest --
			// misc items, keys, soul gems -- which the direct branch would still turn into
			// an equip event (the "cannot equip" message comes with it, as in vanilla).
			// Mods listening for that event on a misc item need it to arrive.
			const bool queueable = a_object->Is(RE::FormType::Weapon) || a_object->Is(RE::FormType::Armor) ||
			                       a_object->Is(RE::FormType::Light) || a_object->Is(RE::FormType::Ammo) ||
			                       a_object->Is(RE::FormType::Scroll) || a_object->Is(RE::FormType::Projectile);

			bool queue = false;
			if (queueable && mode == 1) {
				auto* state = a_actor->AsActorState();
				queue = state && state->IsWeaponDrawn();
			} else if (queueable && mode == 2) {
				queue = true;
			}

			// The game's shield restore is broken, so do it here. Equipping a two-hander
			// makes the player remember what was in each hand (lastOneHandItems: 0 = left,
			// 1 = right); equipping a one-hand item afterwards gives back the other hand's.
			// The equip worker (REL::ID 37974) decides which by comparing the item's slot to
			// LeftHand -- but a shield's slot is Shield, whose parent is LeftHand, so it reads
			// as a right-hand item: the remembered LEFT weapon comes back into the left hand
			// and replaces the shield. Seen as sword+axe, then a bow, then a shield: the
			// shield's key put a weapon in the left hand. So take the right-hand weapon out
			// of that memory, clear it so the worker restores nothing, and put the weapon
			// back into the right hand ourselves -- what the worker means to do.
			RE::TESBoundObject* restoreRight = nullptr;
			if (auto* armo = a_object->As<RE::TESObjectARMO>(); armo && armo->IsShield() && a_actor->IsPlayerRef()) {
				auto& info = static_cast<RE::PlayerCharacter*>(a_actor)->GetInfoRuntimeData();
				if (a_em->unk01) {  // the worker's own "restore the other hand" switch
					restoreRight = info.lastOneHandItems[1];
				}
				info.lastOneHandItems[0] = nullptr;
				info.lastOneHandItems[1] = nullptr;
			}

			if (!a_xl) {
				a_xl = StackList(a_actor, a_object, false);
			}

			a_em->EquipObject(a_actor, a_object, a_xl, a_count, a_slot,
				queue,  // queueEquip
				false,  // forceEquip
				true,   // playSounds
				false); // applyNow

			if (restoreRight) {
				// Same conditions the worker's restore (REL::ID 37957) checks: still owned,
				// and the hand it goes to is free.
				auto*      process = a_actor->GetActorRuntimeData().currentProcess;
				const auto counts = a_actor->GetInventoryCounts(
					[&](RE::TESBoundObject& a_obj) { return std::addressof(a_obj) == restoreRight; });
				if (process && !process->GetEquippedRightHand() && !counts.empty() && counts.begin()->second > 0) {
					EquipItem(a_em, a_actor, restoreRight, nullptr, 1, EquipSlot(0x13F42));
				}
			}

			if (mode == 0) {
				FinishLikeVanilla(a_actor, a_object);
			}
		}

		// Every item unequip goes through here, for the same reason.
		void UnequipItem(RE::ActorEquipManager* a_em, RE::Actor* a_actor, RE::TESBoundObject* a_object,
			RE::ExtraDataList* a_xl)
		{
			const bool vanilla = Settings::EquipMode() == 0;
			if (!a_xl) {
				a_xl = StackList(a_actor, a_object, true);
			}
			a_em->UnequipObject(a_actor, a_object, a_xl, 1, nullptr,
				!vanilla,  // queueEquip
				false,     // forceEquip
				true,      // playSounds
				false);    // applyNow
			if (vanilla) {
				FinishLikeVanilla(a_actor, a_object);
			}
		}

		// A book cannot be equipped -- the equip core only plays its sound and sends the
		// equip event. Favorite Misc Items lets books be favorited and reads them by hooking
		// the call inside FavoritesMenu::UseQuickslotItem, which a hotkey here never goes
		// through, so this does what that hook does: the equip call first, as the menu
		// makes it, then a spell tome is learned and anything else is opened.
		void UseBook(RE::PlayerCharacter* a_player, RE::ActorEquipManager* a_em, RE::TESObjectBOOK* a_book,
			RE::ExtraDataList* a_xl)
		{
			EquipItem(a_em, a_player, a_book, a_xl);
			if (a_book->TeachesSpell()) {
				if (a_book->Read(a_player)) {
					a_player->RemoveItem(a_book, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
				}
				return;
			}
			RE::BSString text;
			a_book->GetDescription(text, nullptr);
			RE::NiMatrix3 rot{};
			rot.SetEulerAnglesXYZ(-0.05f, -0.05f, 1.50f);
			RE::BookMenu::OpenBookMenu(text, a_xl, nullptr, a_book, RE::NiPoint3{}, rot, 1.0f, true);
		}

		void EquipSpellForm(RE::PlayerCharacter* a_player, RE::ActorEquipManager* a_em,
			RE::SpellItem* a_spell, std::uint8_t a_hands)
		{
			constexpr RE::FormID kRight = 0x13F42;
			constexpr RE::FormID kLeft = 0x13F43;
			constexpr RE::FormID kBoth = 0x13F45;

			// Decide two-handedness by comparing the spell's equip-slot POINTER to the
			// both-hands slot -- never dereference the spell's slot (a custom/broken spell
			// can carry a dangling equip-slot pointer; IsTwoHanded() would crash on it).
			auto* spellSlot = a_spell->GetEquipSlot();
			auto* bothSlot = EquipSlot(kBoth);
			if (spellSlot && spellSlot == bothSlot) {
				a_em->EquipSpell(a_player, a_spell, bothSlot);
				return;
			}

			// Remembered hand wins: the spell goes back exactly where it was when the key
			// was assigned, every press. Held in both hands at that moment means both hands
			// on one press, which is the thing the alternating fallback below can only get
			// to on the second.
			if (a_hands != kHandNone) {
				if (a_hands & kHandRight) {
					a_em->EquipSpell(a_player, a_spell, EquipSlot(kRight));
				}
				if (a_hands & kHandLeft) {
					a_em->EquipSpell(a_player, a_spell, EquipSlot(kLeft));
				}
				return;
			}

			// No preference recorded: mirror vanilla favorites, prefer the empty/other hand.
			auto& rt = a_player->GetActorRuntimeData();
			if (rt.selectedSpells[RE::Actor::SlotTypes::kLeftHand] != a_spell) {
				a_em->EquipSpell(a_player, a_spell, EquipSlot(kLeft));
			} else if (rt.selectedSpells[RE::Actor::SlotTypes::kRightHand] != a_spell) {
				a_em->EquipSpell(a_player, a_spell, EquipSlot(kRight));
			}
		}

		// Read one ExtraDataList's instance identity (ench/uid/health) for comparison.
		void ReadListId(RE::ExtraDataList* a_xl, RE::FormID& a_ench, std::uint16_t& a_uid, std::int32_t& a_health)
		{
			a_ench = 0;
			a_uid = 0;
			a_health = 0;
			if (auto* e = a_xl->GetByType<RE::ExtraEnchantment>(); e && e->enchantment) {
				a_ench = e->enchantment->GetFormID();
			}
			if (auto* u = a_xl->GetByType<RE::ExtraUniqueID>()) {
				a_uid = u->uniqueID;
			}
			if (auto* h = a_xl->GetByType<RE::ExtraHealth>()) {
				a_health = static_cast<std::int32_t>(std::lround(h->health * 100.0f));
			}
		}

		// Find the inventory ExtraDataList for the exact instance the hotkey is bound to
		// (the enchanted/tempered copy, not a plain one of the same base form). Returns
		// nullptr for fungible binds (no instance data), letting the game pick any copy.
		RE::ExtraDataList* FindInstanceList(RE::TESBoundObject* a_bound, const ItemId& a_id)
		{
			// uid ignored (see ItemId::Same) -- it never identifies a distinct row, and old
			// co-saves may still carry a stale one.
			if (a_id.ench == 0 && a_id.health == 0) {
				return nullptr;
			}
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* changes = player ? player->GetInventoryChanges() : nullptr;
			if (!changes || !changes->entryList) {
				return nullptr;
			}
			for (auto* entry : *changes->entryList) {
				if (!entry || entry->object != a_bound || !entry->extraLists) {
					continue;
				}
				for (auto* xl : *entry->extraLists) {
					if (!xl) {
						continue;
					}
					RE::FormID    ench = 0;
					std::uint16_t uid = 0;
					std::int32_t  health = 0;
					ReadListId(xl, ench, uid, health);
					if (ench == a_id.ench && health == a_id.health) {
						return xl;
					}
				}
			}
			return nullptr;
		}

		// Count owned copies of a_bound that are interchangeable with the bound item --
		// same enchantment and temper, ANY ExtraUniqueID. uid is deliberately ignored: it
		// is only a per-instance tag. Vanilla stacks plain copies (uid 0), but mods like
		// Wheeler stamp every weapon/armor copy with a unique ExtraUniqueID, so two
		// otherwise-identical swords would look like distinct instances. For the dual-wield
		// decision they are the same item.
		std::int32_t CountMatchingInstances(RE::TESBoundObject* a_bound, RE::FormID a_ench, std::int32_t a_health)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return 0;
			}

			std::int32_t total = 0;
			auto counts = player->GetInventoryCounts(
				[&](RE::TESBoundObject& a_obj) { return std::addressof(a_obj) == a_bound; });
			if (!counts.empty()) {
				total = counts.begin()->second;
			}
			if (total <= 0) {
				return 0;
			}

			auto* changes = player->GetInventoryChanges();
			if (!changes || !changes->entryList) {
				// No per-instance data at all -> every copy is plain (ench/health 0).
				return (a_ench == 0 && a_health == 0) ? total : 0;
			}

			std::int32_t extraTotal = 0;
			std::int32_t matchedExtra = 0;
			for (auto* entry : *changes->entryList) {
				if (!entry || entry->object != a_bound || !entry->extraLists) {
					continue;
				}
				for (auto* xl : *entry->extraLists) {
					if (!xl) {
						continue;
					}
					RE::FormID    ench = 0;
					std::uint16_t uid = 0;
					std::int32_t  health = 0;
					ReadListId(xl, ench, uid, health);
					const auto c = xl->GetCount();
					extraTotal += c;
					if (ench == a_ench && health == a_health) {
						matchedExtra += c;
					}
				}
			}
			// Copies not covered by any extra list are plain (ench/health 0).
			const std::int32_t plain = total - extraTotal;
			std::int32_t matched = matchedExtra;
			if (plain > 0 && a_ench == 0 && a_health == 0) {
				matched += plain;
			}
			return matched;
		}

		// Return an inventory ExtraDataList matching (ench, health) -- any uid -- that is
		// NOT currently worn, so the dual-wield path equips a *different* physical copy to
		// the free hand. nullptr means "no distinct spare list" (plain stacked copies), in
		// which case the caller lets the engine pick a copy.
		RE::ExtraDataList* FindUnwornInstance(RE::TESBoundObject* a_bound, RE::FormID a_ench,
			std::int32_t a_health, RE::ExtraDataList* a_skip = nullptr)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* changes = player ? player->GetInventoryChanges() : nullptr;
			if (!changes || !changes->entryList) {
				return nullptr;
			}
			for (auto* entry : *changes->entryList) {
				if (!entry || entry->object != a_bound || !entry->extraLists) {
					continue;
				}
				for (auto* xl : *entry->extraLists) {
					if (!xl || xl == a_skip) {
						continue;
					}
					RE::FormID    ench = 0;
					std::uint16_t uid = 0;
					std::int32_t  health = 0;
					ReadListId(xl, ench, uid, health);
					if (ench != a_ench || health != a_health) {
						continue;
					}
					if (xl->HasType(RE::ExtraDataType::kWorn) || xl->HasType(RE::ExtraDataType::kWornLeft)) {
						continue;
					}
					return xl;
				}
			}
			return nullptr;
		}

		// Put an item in the hand(s) the bind remembers. Both hands means the player was
		// dual-wielding it when the key was assigned, so a second interchangeable copy goes
		// to the left -- FindUnwornInstance skips the one already going to the right, and a
		// null list is fine: plain stacked copies carry no extra data and the engine picks.
		void EquipToHands(RE::PlayerCharacter* a_player, RE::ActorEquipManager* a_em,
			RE::TESBoundObject* a_bound, const ItemId& a_id, RE::ExtraDataList* a_xl)
		{
			constexpr RE::FormID kRight = 0x13F42;
			constexpr RE::FormID kLeft = 0x13F43;

			if (a_id.hands == kHandBoth) {
				EquipItem(a_em, a_player, a_bound, a_xl, 1, EquipSlot(kRight));
				if (CountMatchingInstances(a_bound, a_id.ench, a_id.health) >= 2) {
					EquipItem(a_em, a_player, a_bound,
						FindUnwornInstance(a_bound, a_id.ench, a_id.health, a_xl), 1, EquipSlot(kLeft));
				}
				return;
			}
			EquipItem(a_em, a_player, a_bound, a_xl, 1,
				EquipSlot((a_id.hands & kHandRight) != 0 ? kRight : kLeft));
		}

		// Is the bound item on the player right now?
		//
		// The hand slots alone are not the answer. They only ever hold weapons, shields,
		// torches and spells, so an armour piece looks "not equipped" no matter what -- and
		// the toggle below then tried to equip it again instead of taking it off. That was
		// the bug: cuirasses and helmets could be put on with their hotkey but never off.
		//
		// a_xl is the exact instance the bind points at when there is one; otherwise any
		// worn copy of the base object counts, which is what a fungible bind means.
		bool IsWornNow(RE::PlayerCharacter* a_player, RE::TESForm* a_form,
			RE::TESBoundObject* a_bound, const ItemId& a_id, RE::ExtraDataList* a_xl)
		{
			if (a_xl) {
				return a_xl->HasType(RE::ExtraDataType::kWorn) ||
				       a_xl->HasType(RE::ExtraDataType::kWornLeft);
			}
			auto* proc = a_player->GetActorRuntimeData().currentProcess;
			if (proc && (proc->GetEquippedRightHand() == a_form || proc->GetEquippedLeftHand() == a_form)) {
				return true;
			}
			auto* changes = a_player->GetInventoryChanges();
			if (!changes || !changes->entryList) {
				return false;
			}
			// Match the enchant/temper the bind names, not just the base object: an
			// enchanted cuirass and a plain one share a row, and taking the enchanted one
			// off because the plain one's hotkey was pressed is the same class of mistake
			// the instance-precise toggle exists to avoid. Wearing anything at all creates
			// the extra-data list that carries kWorn, so a plain worn copy is found here.
			for (auto* entry : *changes->entryList) {
				if (!entry || entry->object != a_bound || !entry->extraLists) {
					continue;
				}
				for (auto* xl : *entry->extraLists) {
					if (!xl || !(xl->HasType(RE::ExtraDataType::kWorn) ||
									xl->HasType(RE::ExtraDataType::kWornLeft))) {
						continue;
					}
					RE::FormID    ench = 0;
					std::uint16_t uid = 0;
					std::int32_t  health = 0;
					ReadListId(xl, ench, uid, health);
					if (ench == a_id.ench && health == a_id.health) {
						return true;
					}
				}
			}
			return false;
		}

		// Part of a "loadout" for the group toggle: something that stays on until it is
		// taken off. Potions and food are used up, books are read, and misc items, keys and
		// soul gems only fire their equip event -- none of them is ever worn, so they must
		// not keep a group from reading as fully equipped, and are left alone when it is
		// stripped.
		bool IsLoadoutGear(RE::TESForm* a_form)
		{
			return a_form->Is(RE::FormType::Weapon) || a_form->Is(RE::FormType::Armor) ||
			       a_form->Is(RE::FormType::Light) || a_form->Is(RE::FormType::Ammo) ||
			       a_form->Is(RE::FormType::Scroll);
		}

		void EquipForm(const ItemId& a_id)
		{
			auto* form = RE::TESForm::LookupByID(a_id.form);
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* em = RE::ActorEquipManager::GetSingleton();
			if (!player || !em || !form) {
				return;
			}

			switch (form->GetFormType()) {
			case RE::FormType::Spell:
				if (auto* spell = form->As<RE::SpellItem>()) {
					EquipSpellForm(player, em, spell, a_id.hands);
				}
				break;

			case RE::FormType::Shout:
				if (auto* shout = form->As<RE::TESShout>()) {
					em->EquipShout(player, shout);
				}
				break;

			default:
				{
					auto* bound = form->As<RE::TESBoundObject>();
					if (!bound) {
						break;
					}
					if (auto* book = form->As<RE::TESObjectBOOK>()) {
						UseBook(player, em, book, FindInstanceList(bound, a_id));
						break;
					}

					auto* proc = player->GetActorRuntimeData().currentProcess;
					const bool inRight = proc && proc->GetEquippedRightHand() == form;
					const bool inLeft = proc && proc->GetEquippedLeftHand() == form;

					// Checked here as well as at assignment, so a bind saved before the
					// check existed (a two-hander remembering "both") is ignored too.
					const bool handMemory = a_id.hands != kHandNone && SupportsHandMemory(form);

					auto* weap = form->As<RE::TESObjectWEAP>();
					const bool dualWield = weap &&
					                       (weap->IsOneHandedSword() || weap->IsOneHandedDagger() ||
					                        weap->IsOneHandedAxe() || weap->IsOneHandedMace() || weap->IsStaff());

					// --- Dual wield (runs BEFORE the instance-precise toggle) ---
					// One-handed weapon already held in exactly one hand, with a second
					// interchangeable copy (same enchant/temper, any ExtraUniqueID) in the
					// pack -> equip that copy to the free hand, matching vanilla favorites
					// (two presses = dual wield). Placed first, and keyed on ench/temper
					// rather than uid, so it still fires when a mod (e.g. Wheeler) stamps a
					// unique ExtraUniqueID on every weapon copy -- otherwise our bind reads
					// as one distinct instance and the toggle below just puts it away.
					if (!handMemory && dualWield && (inRight != inLeft) &&
						CountMatchingInstances(bound, a_id.ench, a_id.health) >= 2) {
						constexpr RE::FormID kRight = 0x13F42;
						constexpr RE::FormID kLeft = 0x13F43;
						auto* freeXl = FindUnwornInstance(bound, a_id.ench, a_id.health);
						EquipItem(em, player, bound, freeXl, 1, EquipSlot(inRight ? kLeft : kRight));
						break;
					}

					auto* xl = FindInstanceList(bound, a_id);

					// --- Remembered hand ---
					// Assigned while the item was in hand, so it goes back to that hand
					// rather than to whichever one the engine feels like. Still a toggle:
					// pressing it while worn puts it away, same as everything else.
					if (handMemory) {
						if (IsWornNow(player, form, bound, a_id, xl)) {
							UnequipItem(em, player, bound, xl);
						} else {
							EquipToHands(player, em, bound, a_id, xl);
						}
						break;
					}

					// --- Instance-precise bind (enchanted/tempered/unique copy) ---
					// You only ever own the one specific instance, so a plain toggle is
					// correct. Test "worn" on the EXACT instance, not the base form;
					// otherwise pressing the enchanted sword's hotkey while a plain one
					// (same base) is in hand reads as "already worn" and unequips instead
					// of switching.
					if (xl) {
						if (IsWornNow(player, form, bound, a_id, xl)) {
							UnequipItem(em, player, bound, xl);
						} else {
							EquipItem(em, player, bound, xl);
						}
						break;
					}

					// --- Fungible bind (plain copies of a base form) ---
					// Default toggle: equip if nothing of the kind is on, else take it off.
					// "On" has to cover the armour slots too, not just the hands.
					if (IsWornNow(player, form, bound, a_id, nullptr)) {
						UnequipItem(em, player, bound, nullptr);
					} else {
						EquipItem(em, player, bound, nullptr);
					}
				}
				break;
			}
		}

		// Slots a group member can claim. Tracked per hand rather than as a count so a
		// shield or torch (which can only go left) doesn't consume the right hand.
		struct Hands
		{
			bool right = false;
			bool left = false;
		};

		// Equip one member of a group. No toggling here -- EquipSet decides that for the set
		// as a whole. A member that remembers a hand goes back to it and claims that hand;
		// the rest take the right hand first, then the left, in the order they were added.
		void EquipGroupMember(RE::PlayerCharacter* a_player, RE::ActorEquipManager* a_em,
			const ItemId& a_id, Hands& a_hands)
		{
			constexpr RE::FormID kRight = 0x13F42;
			constexpr RE::FormID kLeft = 0x13F43;
			constexpr RE::FormID kBoth = 0x13F45;

			auto* form = RE::TESForm::LookupByID(a_id.form);
			if (!form) {
				return;
			}

			// Voice forms never touch the hands.
			if (auto* shout = form->As<RE::TESShout>()) {
				a_em->EquipShout(a_player, shout);
				return;
			}
			if (auto* spell = form->As<RE::SpellItem>()) {
				if (IsVoiceForm(form)) {
					a_em->EquipSpell(a_player, spell, spell->GetEquipSlot());
					return;
				}
				if (a_id.hands != kHandNone) {
					EquipSpellForm(a_player, a_em, spell, a_id.hands);
					a_hands.right = a_hands.right || (a_id.hands & kHandRight) != 0;
					a_hands.left = a_hands.left || (a_id.hands & kHandLeft) != 0;
					return;
				}
				// Compare the equip-slot POINTER rather than calling IsTwoHanded(): a broken
				// or custom spell can carry a dangling slot pointer.
				if (spell->GetEquipSlot() && spell->GetEquipSlot() == EquipSlot(kBoth)) {
					a_em->EquipSpell(a_player, spell, EquipSlot(kBoth));
					a_hands.right = a_hands.left = true;
				} else if (!a_hands.right) {
					a_em->EquipSpell(a_player, spell, EquipSlot(kRight));
					a_hands.right = true;
				} else if (!a_hands.left) {
					a_em->EquipSpell(a_player, spell, EquipSlot(kLeft));
					a_hands.left = true;
				}
				return;
			}

			auto* bound = form->As<RE::TESBoundObject>();
			if (!bound) {
				return;
			}
			// Pick the exact instance the bind points at (enchanted/tempered copy); null for
			// a fungible bind, which lets the engine take any copy.
			auto* xl = FindInstanceList(bound, a_id);

			if (auto* book = form->As<RE::TESObjectBOOK>()) {
				UseBook(a_player, a_em, book, xl);
				return;
			}

			if (auto* weap = form->As<RE::TESObjectWEAP>()) {
				const bool oneHanded = weap->IsOneHandedSword() || weap->IsOneHandedDagger() ||
				                       weap->IsOneHandedAxe() || weap->IsOneHandedMace() || weap->IsStaff();
				if (oneHanded && a_id.hands != kHandNone) {
					EquipToHands(a_player, a_em, bound, a_id, xl);
					a_hands.right = a_hands.right || (a_id.hands & kHandRight) != 0;
					a_hands.left = a_hands.left || (a_id.hands & kHandLeft) != 0;
					return;
				}
				if (!oneHanded) {  // greatsword, bow, crossbow -- takes everything
					EquipItem(a_em, a_player, bound, xl);
					a_hands.right = a_hands.left = true;
				} else if (!a_hands.right) {
					EquipItem(a_em, a_player, bound, xl, 1, EquipSlot(kRight));
					a_hands.right = true;
				} else if (!a_hands.left) {
					EquipItem(a_em, a_player, bound, xl, 1, EquipSlot(kLeft));
					a_hands.left = true;
				}
				return;
			}

			// Shields and torches are left-hand only; they must not eat the right hand, so a
			// group of "shield, sword" still puts the sword where it belongs.
			auto*      armo = form->As<RE::TESObjectARMO>();
			const bool leftOnly = (armo && armo->IsShield()) || form->Is(RE::FormType::Light);
			if (leftOnly) {
				if (!a_hands.left) {
					EquipItem(a_em, a_player, bound, xl);
					a_hands.left = true;
				}
				return;
			}

			// A scroll is cast from a hand like a spell.
			if (form->Is(RE::FormType::Scroll)) {
				if (!a_hands.right) {
					EquipItem(a_em, a_player, bound, xl, 1, EquipSlot(kRight));
					a_hands.right = true;
				} else if (!a_hands.left) {
					EquipItem(a_em, a_player, bound, xl, 1, EquipSlot(kLeft));
					a_hands.left = true;
				}
				return;
			}

			// Armour, ammo, potions, food -- no hand bookkeeping. Potions and food are
			// consumed here, which is what a group like "armour + healing potion" is for.
			EquipItem(a_em, a_player, bound, xl);
		}

		// Take the whole set off. Consumables are skipped -- stripping a loadout must not
		// drink the potion that is on the same key. Spells and shouts stay put: the engine
		// offers no "unequip spell", and vanilla favorites never took one off either.
		void UnequipSet(RE::PlayerCharacter* a_player, RE::ActorEquipManager* a_em,
			const std::vector<ItemId>& a_items)
		{
			for (const auto& id : a_items) {
				auto* form = RE::TESForm::LookupByID(id.form);
				if (!form || !IsLoadoutGear(form)) {
					continue;
				}
				auto* bound = form->As<RE::TESBoundObject>();
				UnequipItem(a_em, a_player, bound, FindInstanceList(bound, id));
			}
		}

		void EquipSet(const std::vector<ItemId>& a_items)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* em = RE::ActorEquipManager::GetSingleton();
			if (!player || !em) {
				return;
			}

			// A group toggles like a single bind does, just on the whole set: everything in
			// it already on -> take it all off, anything missing -> put the set on. Without
			// this, gear that was perfectly removable on its own became stuck the moment it
			// shared a key with something else.
			bool anyGear = false;
			bool allWorn = true;
			for (const auto& id : a_items) {
				auto* form = RE::TESForm::LookupByID(id.form);
				if (!form || !IsLoadoutGear(form)) {
					continue;
				}
				anyGear = true;
				auto* bound = form->As<RE::TESBoundObject>();
				if (!IsWornNow(player, form, bound, id, FindInstanceList(bound, id))) {
					allWorn = false;
					break;
				}
			}
			if (anyGear && allWorn) {
				UnequipSet(player, em, a_items);
				return;
			}

			Hands hands;
			for (const auto& id : a_items) {
				EquipGroupMember(player, em, id, hands);
			}
		}
	}

	std::uint8_t CurrentHands(RE::TESForm* a_form)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || !a_form) {
			return kHandNone;
		}
		if (!SupportsHandMemory(a_form)) {
			return kHandNone;
		}
		std::uint8_t mask = kHandNone;
		// Magic lives in selectedSpells, not in the process's hand slots -- the same place
		// EquipSpellForm reads when it decides which hand is free.
		if (auto* spell = a_form->As<RE::SpellItem>()) {
			auto& rt = player->GetActorRuntimeData();
			if (rt.selectedSpells[RE::Actor::SlotTypes::kRightHand] == spell) {
				mask |= kHandRight;
			}
			if (rt.selectedSpells[RE::Actor::SlotTypes::kLeftHand] == spell) {
				mask |= kHandLeft;
			}
			return mask;
		}
		auto* proc = player->GetActorRuntimeData().currentProcess;
		if (!proc) {
			return kHandNone;
		}
		if (proc->GetEquippedRightHand() == a_form) {
			mask |= kHandRight;
		}
		if (proc->GetEquippedLeftHand() == a_form) {
			mask |= kHandLeft;
		}
		return mask;
	}

	bool IsVoiceForm(RE::TESForm* a_form)
	{
		if (!a_form) {
			return false;
		}
		if (a_form->Is(RE::FormType::Shout)) {
			return true;
		}
		if (auto* spell = a_form->As<RE::SpellItem>()) {
			const auto* slot = spell->GetEquipSlot();
			return slot && slot->GetFormID() == 0x25BEE;  // Voice slot
		}
		return false;
	}

	void Fire(std::vector<ItemId> a_items)
	{
		if (a_items.empty()) {
			return;
		}
		auto* task = SKSE::GetTaskInterface();
		if (!task) {
			return;
		}
		task->AddTask([items = std::move(a_items)]() {
			// Reconcile before equipping. An item the player un-favorited loses its place in
			// the bind; one they merely ran out of keeps it (PickupWatch restores the star
			// when it comes back), it is just skipped this press.
			std::vector<ItemId> live;
			live.reserve(items.size());
			for (const auto& id : items) {
				const auto state = Favorites::Query(id.form);
				if (state == Favorites::State::kUnfavorited) {
					HotkeyManager::GetSingleton()->RemoveByItem(id);
					logger::info("fire {:08X}: un-favorited -> dropped from its hotkey", id.form);
					continue;
				}
				if (state == Favorites::State::kAbsent) {
					if (Settings::DebugLog()) {
						logger::info("fire {:08X}: none held -> skipped, binding kept", id.form);
					}
					continue;
				}
				live.push_back(id);
			}
			if (live.empty()) {
				return;
			}

			if (Settings::DebugLog()) {
				logger::info("equip fire -> {} item(s), first form {:08X} ench {:08X} hp {}",
					live.size(), live.front().form, live.front().ench, live.front().health);
			}

			// One item keeps the toggle; a group is a loadout and only ever equips.
			if (live.size() == 1) {
				EquipForm(live.front());
			} else {
				EquipSet(live);
			}
		});
	}

	namespace
	{
		// Forms an API consumer equipped a moment ago, with the time it happened. Small and
		// short-lived, so a flat list beats a map. Guarded because EquipNow is called from
		// an input handler while our own sink reads it from the same dispatch.
		constexpr auto kClaimWindow = std::chrono::milliseconds(100);

		std::mutex                                                                       g_claimLock;
		std::vector<std::pair<RE::FormID, std::chrono::steady_clock::time_point>>        g_claims;
	}

	bool EquipNow(const ItemId& a_id)
	{
		if (!a_id) {
			return false;
		}
		const auto state = Favorites::Query(a_id.form);
		if (state == Favorites::State::kUnfavorited) {
			HotkeyManager::GetSingleton()->RemoveByItem(a_id);
			logger::info("EquipNow {:08X}: un-favorited -> binding removed", a_id.form);
			return false;
		}
		if (state == Favorites::State::kAbsent) {
			// The player no longer holds it (used the last one, lost the power).
			if (Settings::DebugLog()) {
				logger::info("EquipNow {:08X}: player no longer has it", a_id.form);
			}
			return false;
		}

		EquipForm(a_id);

		const auto now = std::chrono::steady_clock::now();
		{
			std::scoped_lock lk(g_claimLock);
			std::erase_if(g_claims, [&](const auto& c) { return now - c.second > kClaimWindow; });
			g_claims.emplace_back(a_id.form, now);
		}
		return true;
	}

	bool IsClaimed(const ItemId& a_id)
	{
		const auto       now = std::chrono::steady_clock::now();
		std::scoped_lock lk(g_claimLock);
		std::erase_if(g_claims, [&](const auto& c) { return now - c.second > kClaimWindow; });
		return std::any_of(g_claims.begin(), g_claims.end(),
			[&](const auto& c) { return c.first == a_id.form; });
	}
}
