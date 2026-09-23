#include "InputHandler.h"

#include "EquipDispatch.h"
#include "Favorites.h"
#include "HotkeyManager.h"
#include "InventoryIcons.h"
#include "KeyConflict.h"
#include "Localization.h"
#include "MessageBox.h"
#include "MenuAssign.h"
#include "Settings.h"

#include <vector>

namespace HKS
{
	InputHandler* InputHandler::GetSingleton()
	{
		static InputHandler singleton;
		return &singleton;
	}

	void InputHandler::Register()
	{
		if (auto* idm = RE::BSInputDeviceManager::GetSingleton()) {
			idm->AddEventSink(static_cast<RE::BSTEventSink<RE::InputEvent*>*>(GetSingleton()));
			logger::info("input sink registered");
		}
		if (auto* ui = RE::UI::GetSingleton()) {
			static_cast<RE::BSTEventSource<RE::MenuOpenCloseEvent>*>(ui)->AddEventSink(
				static_cast<RE::BSTEventSink<RE::MenuOpenCloseEvent>*>(GetSingleton()));
			logger::info("menu open/close sink registered");
		}
	}

	RE::BSEventNotifyControl InputHandler::ProcessEvent(
		const RE::MenuOpenCloseEvent*              a_event,
		RE::BSTEventSource<RE::MenuOpenCloseEvent>* /*a_source*/)
	{
		if (!a_event) {
			return RE::BSEventNotifyControl::kContinue;
		}
		// Only resync at the boundaries of menus where a stuck key actually does harm --
		// the Favorites menu (uncloseable on a stuck modifier) and the SkyUI item menus
		// that share the assign path. Transient menus (cursor, tooltips) are ignored so an
		// in-progress capture isn't dropped by an unrelated toggle.
		const std::string_view name{ a_event->menuName.c_str() };
		if (name != RE::FavoritesMenu::MENU_NAME &&
			name != RE::InventoryMenu::MENU_NAME &&
			name != RE::ContainerMenu::MENU_NAME &&
			name != RE::MagicMenu::MENU_NAME &&
			name != RE::GiftMenu::MENU_NAME &&
			name != RE::BarterMenu::MENU_NAME) {
			return RE::BSEventNotifyControl::kContinue;
		}
		// Drop any stale pressed keys and abandon a half-finished capture so a stuck
		// modifier can never brick the menu.
		_kbHeld.clear();
		_msHeld.clear();
		_padHeld.clear();
		_capturing = false;
		_capGroup = false;
		_capChord.clear();
		_capDown.clear();
		_capTarget = {};

		// Menu control keys are read per menu instance; a new menu may bind different ones.
		MenuAssign::InvalidateMenuKeys();

		// On open, reconcile bindings with favorite state: a hotkey whose item the player
		// un-favorited (vanilla F) drops its binding + keycap now, instead of lingering and
		// reappearing when the item next returns to the inventory. Done on open only, so it
		// never races the auto-favorite that assignment queues while the menu is open.
		if (a_event->opening && Favorites::PruneUnfavorited()) {
			InventoryIcons::MarkDirty();
		}
		return RE::BSEventNotifyControl::kContinue;
	}

	std::unordered_set<std::uint32_t>& InputHandler::HeldFor(RE::INPUT_DEVICE a_device)
	{
		switch (a_device) {
		case RE::INPUT_DEVICE::kMouse:
			return _msHeld;
		case RE::INPUT_DEVICE::kGamepad:
			return _padHeld;
		default:
			return _kbHeld;
		}
	}

	bool InputHandler::IsHeld(RE::INPUT_DEVICE a_device, std::uint32_t a_key) const
	{
		switch (a_device) {
		case RE::INPUT_DEVICE::kMouse:
			return _msHeld.contains(a_key);
		case RE::INPUT_DEVICE::kGamepad:
			return _padHeld.contains(a_key);
		default:
			return _kbHeld.contains(a_key);
		}
	}

	std::vector<ItemId> InputHandler::ResolveForKey(RE::INPUT_DEVICE a_device, std::uint32_t a_key) const
	{
		auto held = const_cast<InputHandler*>(this)->HeldFor(a_device);
		held.insert(a_key);  // the just-pressed key may not be in the set yet
		return HotkeyManager::GetSingleton()->ResolveChordItems(a_device, held, a_key);
	}

	bool InputHandler::FiringSuppressed()
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			return true;
		}
		// Never equip/unequip while an item menu is showing: doing so frees/moves the
		// ExtraDataList that Inventory3DManager's preview holds -> UAF crash.
		if (MenuAssign::IsAssignMenuOpen()) {
			return true;
		}
		// Menus that own input but don't pause (or are un-paused by SkyrimSoulsRE) and
		// aren't "application" menus -- chiefly the dialogue menu. Enumerated by name so
		// it still holds under Souls, which keeps input flowing. Fader/Loading mirror the
		// transition guard vanilla FavoritesHandler::ProcessButton applies.
		static constexpr std::string_view kBlockingMenus[] = {
			RE::DialogueMenu::MENU_NAME,
			RE::CraftingMenu::MENU_NAME,
			RE::BookMenu::MENU_NAME,
			RE::LockpickingMenu::MENU_NAME,
			RE::SleepWaitMenu::MENU_NAME,
			RE::TrainingMenu::MENU_NAME,
			RE::TweenMenu::MENU_NAME,
			RE::LevelUpMenu::MENU_NAME,
			RE::MessageBoxMenu::MENU_NAME,
			RE::Console::MENU_NAME,
			RE::LoadingMenu::MENU_NAME,
			RE::FaderMenu::MENU_NAME,
		};
		for (const auto& name : kBlockingMenus) {
			if (ui->IsMenuOpen(name)) {
				return true;
			}
		}
		// Paused, or an application menu (inventory/magic/map/stats/journal/...) owns the
		// screen. We deliberately do NOT gate on ControlMap::IsMovementControlsEnabled():
		// vanilla favorites don't, and that flag is also cleared by quests/cutscenes that
		// merely disable movement -- states where a vanilla hotkey would still equip. That
		// broad gate was what made hotkeys "randomly" dead when vanilla would have worked.
		return ui->GameIsPaused() || ui->IsApplicationMenuOpen();
	}

	// Actually store the binding. Split out of CommitCapture because the key-conflict
	// prompt is asynchronous: when it fires we hand this to the message box callback and
	// run it only if the player confirms.
	void InputHandler::ApplyAssignment(Bind a_bind, ItemId a_target, bool a_group)
	{
		// Snapshot which hand(s) the form is in right now, so the hotkey puts it back there
		// on every press instead of leaving the choice to the engine -- SkyUI's saved equip
		// state. Read here, at the moment of assignment, because that is what the player is
		// looking at; nothing else in the binding changes afterwards.
		if (Settings::RememberHand()) {
			a_target.hands = EquipDispatch::CurrentHands(RE::TESForm::LookupByID(a_target.form));
		}

		auto*      mgr = HotkeyManager::GetSingleton();
		const auto res = a_group ? mgr->AddToGroup(a_bind, a_target) : mgr->Assign(a_bind, a_target);
		logger::info("{} chord ({} keys, dev {}) -> form {:08X} ench {:08X} hp {} hands {} (result {})",
			a_group ? "grouped" : "assigned",
			a_bind.keys.size(), static_cast<int>(a_bind.device), a_target.form, a_target.ench,
			a_target.health, a_target.hands, static_cast<int>(res));
		InventoryIcons::MarkDirty();  // re-stamp menu keycaps (handles displaced bindings)
		// Favorite it right away (star + shows in Favorites with our badge), like a
		// vanilla F press. Prefer the game's real-entry path (live refresh); fall back
		// to the by-form path otherwise.
		if (res != HotkeyManager::AssignResult::kRemoved) {
			if (!Favorites::FavoriteSelectedItem(a_target.form)) {
				Favorites::EnsureFavorited(a_target.form);
			}
		}
	}

	void InputHandler::CommitCapture()
	{
		if (_capTarget && !_capChord.empty()) {
			Bind bind;
			bind.device = _capDevice;
			bind.keys.assign(_capChord.begin(), _capChord.end());
			bind.Canonicalize();

			// If one of the chord's keys already drives a vanilla gameplay control, pressing
			// it in game would fire both. Ask before committing -- unless the player turned
			// the check off, or this is a keyboard-less bind (mouse/gamepad aren't checked).
			std::string clashKey;
			std::string clashControl;
			if (Settings::WarnKeyConflict() && bind.device == RE::INPUT_DEVICE::kKeyboard) {
				for (auto k : bind.keys) {
					clashControl = KeyConflict::GameplayControl(k);
					if (!clashControl.empty()) {
						clashKey = KeyConflict::KeyName(k);
						break;
					}
				}
			}

			if (!clashControl.empty()) {
				const ItemId target = _capTarget;
				const bool   group = _capGroup;
				ShowMessageBox(
					Localization::Format("$STB_HK_KeyConflict_Body", { clashKey, clashControl }),
					[bind, target, group](unsigned int a_button) {
						if (a_button == 0) {
							GetSingleton()->ApplyAssignment(bind, target, group);
						} else {
							logger::info("assign cancelled by player (key conflict)");
						}
					},
					{ Localization::Get("$STB_HK_KeyConflict_Assign"),
						Localization::Get("$STB_HK_KeyConflict_Cancel") });
				logger::info("key conflict: {} is bound to \"{}\" -- prompting", clashKey, clashControl);
			} else {
				ApplyAssignment(bind, _capTarget, _capGroup);
			}
		}
		else if (!_capChord.empty()) {
			// Keys were captured but the highlighted entry gave no form -- the selection
			// couldn't be read (menu path mismatch, nothing highlighted, non-item row).
			// Logged because to the player this looks like "the hotkey just didn't take".
			logger::warn("assign aborted: {} key(s) captured but no target form under the cursor",
				_capChord.size());
		}
		// Reset only the pending-chord state; the capture *session* stays open until the
		// modifier is released (handled in ProcessEvent), so the next item can be bound in
		// the same hold without releasing the modifier.
		_capChord.clear();
		_capDown.clear();
		_capTarget = {};
	}

	RE::BSEventNotifyControl InputHandler::ProcessEvent(
		RE::InputEvent* const*               a_event,
		RE::BSTEventSource<RE::InputEvent*>* /*a_source*/)
	{
		if (!a_event) {
			return RE::BSEventNotifyControl::kContinue;
		}

		// The console and our own key-conflict prompt overlay the assign menu but steal
		// input focus -- never capture a chord (or assign) while either is up. The player
		// is typing commands or answering the prompt, not binding; without this a held
		// modifier would keep capturing behind the dialog.
		auto*               ui = RE::UI::GetSingleton();
		const bool          blockingOverlay = ui && (ui->IsMenuOpen(RE::Console::MENU_NAME) ||
                                              ui->IsMenuOpen(RE::MessageBoxMenu::MENU_NAME));
		const bool          assignOpen = MenuAssign::IsAssignMenuOpen() && !blockingOverlay;
		// A modifier the open menu uses for its own controls is not ours in that menu -- see
		// MenuAssign::MenuUsesKey. 0 means "no such modifier here".
		const std::uint32_t modKey = MenuAssign::UsableModifier(Settings::AssignModifier());
		const std::uint32_t groupKey = MenuAssign::UsableModifier(Settings::GroupModifier());

		if (!assignOpen && _capturing) {  // menu closed mid-capture -> abandon
			_capturing = false;
			_capGroup = false;
			_capChord.clear();
			_capDown.clear();
			_capTarget = {};
		}

		for (auto* e = *a_event; e; e = e->next) {
			auto* button = e->AsButtonEvent();
			if (!button || !button->HasIDCode()) {
				continue;
			}

			const auto device = button->device.get();
			const auto idCode = button->GetIDCode();
			const bool pressed = button->IsPressed();
			auto&      held = HeldFor(device);

			if (pressed) {
				held.insert(idCode);
			} else {
				held.erase(idCode);
			}

			// ---- assignment capture (an assign-menu open) ----
			// The assign-modifier is a keyboard key; the chord it captures can be keyboard
			// OR mouse keys (a chord stays single-device, set by its first key).
			if (assignOpen) {
				// Two modifiers, same capture machinery: the assign one sets the chord to
				// the single selected item, the group one stacks the item onto whatever the
				// chord already holds. Which one opened the session decides for the whole
				// hold, so the meaning can't change halfway through a bind.
				const bool isModKey = device == RE::INPUT_DEVICE::kKeyboard &&
				                      ((modKey != 0 && idCode == modKey) || (groupKey != 0 && idCode == groupKey));
				if (isModKey) {
					if (button->IsDown() && !_capturing) {
						// Open a capture session. The target is NOT locked here -- it's
						// locked at each chord's first key-down (below), so between binds
						// the selection can move freely (mouse-hover the next item) and you
						// can bind several items in one modifier hold.
						_capturing = true;
						_capGroup = groupKey != 0 && idCode == groupKey && idCode != modKey;
						_capModKey = idCode;
						_capChord.clear();
						_capDown.clear();
						_capTarget = {};
						_capDevice = RE::INPUT_DEVICE::kKeyboard;
					} else if (!pressed && _capturing && idCode == _capModKey) {
						// Session modifier released -> commit any pending chord and close it.
						CommitCapture();
						_capturing = false;
						_capGroup = false;
					}
					continue;  // a modifier key never fires a hotkey
				}
				if (_capturing && (device == RE::INPUT_DEVICE::kKeyboard || device == RE::INPUT_DEVICE::kMouse)) {
					if (button->IsDown()) {
						const std::size_t maxKeys = Settings::EnableChords() ? kMaxChord : 1;
						if (_capChord.empty()) {
							// First key of a new chord: fix its device and lock the item now
							// (before the key can move SkyUI's type-search selection).
							_capDevice = device;
							_capTarget = MenuAssign::GetSelectedAssignTarget();
						}
						if (device == _capDevice && _capChord.size() < maxKeys) {
							_capChord.insert(idCode);
							_capDown.insert(idCode);
							// Chord full -> commit at once, no modifier release needed. Its
							// keys stay down; their key-ups are ignored (CommitCapture
							// cleared _capDown), so they can't re-commit.
							if (_capChord.size() >= maxKeys) {
								CommitCapture();
							}
						}
					} else if (_capDown.erase(idCode) && _capDown.empty() && !_capChord.empty()) {
						// A not-yet-full chord whose keys were all released -> commit as-is.
						// This is what lets a single key commit on release while the modifier
						// is still held, so several single-key binds fire off in one hold.
						CommitCapture();
					}
					continue;  // captured keys must not fire
				}
			}

			// ---- firing ----
			if (!pressed || !button->IsDown() || FiringSuppressed()) {
				continue;  // only the completing down-stroke fires, never while paused
			}

			// Only a chord that CONTAINS idCode is returned, so the matched bind is one this
			// keystroke actually completes (an unrelated held/phantom key can't shadow it
			// and drop the press). The members come back copied: the store is mutated from
			// the main thread, so a pointer into it must not outlive the lock.
			auto items = HotkeyManager::GetSingleton()->ResolveChordItems(device, held, idCode);
			if (items.empty()) {
				continue;
			}
			// A mod that equipped one of these through the plugin API a moment ago has taken
			// responsibility for this press -- firing it again here would re-equip on top of
			// whatever it started (a shout mid-charge, say). The rest of a group still goes
			// through, so "shout + armour" on one key still puts the armour on.
			std::erase_if(items, [](const ItemId& a_id) { return EquipDispatch::IsClaimed(a_id); });
			if (items.empty()) {
				continue;
			}
			EquipDispatch::Fire(std::move(items));
		}

		return RE::BSEventNotifyControl::kContinue;
	}
}
