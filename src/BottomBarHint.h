#pragma once

namespace RE
{
	class IMenu;
}

namespace HKS::BottomBarHint
{
	// Adds "Assign hotkey" / "Add to group", each with its modifier's keycap, to the button
	// hints a menu draws along its bottom edge -- so the feature is discoverable instead of
	// something you have to read the mod page to find out about.
	//
	// The menus build that row in ActionScript: on every selection change they clear a
	// ButtonPanel and re-add one button per available action. We wrap the method that does
	// it on the menu object and append our entries after the original has run, exactly the
	// way the keycap rendering wraps formatName. Panels pre-allocate a fixed number of
	// buttons and quietly refuse extras once full, so on a crowded row our hints are the
	// ones that drop -- which is the right way round.
	//
	// Both entry points degrade to nothing when the menu has no such method: a UI that does
	// not descend from SkyUI, or the vanilla Favorites menu, which draws no hints at all.
	// Nothing here is required for the hotkeys themselves to work.

	// SkyUI-shaped item menus: _root.Menu_mc.updateBottomBar(bSelected) -> navPanel.
	void SetupItemMenu(RE::IMenu* a_menu);

	// Favorites menus that ship their own hint rows (Untarnished UI and relatives):
	// _root.MenuHolder.Menu_mc.updateNavButtons() -> navPanel.row1.
	void SetupFavorites(RE::IMenu* a_menu);
}
