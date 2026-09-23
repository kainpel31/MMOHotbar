// This file is part of MMOHotbar (GPL-3.0-only).
#include "HotbarConsole.h"

#include "HotbarHUD.h"

namespace MMO
{
namespace
{
bool FireSlot(RE::StaticFunctionTag*, std::int32_t a_slot)
{
    if (a_slot < 1 || a_slot > 24) {
        logger::warn("MMOHotbar.FireSlot: slot {} out of range (1-24)", a_slot);
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
    if (a_slot < 1 || a_slot > 24) {
        logger::warn("MMOHotbar.FireSlotSwap: slot {} out of range (1-24)", a_slot);
        return false;
    }
    return HotbarHUD::ExecuteSlot(
        static_cast<std::uint32_t>(a_slot - 1), HKS::EquipDispatch::FireMode::kSwapHands);
}

void DumpSlots(RE::StaticFunctionTag*)
{
    logger::info("[MMO] active preset: {} (tap X to flip)", HotbarHUD::ActivePreset());
    for (const auto& s : HotbarHUD::Snapshot()) {
        logger::info("[MMO] slot {:2} [{}] {}", s.slot + 1, s.label, s.names);
    }
    if (auto* con = RE::ConsoleLog::GetSingleton()) {
        con->Print("[MMO] 24 slots dumped to MMOHotbar.log");
    }
}
}  // namespace

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
        logger::info("MMO hotbar Papyrus API registered (MMOHotbar.FireSlot/FireSlotSwap/GetPreset/DumpSlots)");
    }
}
}  // namespace MMO
