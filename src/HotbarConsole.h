// MMOHotbar Papyrus hooks: MMOHotbar.FireSlot(int) -> bool,
// MMOHotbar.GetPreset() -> int (1 or 2),
// MMOHotbar.DumpSlots() -> void (logs the active 12-slot bank).
// Part of MMOHotbar (GPL-3.0-only).
#pragma once

namespace MMO
{
	class HotbarConsole
	{
	public:
		static void Register();
	};
}
