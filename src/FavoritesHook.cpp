#include "FavoritesHook.h"

#include "Favorites.h"
#include "HotkeyManager.h"
#include "InputHandler.h"
#include "MenuAssign.h"
#include "Settings.h"

#include <string>

namespace HKS
{
	namespace
	{
		constexpr const char* kItemListPath = "_root.MenuHolder.Menu_mc.itemList";

		// Either assign modifier: the plain one or the group one.
		bool AnyModifierHeld()
		{
			auto*      input = InputHandler::GetSingleton();
			const auto assignKey = MenuAssign::UsableModifier(Settings::AssignModifier());
			const auto groupKey = MenuAssign::UsableModifier(Settings::GroupModifier());
			return (assignKey != 0 && input->IsHeld(RE::INPUT_DEVICE::kKeyboard, assignKey)) ||
			       (groupKey != 0 && input->IsHeld(RE::INPUT_DEVICE::kKeyboard, groupKey));
		}

		// Lower sorts first: modifiers (Ctrl, Alt, Shift) ahead of normal keys.
		int DisplayRank(std::uint32_t a_sc)
		{
			switch (a_sc) {
			case 0x1D:
			case 0x9D:
				return 0;  // Ctrl
			case 0x38:
			case 0xB8:
				return 1;  // Alt
			case 0x2A:
			case 0x36:
				return 2;  // Shift
			default:
				return 3;
			}
		}

		// Reconcile each entry's `hotkeyLabel` with our store every frame. We only
		// write members that actually differ and only refresh the list when something
		// changed -- so this self-heals on menu reopen / filter switch (entry objects
		// are rebuilt by the game and lose our member) without per-frame list churn.
		void PushBadges(RE::FavoritesMenu* a_menu)
		{
			auto movie = a_menu->uiMovie;
			if (!movie) {
				return;
			}

			// Un-favoriting here removes the row, but the binding would survive and come
			// back with the item -- reconcile it live (throttled; this runs every frame).
			static int pruneFrame = 0;
			if (++pruneFrame % 15 == 0) {
				Favorites::PruneUnfavorited();
			}

			RE::GFxValue itemList;
			if (!movie->GetVariable(&itemList, kItemListPath) || !itemList.IsObject()) {
				return;
			}
			RE::GFxValue entryList;
			if (!itemList.GetMember("_entryList", &entryList) || !entryList.IsArray()) {
				return;
			}

			auto*      mgr = HotkeyManager::GetSingleton();
			const auto size = entryList.GetArraySize();
			bool       changed = false;

			for (std::uint32_t i = 0; i < size; ++i) {
				RE::GFxValue entry;
				if (!entryList.GetElement(i, &entry) || !entry.IsObject()) {
					continue;
				}

				// Desired keycaps as DX scancodes, modifier first (the AS clip is the
				// vanilla "Keyboard" symbol indexed by scancode -> gotoAndStop(code)).
				std::uint32_t k1 = 0;
				std::uint32_t k2 = 0;
				std::uint32_t hands = 0;

				RE::GFxValue fidVal;
				if (Settings::IconsEnabled(Settings::MenuKind::kFavorites) &&
					entry.GetMember("formId", &fidVal) && fidVal.IsNumber()) {
					const auto fid = static_cast<RE::FormID>(fidVal.GetNumber());
					if (const auto* hk = mgr->FindByForm(fid)) {
						for (const auto& member : hk->items) {
							if (member.form == fid) {
								hands = member.hands;
								break;
							}
						}
						auto keys = hk->bind.keys;
						std::sort(keys.begin(), keys.end(), [](std::uint32_t a, std::uint32_t b) {
							const int ra = DisplayRank(a);
							const int rb = DisplayRank(b);
							return ra != rb ? ra < rb : a < b;
						});
						if (!keys.empty()) {
							k1 = keys[0];
						}
						if (keys.size() > 1) {
							k2 = keys[1];
						}
					}
				}

				const auto readNum = [&](const char* a_name) -> std::uint32_t {
					RE::GFxValue v;
					return (entry.GetMember(a_name, &v) && v.IsNumber()) ? static_cast<std::uint32_t>(v.GetNumber()) : 0;
				};
				if (readNum("hotkeyKey1") == k1 && readNum("hotkeyKey2") == k2 &&
					readNum("hotkeyHands") == hands) {
					continue;
				}

				entry.SetMember("hotkeyKey1", RE::GFxValue{ static_cast<double>(k1) });
				entry.SetMember("hotkeyKey2", RE::GFxValue{ static_cast<double>(k2) });
				entry.SetMember("hotkeyHands", RE::GFxValue{ static_cast<double>(hands) });
				changed = true;
			}

			if (changed) {
				itemList.Invoke("UpdateList");
			}
		}
	}

	void FavoritesHook::AdvanceMovie(RE::FavoritesMenu* a_this, float a_interval, std::uint32_t a_currentTime)
	{
		_AdvanceMovie(a_this, a_interval, a_currentTime);
		PushBadges(a_this);
	}

	bool FavoritesHook::MenuCanProcess(RE::MenuEventHandler* a_this, RE::InputEvent* a_event)
	{
		// While the assign-modifier is held, force keyboard buttons through to
		// ProcessButton (vanilla CanProcess would reject most keys, so our chord keys
		// would never arrive). Outside assignment we defer to vanilla entirely.
		if (a_event) {
			if (auto* button = a_event->AsButtonEvent(); button && button->device.get() == RE::INPUT_DEVICE::kKeyboard) {
				// Never claim the menu-cancel control -- it must always reach vanilla so
				// the menu can be closed even if the modifier is seen as held (a stuck
				// key would otherwise make the menu swallow every keystroke).
				// Presses only. A release is always left to vanilla: claiming it would keep it
				// from the menu, and a menu that saw the press but never the release keeps
				// that key held for good (see MenuInputBlock).
				auto*      ue = RE::UserEvents::GetSingleton();
				const bool isCancel = ue && button->userEvent == ue->cancel;
				if (button->IsPressed() && !isCancel && AnyModifierHeld()) {
					return true;
				}
			}
		}
		return _MenuCanProcess(a_this, a_event);
	}

	bool FavoritesHook::MenuProcessButton(RE::MenuEventHandler* a_this, RE::ButtonEvent* a_event)
	{
		// Blocking only -- chord capture lives in InputHandler (it reliably sees the
		// modifier release, which vanilla CanProcess would otherwise swallow).
		if (a_event && a_event->device.get() == RE::INPUT_DEVICE::kKeyboard) {
			// The cancel control is never swallowed -- the menu must stay closeable.
			// Releases always pass, for the same reason CanProcess never claims them: the
			// menu's own press/release bookkeeping -- vanilla FavoritesMenu latches on the
			// press it acts on and only unlatches on the release -- must stay paired.
			auto*      ue = RE::UserEvents::GetSingleton();
			const bool isCancel = ue && a_event->userEvent == ue->cancel;
			if (!isCancel && a_event->IsPressed()) {
				const std::uint32_t id = a_event->GetIDCode();
				const bool          modHeld = AnyModifierHeld();

				// While assigning, swallow everything so vanilla never reacts; and always
				// kill vanilla number-key (1..0) assignment -- our system owns hotkeys now.
				if (modHeld || (a_event->IsDown() && id >= 0x02 && id <= 0x0B)) {
					return true;
				}
			}
		}

		return _MenuProcessButton(a_this, a_event);
	}

	bool FavoritesHook::HandlerProcessButton(RE::FavoritesHandler* a_this, RE::ButtonEvent* a_event)
	{
		if (a_event) {
			// Keep the "open favorites menu" key working; drop everything else so the
			// vanilla 1-8 equip path is dead and only our input sink fires hotkeys.
			if (auto* ue = RE::UserEvents::GetSingleton(); ue && a_event->userEvent == ue->favorites) {
				return _HandlerProcessButton(a_this, a_event);
			}
		}
		return false;
	}

	void FavoritesHook::Install()
	{
		REL::Relocation<std::uintptr_t> menuVtbl0{ RE::VTABLE_FavoritesMenu[0] };  // IMenu
		_AdvanceMovie = menuVtbl0.write_vfunc(0x5, &FavoritesHook::AdvanceMovie);

		REL::Relocation<std::uintptr_t> menuVtbl1{ RE::VTABLE_FavoritesMenu[1] };  // MenuEventHandler
		_MenuCanProcess = menuVtbl1.write_vfunc(0x1, &FavoritesHook::MenuCanProcess);
		_MenuProcessButton = menuVtbl1.write_vfunc(0x5, &FavoritesHook::MenuProcessButton);

		REL::Relocation<std::uintptr_t> handlerVtbl{ RE::VTABLE_FavoritesHandler[0] };
		_HandlerProcessButton = handlerVtbl.write_vfunc(0x5, &FavoritesHook::HandlerProcessButton);

		logger::info("favorites hooks installed (badge + assign + vanilla off)");
	}
}
