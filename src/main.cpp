#include <Windows.h>
#include <algorithm>
#include <chrono>
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

	// Every SKSE message, by name, logged the moment it arrives.
	//
	// This exists because of a reported hang on New Game / Load Game. Without it the log
	// simply stops, and "stopped between two messages" is indistinguishable from "hung
	// inside the game" -- which is the one fact that decides where to look next.
	//
	// The parameter is the raw std::uint32_t that Message::type carries, not the enum:
	// the unscoped enum has no enumerator for every message the game actually sends, so
	// an unknown value prints as "other" rather than being a value we could not name.
	[[nodiscard]] const char* MessageName(std::uint32_t a_type)
	{
		using T = SKSE::MessagingInterface;
		switch (a_type) {
		case T::kPostLoad:
			return "kPostLoad";
		case T::kPostPostLoad:
			return "kPostPostLoad";
		case T::kPreLoadGame:
			return "kPreLoadGame";
		case T::kPostLoadGame:
			return "kPostLoadGame";
		case T::kSaveGame:
			return "kSaveGame";
		case T::kDeleteGame:
			return "kDeleteGame";
		case T::kInputLoaded:
			return "kInputLoaded";
		case T::kNewGame:
			return "kNewGame";
		case T::kDataLoaded:
			return "kDataLoaded";
		default:
			return "other";
		}
	}

	// Times one startup step and brackets it in the log. Scoped on purpose: a step that
	// never returns still leaves its "-> name" line behind, so the last "->" without a
	// matching "<-" names the function that hung.
	struct StepTimer
	{
		const char*                           name;
		std::chrono::steady_clock::time_point start;

		explicit StepTimer(const char* a_name) : name(a_name), start(std::chrono::steady_clock::now())
		{
			logger::info("  -> {}", name);
		}

		~StepTimer()
		{
			const auto ms =
			    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
			                                                          start);
			logger::info("  <- {} took {} ms", name, ms.count());
		}
	};
} // namespace

// The INI file says what the settings are; this says what the plugin did with them, in
// one block, at startup. A player reporting a problem should not have to read the whole
// log to find out how the mod was configured.
constexpr auto kUsage =
    "settings: [Hotbar] iVisibleSlots/fBarScale/fBarX/fBarY place the bar, bEnableHUD=0 "
    "hides it, iPresetModifierScanCode is the X key; [Icons] iKeycapSource 0=STB 1=SkyUI, "
    "bShowItemIcons=1 draws item icons";

static void SKSEMessageHandler(SKSE::MessagingInterface::Message* message)
{
	if (!message) {
		return;
	}
	logger::info("MMO hotbar: {}", MessageName(message->type));

	switch (message->type) {
	case SKSE::MessagingInterface::kDataLoaded:
		{
			logger::info("MMO hotbar: starting up\n{}", kUsage);
			{
				StepTimer t{ "Settings::Load" };
				HKS::Settings::Load();
			}
			{
				StepTimer t{ "Localization::Load" };
				HKS::Localization::Load();
			}
			{
				StepTimer t{ "FavoritesHook::Install" };
				HKS::FavoritesHook::Install();
			}
			{
				StepTimer t{ "InventoryIcons::LoadResources" };
				HKS::InventoryIcons::LoadResources();
			}
			{
				StepTimer t{ "InventoryIcons::Install" };
				HKS::InventoryIcons::Install();
			}
			{
				StepTimer t{ "MenuInputBlock::Install" };
				HKS::MenuInputBlock::Install();
			}
			{
				StepTimer t{ "InputHandler::Register" };
				HKS::InputHandler::Register();
			}
			{
				StepTimer t{ "PickupWatch::Register" };
				HKS::PickupWatch::Register();
			}
			{
				StepTimer t{ "HotbarHUD::Register" };
				MMO::HotbarHUD::Register();
			}
			{
				StepTimer t{ "HotbarHUDView::Install" };
				MMO::HotbarHUDView::Install();
			}
			logger::info("MMO hotbar: startup complete");
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

extern "C" DLLEXPORT bool SKSEPlugin_Query(const SKSE::QueryInterface* a_skse, SKSE::PluginInfo* a_info)
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
	// Both setters lost their bool parameter in this library: the flag is implied by
	// calling them, and there is no "false" case any more.
	v.UsesAddressLibrary();

	// Struct layouts changed at 1.6.629; CommonLibSSE-NG resolves the per-runtime layout
	// for us, so declare the modern one (was UsesStructsPost629(true)). (Drops support for
	// AE older than 1.6.629, which nobody runs; SE 1.5.97 is unaffected -- it loads through
	// SKSEPlugin_Query above.)
	v.UsesUpdatedStructs();

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

extern "C" DLLEXPORT bool SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
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
