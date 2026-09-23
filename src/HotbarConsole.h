// MMOHotbar Papyrus hooks: MMOHotbar.FireSlot(int) -> bool,
// MMOHotbar.GetPreset() -> int (1 or 2),
// MMOHotbar.DumpSlots() -> void (logs the 24-slot table).
// Part of MMOHotbar (GPL-3.0-only).
#pragma once

namespace MMO
{
class HotbarConsole
{
public:
    static void Register();
};
}  // namespace MMO
