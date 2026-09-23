#pragma once

#include "Hotkey.h"

#include <vector>

namespace HKS::EquipDispatch
{
	// Act on the items bound to one hotkey. Safe to call from the input thread: the work
	// is re-resolved by FormID and run on the main thread via the SKSE task queue. We never
	// carry a raw ExtraDataList pointer across the queue (that pointer can be freed when
	// stacks are re-split -- the reference mod's deferred-equip use-after-free crash).
	//
	// One item keeps the vanilla-favourites feel: press to equip, press again to put away.
	// Several items are a group, which toggles as a set: hand items are dealt out in order
	// (first to the right hand, second to the left), and once the whole set is on, the next
	// press takes it off.
	void Fire(std::vector<ItemId> a_items);

	// Which hand(s) the player is holding this form in right now, as a HandMask. Read at
	// assignment time and stored on the binding, so the hotkey can put the form back where
	// it was instead of leaving the choice to the engine. kHandNone for anything that is
	// not in a hand -- armour, ammo, and anything not currently equipped.
	[[nodiscard]] std::uint8_t CurrentHands(RE::TESForm* a_form);

	// True for forms that go in the Voice slot (shouts and powers/lesser-powers).
	[[nodiscard]] bool IsVoiceForm(RE::TESForm* a_form);

	// Equip synchronously, on the calling thread, which must be the main one -- an input
	// handler qualifies. Returns false when nothing was equipped: the binding was dropped
	// because the player un-favorited it, or they no longer hold the form. Exposed through
	// the plugin API for mods that must act on the same press, such as one that starts
	// charging a shout and needs it already in the voice slot.
	//
	// A successful call also CLAIMS the binding for a moment (see IsClaimed): the caller
	// has handled this press, and our own input sink -- which runs after the player-input
	// sinks in the same dispatch -- must not equip it a second time on top.
	[[nodiscard]] bool EquipNow(const ItemId& a_id);

	// True while a_id is still claimed by a recent EquipNow. The window is deliberately
	// short: it only has to cover the rest of the keypress that claimed it, and a stale
	// claim would otherwise swallow a genuine second press.
	[[nodiscard]] bool IsClaimed(const ItemId& a_id);
}
