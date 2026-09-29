#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace RE
{
	class HUDMenu;
}

namespace MMO
{
	// Draws the 12-slot hotbar into the game's HUD.
	//
	// HotbarHUD is only the DATA model (which chord sits in which slot of the live
	// preset bank). Nothing in the plugin was loading dist/Interface/MMOHotbar/Hotbar.swf,
	// so the bar existed in the co-save and the Papyrus API but was never on screen.
	// This is the part that puts it there.
	//
	// The movie is our own build (tools/build_hud_swf.py): for each of the 12 slots it
	// defines three named display objects, addressed here by name --
	//     frame<i>  DefineShape2   the slot art
	//     icon<i>   DefineSprite   an empty clip we loadMovie() the PLAYER'S OWN icon
	//                              sheet into, then drive with gotoAndStop(<label>)
	//     key<i>    DefineEditText the keycap label, which we write with SetText
	// Nothing in it belongs to another mod; no SkyUI (or other) art is embedded. The
	// icons come from a file already on the player's disk, loaded at run time, so
	// nothing of theirs is redistributed either. See kIconSheets in the .cpp.
	//
	// Loading is the standard Scaleform dance every HUD mod uses: create an empty
	// MovieClip on the HUD movie's _root at the next free depth, then loadMovie() into
	// it. Those are four calls into documented GFxValue methods, not copied code -- the
	// same sequence appears independently in ahzaab/moreHUDSE (GPL-3.0) and in the
	// unlicensed HUDExtension. What is ours is doing it through CommonLibSSE-NG's
	// relocation-resolved API (HUDMenu, GFxValue, BSScaleformManager) instead of a
	// hand-patched SE offset, which is what holds on SE and on every AE.
	//
	// Attached from HUDMenu::AdvanceMovie rather than PostCreate on purpose: the HUD menu
	// is built during game start-up, which can be BEFORE the plugin reaches kDataLoaded.
	// A PostCreate hook would then never fire for the live menu and the bar would never
	// appear. Advancing the movie happens on every HUD frame, so the first one we see
	// attaches, whichever order the two happen to occur in.
	class HotbarHUDView
	{
	public:
		// kDataLoaded: hook the HUD menu and let the first advancing frame attach.
		static void Install();

		// Re-read the bank on the next frame (after a preset flip, an assignment, or a
		// load). Optional -- the labels are diffed every frame anyway, so this only
		// shortens the window in which a stale label is on screen.
		static void MarkDirty();

		// Forget the attached clip WITHOUT touching the old movie. Call it whenever the game
		// is about to rebuild the HUD (pre-load, new game): the GFxValue must be released
		// while its movie is still alive, and the next HUD frame then attaches afresh. Also
		// clears a sticky failure so a transient error can retry.
		static void Detach();

		// Same bookkeeping reset, but the clip is NOT released -- for the paths where the
		// movie that owned it is already gone (the HUD menu is opening, or the movie was
		// rebuilt without telling us). Releasing there is not a leak-free shortcut: a
		// GFxValue holding a display object releases through the movie's own object
		// interface, so dropping it late calls back into freed movie state. The object
		// itself is small and this runs at most once per HUD rebuild.
		static void Forget();

		// Re-read the menu state on the next HUD frame instead of waiting out the poll
		// interval. Call it when a menu opens or closes, so the bar never lingers over a
		// menu (or stays hidden for a frame after one closes).
		static void RefreshVisibility();

	private:
		static void AdvanceHud(RE::HUDMenu* a_this, float a_interval, std::uint32_t a_currentTime);

		static bool Attach(RE::HUDMenu* a_menu);
		static void Update();

		// The HUD menu's own AdvanceMovie, kept so the hook can call through to it.
		// Assigning a vfunc returns the previous one, which is the only supported way
		// to reach the original once the slot is ours.
		static inline REL::Relocation<decltype(&AdvanceHud)> _AdvanceHud;
	};
} // namespace MMO