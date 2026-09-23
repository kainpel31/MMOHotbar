#pragma once

namespace HKS::Favorites
{
	// Mark the form favorited (so it shows the star and appears in the Favorites menu
	// with our badge), mirroring a vanilla "F" press. Uses the game's own SetFavorite
	// (MagicFavorites for spells/shouts, InventoryChanges for items) -- never hand-edits
	// ExtraDataList. Runs on the main thread via the task queue; callers should only
	// invoke it once the item menus are closed (favoriting reorganizes extraLists, which
	// would desync an open inventory's 3D preview).
	void EnsureFavorited(RE::FormID a_form);

	// Where a bound form currently stands. The three-way split matters: running OUT of a
	// consumable is NOT the same as un-favoriting it. The binding must survive the former
	// (PickupWatch restores the star when the item comes back) and only be dropped on the
	// latter -- conflating them silently deleted hotkeys when the last potion was drunk.
	enum class State
	{
		kFavorited,    // held and favorited -- binding is live
		kUnfavorited,  // held, but the player un-favorited it (vanilla F) -> drop binding
		kAbsent,       // none held (used up / dropped / sold) -> KEEP the binding
	};

	[[nodiscard]] State Query(RE::FormID a_form);

	// True if the form is still favorited (item has an ExtraHotkey, or spell/shout is in
	// MagicFavorites). Shorthand for Query() == kFavorited.
	[[nodiscard]] bool IsFavorited(RE::FormID a_form);

	// Drop every binding whose item is no longer favorited (player un-favorited it via
	// vanilla F, or it left the inventory) so its keycap doesn't linger and reappear when
	// the item returns. Returns true if anything was removed. Main thread.
	bool PruneUnfavorited();

	// Favorite the item currently selected in the open inventory/container menu, the way
	// the game does (real InventoryEntryData via ItemList::GetSelectedItem + live list
	// refresh) -- so the star shows immediately. Verifies the selection still matches
	// a_expected (the locked target). Returns false if it couldn't (e.g. magic menu, no
	// item menu open, selection moved) so the caller can fall back to EnsureFavorited.
	// Works on every runtime: it goes through CommonLibSSE-NG's ItemList API instead of a
	// hardcoded SE address. Must run on the main thread.
	bool FavoriteSelectedItem(RE::FormID a_expected);
}
