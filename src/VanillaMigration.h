#pragma once

namespace HKS
{
	// Save-compatibility: pull pre-existing vanilla favorites hotkeys out of a loaded
	// save and adopt them into our system. Vanilla stores hotkeys as:
	//   - items: an ExtraHotkey (slot 0..7) on the item's ExtraDataList,
	//   - magic: MagicFavorites::hotkeys[slot] -> form.
	// We map slot i -> number key (i+1), Assign it as a single-key bind, then clear the
	// vanilla slot so the key is fully owned by us (vanilla 1-8 firing is already killed).
	namespace VanillaMigration
	{
		// Run after the co-save load (so our own binds win over a stale vanilla slot for
		// the same item). Idempotent: it clears what it migrates, so re-runs find nothing.
		void Migrate();
	}
}
