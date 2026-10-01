#pragma once

namespace HKS
{
	// Minimal INI-backed config. Read once at kDataLoaded from
	// Data/SKSE/Plugins/MMOHotbar.ini, with the upstream STB_HotkeySystem.ini read
	// underneath it as a base layer for installs that predate the rename.
	class Settings
	{
	public:
		static void Load();

		// Keyboard scancode that puts the Favorites menu into "assign" mode while
		// held. Not part of the bound chord -- it only gates assignment. Default
		// 0x1D (Left Ctrl).
		static std::uint32_t AssignModifier() { return _assignModifier; }

		// Runtime override (used by the conflict prompt after it rewrites the INI), so
		// the new modifier takes effect without a reload.
		static void SetAssignModifier(std::uint32_t a_code) { _assignModifier = a_code; }

		// Second modifier: held instead of the assign one, a keypress ADDS the selected
		// item to whatever that key already holds, building a group that equips as a set
		// (SkyUI's item groups). Pressing it again on a member removes that member.
		// 0 disables groups entirely. Default 0x2A (Left Shift).
		static std::uint32_t GroupModifier() { return _groupModifier; }

		// Turn groups off for the session because the configured key can't work (it is the
		// assign modifier, or the Favorites-menu key). Latched, so the next Settings::Load()
		// -- which runs on every menu open, to pick up live INI tweaks -- doesn't quietly
		// read the broken value back in.
		static void DisableGroups()
		{
			_groupsDisabled = true;
			_groupModifier = 0;
		}

		// While an assign modifier is held in an item menu, withhold menu keys from the
		// menu itself, so binding Ctrl+E doesn't also equip the item, Ctrl+R doesn't drop
		// it and Ctrl+F doesn't un-favorite it. The cancel control is always let through
		// so the menu stays closeable. See MenuInputBlock.
		static bool BlockMenuKeys() { return _blockMenuKeys; }

		// Draw "Assign hotkey" / "Add to group" among the button hints at the bottom of the
		// inventory and magic menus. SkyUI-shaped menus only -- see BottomBarHint.
		static bool ShowAssignHint() { return _showAssignHint; }

		// Allow 2-key chords when assigning.
		//   true  -> a single key commits on modifier RELEASE (so you can still add a 2nd
		//            key); a 2nd key commits the chord instantly on its press.
		//   false -> single-key only: the first key press commits instantly (modifier +
		//            key down = bind), no release needed.
		static bool EnableChords() { return _enableChords; }

		// On loading a save, convert any pre-existing vanilla favorites hotkeys (item
		// ExtraHotkey slots + MagicFavorites hotkeys) into our binds on number keys 1-8,
		// then clear the vanilla slot. Lets old saves keep working with our system.
		static bool MigrateVanillaHotkeys() { return _migrateVanillaHotkeys; }

		// Warn (with a confirm prompt) when the key being bound is already a vanilla
		// gameplay control, so the player doesn't silently end up firing two things at
		// once. The vanilla Hotkey1..8 slots are never counted -- replacing those is the
		// whole point of this mod.
		static bool WarnKeyConflict() { return _warnKeyConflict; }

		// Verbose diagnostics (per-pickup, per-instance, "nothing held" notes). Off by
		// default -- those fire on ordinary play and would flood the log. Assignments,
		// binding removals and warnings are always logged regardless.
		static bool DebugLog() { return _debugLog; }

		// When a hotkey is assigned, remember which hand(s) the item or spell was in at that
		// moment and put it back there on every press -- the way SkyUI's saved equip state
		// works. Held in both hands when bound means both hands on one press; held only in
		// the left means the left hand, every time, instead of the engine's "whichever hand
		// is free". A form that was not equipped at all when bound records no preference and
		// keeps the old behaviour.
		static bool RememberHand() { return _rememberHand; }

		// How item equips are applied (hidden, for chasing equip bugs):
		//   0 = the way the game's own favorites hotkeys do it: immediately, then the
		//       model update and shield step FavoritesMenu::UseQuickslotItem runs
		//   1 = immediately while weapons are sheathed, queued while drawn, no model update
		//   2 = always queued (EquipObject's default)
		static int EquipMode() { return _equipMode; }

		// Keycap rendering (tweak live: edit the INI, reopen the menu). Each list lays out
		// differently, so Show/X/Gap are per menu kind; the rest is shared.
		enum class MenuKind
		{
			kInventory,   // the player's own inventory
			kContainer,   // container / barter / gift -- someone else's list beside yours
			kMagic,
			kFavorites
		};

		static bool  IconAfterName() { return _iconAfterName; }
		static float IconScale() { return _iconScale; }
		static float IconY() { return _iconY; }

		// Which keycap art the list rows draw ([Icons] iKeycapSource):
		//   0  Interface/STB_Keycaps.swf, export STBKeycap (ours, built from a UI mod's art)
		//   1  SkyUI's own Interface/SkyUI/buttonart.swf, export ButtonArt
		// Both use the same frame convention, so this only chooses the file. Out of range
		// falls back to 0.
		static int KeycapSource() { return _keycapSource; }

		// Draw keycaps in this menu at all.
		static bool IconsEnabled(MenuKind a_kind)
		{
			switch (a_kind) {
			case MenuKind::kContainer:
				return _iconsCont;
			case MenuKind::kMagic:
				return _iconsMagic;
			case MenuKind::kFavorites:
				return _iconsFav;
			default:
				return _iconsInv;
			}
		}

		// Draw R / L after the keycap for a bind that remembers a hand.
		static bool  ShowHandLabel() { return _showHandLabel; }
		static float HandLabelSize() { return _handLabelSize; }
		static float HandLabelGap() { return _handLabelGap; }

		static float IconX(MenuKind a_kind)
		{
			switch (a_kind) {
			case MenuKind::kContainer:
				return _iconXCont;
			case MenuKind::kMagic:
				return _iconXMagic;
			case MenuKind::kFavorites:
				return _iconXFav;
			default:
				return _iconX;
			}
		}
		static float IconGap(MenuKind a_kind)
		{
			switch (a_kind) {
			case MenuKind::kContainer:
				return _iconGapCont;
			case MenuKind::kMagic:
				return _iconGapMagic;
			case MenuKind::kFavorites:
				return _iconGapFav;
			default:
				return _iconGap;
			}
		}

	private:
		static inline std::uint32_t _assignModifier = 0x1D;
		static inline std::uint32_t _groupModifier = 0x2A;
		static inline bool          _groupsDisabled = false;
		static inline bool          _blockMenuKeys = true;
		static inline bool          _showAssignHint = true;
		static inline bool          _enableChords = true;
		static inline bool          _migrateVanillaHotkeys = true;
		static inline bool          _warnKeyConflict = true;
		static inline bool          _debugLog = false;
		static inline bool          _rememberHand = true;
		static inline int           _equipMode = 0;
		static inline bool          _iconsInv = true;
		static inline bool          _iconsCont = true;
		static inline bool          _iconsMagic = true;
		static inline bool          _iconsFav = true;
		static inline bool          _showHandLabel = true;
		static inline float         _handLabelSize = 14.0f;
		static inline float         _handLabelGap = 4.0f;
		static inline bool          _iconAfterName = true;
		static inline int           _keycapSource = 0;
		static inline float         _iconScale = 75.0f;
		static inline float         _iconY = 2.0f;
		static inline float         _iconX = 40.0f;
		static inline float         _iconGap = 20.0f;
		static inline float         _iconXCont = 40.0f;
		static inline float         _iconGapCont = 20.0f;
		static inline float         _iconXMagic = 25.0f;
		static inline float         _iconGapMagic = 20.0f;
		static inline float         _iconXFav = 40.0f;
		static inline float         _iconGapFav = 20.0f;
	};
}
