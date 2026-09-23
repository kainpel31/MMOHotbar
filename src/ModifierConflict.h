#pragma once

namespace HKS
{
	// Warns the player when the assign-modifier scancode collides with the key that
	// opens the Favorites menu. Sharing one key makes both the chord-capture and the
	// menu-open fight over the same press (the menu bugs out / won't close), so we
	// offer to switch the modifier to a free key and persist the choice to the INI.
	namespace ModifierConflict
	{
		// Show the prompt if there is a conflict. Safe to call on every game load;
		// it prompts at most once per session and does nothing when keys differ.
		void CheckAndPrompt();
	}
}
