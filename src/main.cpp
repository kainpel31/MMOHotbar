#include <Windows.h>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

using namespace RE;
#include <SimpleIni.hpp>

#include "EquipDispatch.h"
#include "FavoritesHook.h"
#include "HotbarConsole.h"
#include "HotbarHUD.h"
#include "HotbarHUDView.h"
#include "InputHandler.h"
#include "InventoryIcons.h"
#include "Localization.h"
#include "MenuInputBlock.h"
#include "ModifierConflict.h"
#include "PickupWatch.h"
#include "Serialization.h"
#include "Settings.h"
#include "VanillaMigration.h"

namespace
{
	// Is the original STB Hotkey System present? Two independent answers, because neither
	// one alone is enough:
	//  - the module check sees any copy already loaded into the process, whatever path it
	//    came from and whatever order SKSE happened to load the two plugins in;
	//  - the file check sees a copy that is installed but not loaded yet, which is the
	//    ordinary case when both DLLs sit in the same folder.
	// The path is built from the game executable rather than the current directory:
	// skse_loader.exe does not promise to start the game with its folder as the working
	// directory, and a guard that silently passes is worse than no guard at all.
	[[nodiscard]] bool StbHotkeySystemPresent()
	{
		if (GetModuleHandleW(L"STB_HotkeySystem.dll")) {
			return true;
		}

		std::wstring exePath(32768, L'\0');
		const DWORD length =
			GetModuleFileNameW(nullptr, exePath.data(), static_cast<DWORD>(exePath.size()));
		if (length == 0 || static_cast<std::size_t>(length) >= exePath.size()) {
			// Failed or truncated: fall back to the relative form, correct for every
			// ordinary launch.
			return std::filesystem::exists("Data/SKSE/Plugins/STB_HotkeySystem.dll");
		}
		exePath.resize(length);

		return std::filesystem::exists(
			std::filesystem::path(exePath).parent_path() / L"Data/SKSE/Plugins/STB_HotkeySystem.dll");
	}
} // namespace

static void SKSEMessageHandler(SKSE::MessagingInterface::Message* message)
{
	if (!message) {
		return;
	}

	switch (message->type) {
	case SKSE::MessagingInterface::kDataLoaded:
		{
			HKS::Settings::Load();
			HKS::Localization::Load();
			HKS::FavoritesHook::Install();
			HKS::InventoryIcons::LoadResources();
			HKS::InventoryIcons::Install();
			HKS::MenuInputBlock::Install();
			HKS::InputHandler::Register();
			HKS::PickupWatch::Register();
			MMO::HotbarHUD::Register();
			MMO::HotbarHUDView::Install();
			// The Papyrus API is registered in SKSEPlugin_Load (SKSE's expected place).
		}
		break;

	// The HUD movie is torn down and rebuilt around a load. Drop our clip now, while the
	// old movie is still alive; the next HUD frame attaches to the new one. (A rebuild we
	// were not told about is covered by the HUD menu events in InputHandler, which do not
	// depend on the movie's address.)
	case SKSE::MessagingInterface::kPreLoadGame:
		MMO::HotbarHUDView::Detach();
		break;

	// A message box only displays once the player is in-game, so run the
	// modifier/Favorites-key conflict check on load (prompts at most once/session).
	case SKSE::MessagingInterface::kPostLoadGame:
	case SKSE::MessagingInterface::kNewGame:
		// A new game tears the HUD down and rebuilds it the same way a load does.
		if (message->type == SKSE::MessagingInterface::kNewGame) {
			MMO::HotbarHUDView::Detach();
		}
		// Co-save binds are already loaded here, so ours win over any stale vanilla slot.
		HKS::VanillaMigration::Migrate();
		HKS::ModifierConflict::CheckAndPrompt();
		break;

	default:
		break;
	}
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Query(const SKSE::QueryInterface* a_skse, SKSE::PluginInfo* a_info)
{
	a_info->infoVersion = SKSE::PluginInfo::kVersion;
	a_info->name = "MMOHotbar";
	a_info->version = 2;

	if (a_skse->IsEditor()) {
		logger::critical("Loaded in editor, marking as incompatible"sv);
		return false;
	}

	return true;
}

extern "C" DLLEXPORT constinit auto SKSEPlugin_Version = []() {
	SKSE::PluginVersionData v;

	v.PluginVersion(2);
	v.PluginName("MMOHotbar");
	v.AuthorName("MMOHotbar (fork of STB Hotkey System by STB)");

	// Every game address goes through the Address Library (REL::RelocationID + the
	// VTABLE_/Offset:: constants), so we are not tied to one runtime build.
	v.UsesAddressLibrary(true);

	// Struct layouts changed at 1.6.629; CommonLibSSE-NG resolves the per-runtime layout
	// for us, so declare the modern one. (Drops support for AE older than 1.6.629, which
	// nobody runs; SE 1.5.97 is unaffected -- it loads through SKSEPlugin_Query above.)
	v.UsesStructsPost629(true);

	// CompatibleVersions is deliberately NOT set: a non-empty list is a strict whitelist.
	// It used to hold RUNTIME_SSE_LATEST, which this CommonLib defines as 1.6.678, so AE
	// 1.7.x refused to load the plugin outright. Empty + UsesAddressLibrary = any runtime
	// the address library covers.
	//
	// HasNoStructUse is likewise NOT set: we read game structs everywhere
	// (InventoryEntryData, ExtraDataList, the menus), so claiming otherwise would be a lie.

	return v;
}();

void InitializeLog()
{
	auto path = logger::log_directory();
	if (!path) {
		stl::report_and_fail("Failed to find standard logging directory"sv);
	}

	*path /= fmt::format(FMT_STRING("{}.log"), Version::PROJECT);
	auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);

	auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));

	log->set_level(spdlog::level::info);
	log->flush_on(spdlog::level::info);

	spdlog::set_default_logger(std::move(log));
	spdlog::set_pattern("[%l] %v"s);
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	InitializeLog();

	// Everything below -- the co-save callbacks, the Papyrus API, the messaging listener
	// -- runs on this thread, and the plugin API can be reached from other threads later.
	HKS::EquipDispatch::CaptureMainThread();

	// MMOHotbar is a full fork of STB Hotkey System: same co-save owner id ('HKSY'), same
	// vtable hooks, same exported plugin API. Running both makes every key fire twice and
	// the two co-save records overwrite each other, so refuse to load and say why.
	//
	// Checked here rather than at kPostLoad on purpose. Serialization::Register() below
	// claims the shared unique id, so a later guard would let both DLLs register under
	// 'HKSY' and the records would still clobber each other -- the exact damage this is
	// here to prevent.
	if (StbHotkeySystemPresent()) {
		logger::critical("STB_HotkeySystem.dll is installed. MMOHotbar already contains it "
		                 "(it is a full fork) -- remove the original STB Hotkey System. "
		                 "MMOHotbar will not load.");
		return false;
	}

	HKS::Serialization::Register();
	MMO::HotbarConsole::Register();

	auto* messaging = SKSE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener("SKSE", SKSEMessageHandler)) {
		logger::critical("could not register the SKSE messaging listener");
		return false;
	}

	return true;
}
