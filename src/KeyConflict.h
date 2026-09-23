#pragma once

#include <cstdint>
#include <string>

namespace HKS::KeyConflict
{
	// Readable name for a keyboard DX scancode ("Q", "Left Ctrl", "F5").
	[[nodiscard]] std::string KeyName(std::uint32_t a_code);

	// The vanilla GAMEPLAY control this key is bound to, or "" when it is free.
	//
	// Only the gameplay context is consulted: our hotkeys never fire while a menu owns
	// input (see InputHandler::FiringSuppressed), so a menu-context binding is not a
	// functional clash.
	//
	// Hotkey1..Hotkey8 are deliberately NOT reported. Those are the vanilla favorites
	// slots on the number row, and this mod replaces that path outright (FavoritesHook
	// kills it), so binding there is the intended use rather than a conflict.
	[[nodiscard]] std::string GameplayControl(std::uint32_t a_code);
}
