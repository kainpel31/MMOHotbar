#pragma once

namespace HKS
{
	// Stops menu keys from doing their normal job while a hotkey is being assigned.
	//
	// Without this, binding Ctrl+E in the inventory also fires SkyUI's "E = equip/use",
	// Ctrl+R drops the item, Ctrl+F un-favorites it, and so on -- the chord key keeps its
	// menu meaning, so every bind on a letter the menu already uses has a side effect. The
	// Favorites menu never had the problem because it owns a MenuEventHandler we already
	// swallow input in (see FavoritesHook); Inventory/Magic/Container/Gift/Barter are plain
	// IMenus with no handler of their own, so there is nothing there to hook.
	//
	// So we filter one level up instead. MenuControls::ProcessEvent is the single funnel:
	// for every event it walks the registered MenuEventHandlers and, only if none of them
	// claimed it, forwards it to the Scaleform side (REL::ID 51370). Removing an event from
	// the chain before the original runs therefore hides it from handlers AND from the
	// movie, while our own input sink -- a separate BSInputDeviceManager sink -- still sees
	// the untouched chain and can capture the chord.
	//
	// Registering our own MenuEventHandler would have been tidier but is not reliable: the
	// handler loop (REL::ID 51377) does NOT stop at the first handler that claims an event,
	// it lets every handler overwrite the "handled" flag, so the last registered handler
	// wins -- and menus register theirs as they open, i.e. always after us.
	class MenuInputBlock
	{
	public:
		static void Install();

		// True while menu keys are being withheld: an assign menu is open, an assign
		// modifier is held, and nothing is overlaying the menu.
		[[nodiscard]] static bool Blocking();

	private:
		static RE::BSEventNotifyControl ProcessEvent(
			RE::MenuControls*                    a_this,
			RE::InputEvent* const*               a_event,
			RE::BSTEventSource<RE::InputEvent*>* a_source);

		static inline REL::Relocation<decltype(&MenuInputBlock::ProcessEvent)> _ProcessEvent;
	};
}
