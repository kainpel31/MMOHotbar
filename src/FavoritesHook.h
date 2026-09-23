#pragma once

#include <vector>

namespace HKS
{
	// Bridges our hotkey store with the (SkyUI/Untarnished) Favorites menu:
	//  - AdvanceMovie       : push per-entry `hotkeyLabel` so the SWF draws the badge;
	//  - Menu ProcessButton : capture the assign chord AND block vanilla assignment;
	//  - Handler ProcessButton: disable vanilla 1-8 firing (our input sink owns hotkeys),
	//                           while preserving the "open favorites" key.
	//
	// Everything is vtable-swap / GFx only -- works on SE/AE alike via CommonLibSSE-NG.
	class FavoritesHook
	{
	public:
		static void Install();

	private:
		static void AdvanceMovie(RE::FavoritesMenu* a_this, float a_interval, std::uint32_t a_currentTime);
		static bool MenuCanProcess(RE::MenuEventHandler* a_this, RE::InputEvent* a_event);
		static bool MenuProcessButton(RE::MenuEventHandler* a_this, RE::ButtonEvent* a_event);
		static bool HandlerProcessButton(RE::FavoritesHandler* a_this, RE::ButtonEvent* a_event);

		static inline REL::Relocation<decltype(&AdvanceMovie)>          _AdvanceMovie;
		static inline REL::Relocation<decltype(&MenuCanProcess)>        _MenuCanProcess;
		static inline REL::Relocation<decltype(&MenuProcessButton)>     _MenuProcessButton;
		static inline REL::Relocation<decltype(&HandlerProcessButton)>  _HandlerProcessButton;
	};
}
