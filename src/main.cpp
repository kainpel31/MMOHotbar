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
#include <xbyak/xbyak.h>

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

static void SKSEMessageHandler(SKSE::MessagingInterface::Message* message)
{
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
			MMO::HotbarConsole::Register();
		}
		break;

	// A message box only displays once the player is in-game, so run the
	// modifier/Favorites-key conflict check on load (prompts at most once/session).
	case SKSE::MessagingInterface::kPostLoadGame:
	case SKSE::MessagingInterface::kNewGame:
		// Co-save binds are already loaded here, so ours win over any stale vanilla slot.
		HKS::VanillaMigration::Migrate();
		HKS::ModifierConflict::CheckAndPrompt();
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
	InitializeLog();
	SKSE::Init(a_skse);

	HKS::Serialization::Register();

	auto messaging = SKSE::GetMessagingInterface();
	if (!messaging->RegisterListener("SKSE", SKSEMessageHandler)) {
		return false;
	}

	return true;
}
