#include "MenuAssign.h"

#include "KeyConflict.h"
#include "Settings.h"

#include <algorithm>
#include <initializer_list>
#include <mutex>
#include <string>
#include <vector>

namespace HKS::MenuAssign
{
	namespace
	{
		struct Target
		{
			const char* menu;
			const char* selectedEntry;  // GFx path to the highlighted entry object
		};

		// Favorites has its own root; Inventory/Container/Magic/Gift/Barter all use
		// SkyUI's shared InventoryLists layout, so one path covers them.
		constexpr Target kTargets[] = {
			{ "FavoritesMenu", "_root.MenuHolder.Menu_mc.itemList.selectedEntry" },
			{ "InventoryMenu", "_root.Menu_mc.inventoryLists.panelContainer.itemList.selectedEntry" },
			{ "ContainerMenu", "_root.Menu_mc.inventoryLists.panelContainer.itemList.selectedEntry" },
			{ "MagicMenu", "_root.Menu_mc.inventoryLists.panelContainer.itemList.selectedEntry" },
			{ "GiftMenu", "_root.Menu_mc.inventoryLists.panelContainer.itemList.selectedEntry" },
			{ "BarterMenu", "_root.Menu_mc.inventoryLists.panelContainer.itemList.selectedEntry" },
		};

		// skyui.defines.Inventory.ICT_ACTIVE_EFFECT. SkyUI's Active Effects tab reuses the
		// very same itemList as spells/items, and its rows carry the SOURCE SPELL's formId
		// -- without this check a chord pressed there binds that spell by accident.
		constexpr std::uint32_t kActiveEffectType = 11;

		std::uint32_t ReadNum(RE::GFxValue& a_entry, const char* a_member)
		{
			RE::GFxValue v;
			return (a_entry.GetMember(a_member, &v) && v.IsNumber()) ? static_cast<std::uint32_t>(v.GetNumber()) : 0;
		}
	}

	bool IsAssignMenuOpen()
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			return false;
		}
		for (const auto& t : kTargets) {
			if (ui->IsMenuOpen(t.menu)) {
				return true;
			}
		}
		return false;
	}

	ItemId GetSelectedAssignTarget()
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			return {};
		}
		for (const auto& t : kTargets) {
			auto menu = ui->GetMenu(t.menu);
			if (!menu || !menu->uiMovie) {
				continue;
			}
			RE::GFxValue entry;
			if (!menu->uiMovie->GetVariable(&entry, t.selectedEntry) || !entry.IsObject()) {
				continue;
			}
			const auto fid = static_cast<RE::FormID>(ReadNum(entry, "formId"));
			if (!fid) {
				continue;
			}
			if (ReadNum(entry, "type") == kActiveEffectType) {
				continue;  // an active effect is not something you can equip
			}
			ItemId id;
			id.form = fid;
			id.ench = static_cast<RE::FormID>(ReadNum(entry, "STBench"));
			id.uid = static_cast<std::uint16_t>(ReadNum(entry, "STBuid"));
			id.health = static_cast<std::int32_t>(ReadNum(entry, "STBhealth"));
			return id;
		}
		return {};
	}

	namespace
	{
		struct MenuKeys
		{
			std::string                menu;  // the menu they were read from; empty = nothing cached
			std::vector<std::uint32_t> keys;
		};

		std::mutex g_menuKeysLock;
		MenuKeys   g_menuKeys;

		// Control keycodes a SkyUI-shaped menu keeps on its own object: the item menus'
		// equip mode, tab switch, search and sort keys, and the group keys of SkyUI-derived
		// favorites menus. Empty while none are set -- the menu loads its config a moment
		// after it opens, and an empty read must not be cached as "uses nothing".
		std::vector<std::uint32_t> ReadMenuKeys(RE::GFxMovieView* a_movie, bool a_favorites)
		{
			std::vector<std::uint32_t> keys;
			RE::GFxValue               menu;
			if (!a_movie->GetVariable(&menu, a_favorites ? "_root.MenuHolder.Menu_mc" : "_root.Menu_mc") ||
				!menu.IsObject()) {
				return keys;
			}
			const auto add = [&](RE::GFxValue& a_obj, const char* a_name) {
				RE::GFxValue v;
				if (a_obj.GetMember(a_name, &v) && v.IsNumber() && v.GetNumber() > 0.0) {
					keys.push_back(static_cast<std::uint32_t>(v.GetNumber()));
				}
			};
			for (const char* name : { "_equipModeKey", "_switchTabKey", "_searchKey", "_groupAddKey",
					 "_groupUseKey", "_setIconKey", "_saveEquipStateKey", "_toggleFocusKey" }) {
				add(menu, name);
			}
			RE::GFxValue controls;
			if (menu.GetMember("_sortOrderControls", &controls) && controls.IsObject()) {
				add(controls, "keyCode");
			}
			if (menu.GetMember("_sortColumnControls", &controls) && controls.IsArray()) {
				for (std::uint32_t i = 0; i < controls.GetArraySize(); ++i) {
					RE::GFxValue entry;
					if (controls.GetElement(i, &entry) && entry.IsObject()) {
						add(entry, "keyCode");
					}
				}
			}
			return keys;
		}

		void LogYield(const char* a_menu, const std::vector<std::uint32_t>& a_keys, std::uint32_t a_modifier,
			const char* a_what)
		{
			if (a_modifier != 0 && std::find(a_keys.begin(), a_keys.end(), a_modifier) != a_keys.end()) {
				logger::info("{}: {} ({}) is one of the menu's own controls -- not used as the {} here",
					a_menu, KeyConflict::KeyName(a_modifier), a_modifier, a_what);
			}
		}
	}

	bool MenuUsesKey(std::uint32_t a_scancode)
	{
		if (a_scancode == 0) {
			return false;
		}
		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			return false;
		}
		for (const auto& t : kTargets) {
			if (!ui->IsMenuOpen(t.menu)) {
				continue;
			}
			std::scoped_lock lk(g_menuKeysLock);
			if (g_menuKeys.menu != t.menu) {
				auto menu = ui->GetMenu(t.menu);
				if (!menu || !menu->uiMovie) {
					return false;
				}
				auto keys = ReadMenuKeys(menu->uiMovie.get(), std::string_view{ t.menu } == "FavoritesMenu");
				if (keys.empty()) {
					return false;  // config not loaded yet -- ask again on the next event
				}
				g_menuKeys.menu = t.menu;
				g_menuKeys.keys = std::move(keys);
				LogYield(t.menu, g_menuKeys.keys, Settings::AssignModifier(), "assign modifier");
				LogYield(t.menu, g_menuKeys.keys, Settings::GroupModifier(), "group modifier");
			}
			return std::find(g_menuKeys.keys.begin(), g_menuKeys.keys.end(), a_scancode) != g_menuKeys.keys.end();
		}
		return false;
	}

	std::uint32_t UsableModifier(std::uint32_t a_scancode)
	{
		return MenuUsesKey(a_scancode) ? 0 : a_scancode;
	}

	void InvalidateMenuKeys()
	{
		std::scoped_lock lk(g_menuKeysLock);
		g_menuKeys = {};
	}
}
