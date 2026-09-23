#pragma once

namespace HKS
{
	// Watches items entering the player's inventory and, if a hotkey is still bound to
	// that base form but the item is no longer favorited, re-favorites it.
	//
	// Why this exists: the chord -> form binding lives in HotkeyManager (and the co-save)
	// and the inventory keycap is drawn straight from it, so a bind -- and its icon --
	// survive the item running out. Favorite state, however, is game-owned: consuming the
	// last potion drops its inventory entry and with it the favorite. Picking up a fresh
	// copy does NOT re-favorite it, so vanilla favorites won't use it and EquipDispatch::
	// Fire (which treats "not favorited" as "player un-favorited -> drop the bind") would
	// delete the still-valid binding on the next press.
	//
	// A deliberate vanilla-F un-favorite emits no container-changed event, so this only
	// fires on a genuine re-pickup -- the F-to-remove flow is left intact.
	class PickupWatch
	{
	public:
		static void Register();
	};
}
