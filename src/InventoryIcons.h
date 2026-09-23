#pragma once

namespace HKS
{
	// Renders hotkey keycap(s) on bound entries in the Inventory/Container/Magic/Gift/
	// Barter menus, without editing any SkyUI SWF:
	//   * PostCreate (IMenu vfunc 0x2): inject the keycap symbol (STBKeycap) at runtime
	//     and wrap InventoryListEntry.prototype.formatName so it draws the keycaps;
	//   * AdvanceMovie (IMenu vfunc 0x5): diff-stamp each entry's chord scancodes by
	//     FormID and re-render when they change (covers initial open, live re-assign and
	//     the magic menu uniformly).
	class InventoryIcons
	{
	public:
		static void Install();        // kDataLoaded
		static void LoadResources();  // kDataLoaded: prepare the keycap import request
		static void MarkDirty();      // force a re-stamp (call after an assignment)
	};
}
