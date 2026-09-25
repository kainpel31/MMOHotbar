// This file is part of MMOHotbar (GPL-3.0-only).
#include "HotbarConsole.h"

#include "HotbarHUD.h"

namespace MMO
{
	namespace
	{
		bool FireSlot(RE::StaticFunctionTag*, std::int32_t a_slot)
		{
			constexpr auto maxSlot = static_cast<std::int32_t>(HotbarHUD::kSlotCount);
			if (a_slot < 1 || a_slot > maxSlot) {
				logger::warn("MMOHotbar.FireSlot: slot {} out of range (1-{})", a_slot, maxSlot);
				return false;
			}
			return HotbarHUD::ExecuteSlot(static_cast<std::uint32_t>(a_slot - 1));
		}

		std::int32_t GetPreset(RE::StaticFunctionTag*)
		{
			return static_cast<std::int32_t>(HotbarHUD::ActivePreset());
		}

		bool FireSlotSwap(RE::StaticFunctionTag*, std::int32_t a_slot)
		{
			// Deprecated compatibility alias. Hand swapping was removed; old scripts still
			// receive normal slot behaviour rather than losing their fire call entirely.
			return FireSlot(nullptr, a_slot);
		}

		void DumpSlots(RE::StaticFunctionTag*)
		{
			logger::info("[MMO] active preset: {} (tap X to flip)", HotbarHUD::ActivePreset());
			for (const auto& slot : HotbarHUD::Snapshot()) {
				logger::info("[MMO] slot {:2} [{}] {}", slot.slot + 1, slot.label, slot.names);
			}
			if (auto* console = RE::ConsoleLog::GetSingleton()) {
				console->Print("[MMO] 12 slots dumped to MMOHotbar.log");
			}
		}
	}

	void HotbarConsole::Register()
	{
		if (auto* papyrus = SKSE::GetPapyrusInterface()) {
			papyrus->Register([](RE::BSScript::IVirtualMachine* a_vm) {
				a_vm->RegisterFunction("FireSlot", "MMOHotbar", FireSlot);
				a_vm->RegisterFunction("FireSlotSwap", "MMOHotbar", FireSlotSwap);
				a_vm->RegisterFunction("GetPreset", "MMOHotbar", GetPreset);
				a_vm->RegisterFunction("DumpSlots", "MMOHotbar", DumpSlots);
				return true;
			});
			logger::info("MMO hotbar Papyrus API registered "
				"(MMOHotbar.FireSlot/FireSlotSwap/GetPreset/DumpSlots)");
		}
	}
}
