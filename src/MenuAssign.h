#pragma once

#include "Hotkey.h"

namespace HKS::MenuAssign
{
	// Menus from which a hotkey may be assigned. Favorites uses its own root path;
	// Inventory/Container/Magic/Gift/Barter all share SkyUI's InventoryLists layout.
	[[nodiscard]] bool IsAssignMenuOpen();

	// Identity of the entry currently highlighted in whichever assign-menu is open
	// (form 0 if none). Reads the instance uid (STBuid/STBowner) stamped on the entry by
	// the scaleform callback. Read at modifier-press time so SkyUI type-search on the
	// chord keys can't move the selection out from under us.
	[[nodiscard]] ItemId GetSelectedAssignTarget();

	// True when the open assign menu uses a_scancode for one of its own controls. A
	// modifier on such a key is not ours in that menu: holding it is how the player talks
	// to the menu, so capturing chords or withholding keys there would break the menu's
	// own feature. The case that surfaced it: SkyUI's container menu makes Left Shift its
	// "equip mode" key -- hold it and E eats or drinks straight from a body -- and Left
	// Shift is the default group modifier.
	//
	// Reads the control keycodes SkyUI-shaped menus keep on their own object, once per
	// menu opening; nothing is cached until the menu has loaded its config.
	[[nodiscard]] bool MenuUsesKey(std::uint32_t a_scancode);

	// a_scancode, or 0 when the open menu uses it (see MenuUsesKey).
	[[nodiscard]] std::uint32_t UsableModifier(std::uint32_t a_scancode);

	// Forget the cached menu keys. Called at every assign-menu open and close.
	void InvalidateMenuKeys();
}
