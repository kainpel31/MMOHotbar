#pragma once

#include "Hotkey.h"

#include <unordered_set>
#include <vector>

namespace HKS
{
	// Global input sink. Tracks the live pressed-set per device so we can detect
	// multi-key chords (e.g. Alt+V), and fires the matching hotkey on the key-press
	// that *completes* the most specific satisfied chord.
	//
	// This deliberately does NOT hook FavoritesHandler's byte-patched IsHotkey path
	// (the reference mod's fragile `safe_write` NOP patch -- pure CTD on the wrong
	// runtime). A standard BSInputDeviceManager sink works on every runtime via NG.
	class InputHandler :
		public RE::BSTEventSink<RE::InputEvent*>,
		public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		static InputHandler* GetSingleton();
		static void          Register();

		RE::BSEventNotifyControl ProcessEvent(
			RE::InputEvent* const*              a_event,
			RE::BSTEventSource<RE::InputEvent*>* a_source) override;

		// Wipe the live pressed-set on every menu open/close. A missed key-up (alt-tab,
		// or an earlier input sink consuming the up before us) can leave a key -- notably
		// the assign modifier -- stuck "held". A stuck modifier makes the Favorites menu
		// force+swallow all keyboard input (see FavoritesHook) and become uncloseable.
		// Menu boundaries are a safe, frequent point to resync to a clean state.
		RE::BSEventNotifyControl ProcessEvent(
			const RE::MenuOpenCloseEvent*              a_event,
			RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_source) override;

		// Live pressed-state query. The sink sees every key (incl. in menus), so this
		// is a reliable modifier poll without touching BSInputDevice (whose IsPressed
		// drags in unresolved NG symbols).
		[[nodiscard]] bool IsHeld(RE::INPUT_DEVICE a_device, std::uint32_t a_key) const;

		// What "a_key pressed right now" would fire: the members of the chord it completes,
		// given whatever else is currently held. Empty if the key is bound to nothing.
		// This is what the plugin API hands to other mods -- copies, resolved under the
		// store's lock, safe to read from the input thread.
		[[nodiscard]] std::vector<ItemId> ResolveForKey(RE::INPUT_DEVICE a_device, std::uint32_t a_key) const;

		// True when a hotkey must NOT fire (paused, an assign/item/dialogue/etc. menu
		// owns input). Built from menu state (not just the pause/movement flags) so it
		// still holds under SkyrimSoulsRE, which un-pauses menus and keeps input flowing.
		[[nodiscard]] static bool FiringSuppressed();

	private:
		InputHandler() = default;
		InputHandler(const InputHandler&) = delete;
		InputHandler& operator=(const InputHandler&) = delete;

		std::unordered_set<std::uint32_t>& HeldFor(RE::INPUT_DEVICE a_device);

		// Commit the current pending chord to its locked target (assign + auto-favorite),
		// then reset the pending-chord state. The capture *session* stays open -- it lives
		// as long as the modifier is held -- so several items can be bound in one hold.
		// Called when a chord hits its key limit and when its keys are released.
		void CommitCapture();

		// Store one binding (assign + auto-favorite). Separate from CommitCapture because
		// the key-conflict prompt is asynchronous: on a clash this is deferred into the
		// message box callback and only runs if the player confirms. a_group picks which
		// modifier's meaning applies: replace the chord, or stack onto it.
		void ApplyAssignment(Bind a_bind, ItemId a_target, bool a_group);

		std::unordered_set<std::uint32_t> _kbHeld;
		std::unordered_set<std::uint32_t> _msHeld;
		std::unordered_set<std::uint32_t> _padHeld;

		// Assignment capture (Favorites menu). While the assign-modifier is held we run a
		// capture session: each chord (up to kMaxChord simultaneously-held keys) binds one
		// item and is committed on its own -- when it hits the key limit, or when its keys
		// are released -- so you can rattle off several binds without releasing the
		// modifier between them. The target is locked at each chord's first key-down.
		static constexpr std::size_t      kMaxChord = 2;
		bool                              _capturing = false;
		// Which modifier opened the session: the group one stacks the item onto whatever
		// the chord already holds instead of replacing it. Fixed for the whole session, so
		// swapping modifiers mid-hold can't half-apply.
		bool                              _capGroup = false;
		// The modifier that opened the session. Only ITS release ends the session, so
		// pressing the other modifier mid-hold can't cut the current one short.
		std::uint32_t                     _capModKey = 0;
		ItemId                            _capTarget;  // locked at the pending chord's first key
		RE::INPUT_DEVICE                  _capDevice = RE::INPUT_DEVICE::kKeyboard;
		std::unordered_set<std::uint32_t> _capChord;  // keys accumulated for the pending bind
		std::unordered_set<std::uint32_t> _capDown;   // subset of _capChord still physically held
	};
}
