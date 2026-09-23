#pragma once

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <vector>

namespace HKS
{
	// A chord: the set of scancodes that must be held *simultaneously* to fire.
	// `keys` is kept sorted-ascending and de-duplicated so two binds describing the
	// same physical combo always compare equal regardless of press order.
	//
	// Unlike the reference mod (ExtendedHotkeySystem), the bind is NOT a vanilla
	// ExtraHotkey slot index. The slot index lives in savegame extra-data that the
	// game itself renumbers/clears on stack-splits, equips and container moves --
	// which is exactly why that mod "resets" hotkeys. Here the chord is the key and
	// the target form (see Hotkey::form) is the single source of truth.
	struct Bind
	{
		RE::INPUT_DEVICE          device = RE::INPUT_DEVICE::kKeyboard;
		std::vector<std::uint32_t> keys;  // sorted ascending; the simultaneously-held chord

		[[nodiscard]] bool IsValid() const { return !keys.empty(); }

		[[nodiscard]] bool operator==(const Bind& a_rhs) const
		{
			return device == a_rhs.device && keys == a_rhs.keys;
		}

		// Normalize after construction / deserialization: sort + unique.
		void Canonicalize()
		{
			std::sort(keys.begin(), keys.end());
			keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
		}

		// True if every key of this chord is currently held (a_held is the live
		// pressed-set for the matching device). Used for longest-match resolution.
		[[nodiscard]] bool IsSatisfiedBy(const std::unordered_set<std::uint32_t>& a_held) const
		{
			for (auto k : keys) {
				if (!a_held.contains(k)) {
					return false;
				}
			}
			return !keys.empty();
		}
	};

	// One hotkey = one chord -> one form. The form is a base TESForm:
	//  - item-like (weapon/armor/light/potion/ingredient/ammo/scroll) -> resolved in
	//    the player inventory at fire time;
	//  - spell / shout -> equipped directly.
	// We never store an ExtraDataList pointer (it can be freed between assign and use
	// -- the reference mod's deferred-equip crash). We re-resolve everything by FormID.
	// Identity of the bound item. `form` is the base object. For items the game tracks
	// as a distinct instance (enchanted/tempered/named -> no longer stacks), it carries
	// an ExtraUniqueID; we store that (uidOwner = ExtraUniqueID.baseID, uid =
	// ExtraUniqueID.uniqueID) so two instances of the same base form don't collide.
	// Fungible items / spells / shouts have uid == 0 and match by `form` alone.
	// Item identity. `form` is the base object; the rest distinguish a specific instance
	// of that base (the game keeps enchanted/tempered/unique copies as separate, non-
	// stacking entries):
	//   ench   = ExtraEnchantment.enchantment FormID (0 = none) -- load-order-resolvable
	//   uid    = ExtraUniqueID.uniqueID (0 = none) -- per-save counter
	//   health = ExtraHealth as fixed-point (temper level; 0 = none)
	// A plain/fungible item is (form, 0, 0, 0) and matches any plain copy.
	// Which hand a bind puts its form into, remembered from the moment it was assigned.
	// Not part of identity -- it is a property of the binding, not of the item.
	enum HandMask : std::uint8_t
	{
		kHandNone = 0,  // wasn't equipped when bound -> let the engine pick, as before
		kHandRight = 1 << 0,
		kHandLeft = 1 << 1,
		kHandBoth = kHandRight | kHandLeft,
	};

	struct ItemId
	{
		RE::FormID    form = 0;
		RE::FormID    ench = 0;
		std::uint16_t uid = 0;
		std::int32_t  health = 0;
		std::uint8_t  hands = kHandNone;

		[[nodiscard]] explicit operator bool() const { return form != 0; }

		// uid is deliberately NOT compared. The engine only splits an inventory row when a
		// list carries "distinguishing" extra data; ExtraUniqueID is explicitly on its
		// boring list (with Count/Hotkey/Ownership/ReferenceHandle...), so copies that
		// differ only by uid share ONE row -- see ExtraDataList::NotWorn (REL::ID 11452)
		// and InventoryChanges::item_at (15866). Such a row carries several ExtraDataLists
		// at once, so "the" uid read off it is whichever came last and changes as copies
		// are picked up or used. Matching on it made bindings on looted gear (looted items
		// carry ExtraUniqueID; crafted ones usually don't) silently stop resolving.
		// Enchantment/temper are safe: they always force their own single-list row.
		// `hands` is likewise not compared: re-binding the same item to the same chord has
		// to keep reading as "the same hotkey" (that is what toggles it off) even when the
		// player happens to be holding it differently than they were last time.
		[[nodiscard]] bool Same(const ItemId& a_rhs) const
		{
			return form == a_rhs.form && ench == a_rhs.ench && health == a_rhs.health;
		}
	};

	// One chord -> one or more items.
	//
	// A plain hotkey holds exactly one item and toggles it: press to equip, press again to
	// put away. Two or more items make it a GROUP, which toggles the same way but on the
	// whole set -- one press puts the loadout on, and a press with all of it already on
	// takes it off. Consumables in a group are used, not worn, so they never keep the set
	// from reading as fully equipped and are left alone when it is stripped.
	//
	// Order matters. Hand items are handed out in the order they were added: the first
	// weapon/spell takes the right hand, the second the left. That is also the order the
	// player built the group in, so it is predictable without any extra UI.
	struct Hotkey
	{
		Bind                bind;
		std::vector<ItemId> items;

		[[nodiscard]] bool IsGroup() const { return items.size() > 1; }

		[[nodiscard]] bool Has(const ItemId& a_id) const
		{
			return std::any_of(items.begin(), items.end(),
				[&](const ItemId& i) { return i.Same(a_id); });
		}

		[[nodiscard]] bool HasForm(RE::FormID a_form) const
		{
			return std::any_of(items.begin(), items.end(),
				[&](const ItemId& i) { return i.form == a_form; });
		}
	};

	// Read the instance identity from one inventory row's entry data. Reads ench/uid/
	// health from the row's extra-data (a plain item leaves them all 0).
	[[nodiscard]] inline ItemId ReadIdentity(RE::InventoryEntryData* a_entry)
	{
		ItemId id;
		if (!a_entry) {
			return id;
		}
		id.form = a_entry->object ? a_entry->object->GetFormID() : 0;
		if (!a_entry->extraLists) {
			return id;
		}
		for (auto* xl : *a_entry->extraLists) {
			if (!xl) {
				continue;
			}
			if (auto* e = xl->GetByType<RE::ExtraEnchantment>(); e && e->enchantment) {
				id.ench = e->enchantment->GetFormID();
			}
			// uid is left at 0 on purpose -- see ItemId::Same. A merged "plain copies" row
			// holds several lists, so reading a uid here would pick an arbitrary one.
			if (auto* h = xl->GetByType<RE::ExtraHealth>()) {
				id.health = static_cast<std::int32_t>(std::lround(h->health * 100.0f));
			}
		}
		return id;
	}
}
