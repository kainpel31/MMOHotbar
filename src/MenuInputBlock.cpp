#include "MenuInputBlock.h"

#include "InputHandler.h"
#include "MenuAssign.h"
#include "Settings.h"

#include <unordered_set>
#include <utility>
#include <vector>

namespace HKS
{
	namespace
	{
		// Mouse ids 0..7 are physical buttons; 8/9 are the wheel. Keep the wheel so the
		// list can still be scrolled with the modifier down.
		constexpr std::uint32_t kFirstWheelId = 8;

		// Keys whose press reached the menus and whose release has not yet.
		//
		// Withholding has to be symmetric: a menu that saw a key go down must see it come
		// back up, or its idea of what is held goes stale for good. That is exactly how the
		// Favorites menu became impossible to leave after building a group. MenuControls is
		// notified before our own input sink, so when the group modifier went down the sink
		// had not yet marked it held -- the press went through -- and when it came up the
		// sink still had it marked -- the release was withheld. Scaleform kept Left Shift
		// down forever, turned every Tab into SHIFT_TAB (gfx InputDelegate.inputToNav), and
		// SkyUI-derived favorites menus only close on a plain TAB. The inventory survived
		// only because it accepts either.
		//
		// So a release, or a repeat, is delivered if and only if its press was. Main thread
		// only: MenuControls::ProcessEvent runs from the input dispatch. A stale entry (a
		// release lost to alt-tab) can only ever cause an extra delivery, never an extra
		// withhold, so nothing needs resetting.
		std::unordered_set<std::uint64_t> g_delivered;

		[[nodiscard]] std::uint64_t KeyOf(const RE::ButtonEvent* a_button)
		{
			return (static_cast<std::uint64_t>(a_button->device.get()) << 32) | a_button->GetIDCode();
		}

		// The two devices whose buttons can be withheld, and therefore need their releases
		// kept in step. The gamepad is never touched: the menus need it to navigate at all.
		[[nodiscard]] bool Tracked(const RE::ButtonEvent* a_button)
		{
			const auto device = a_button->device.get();
			return device == RE::INPUT_DEVICE::kKeyboard || device == RE::INPUT_DEVICE::kMouse;
		}

		// Should a fresh press be hidden from the menus while a modifier is held?
		[[nodiscard]] bool WithholdPress(const RE::ButtonEvent* a_button, const RE::UserEvents* a_ue)
		{
			if (a_button->device.get() == RE::INPUT_DEVICE::kMouse) {
				return a_button->GetIDCode() < kFirstWheelId;
			}
			// The cancel control is never withheld: the menu has to stay closeable.
			return !(a_ue && a_button->userEvent == a_ue->cancel);
		}

		// Whether withholding may happen at all right now, before asking what is held.
		[[nodiscard]] bool Armed()
		{
			if (!Settings::BlockMenuKeys() || !MenuAssign::IsAssignMenuOpen()) {
				return false;
			}
			auto* ui = RE::UI::GetSingleton();
			// The console and our own prompts sit on top of the menu and own the keyboard --
			// the player is typing or answering, not binding (this mirrors the capture gate
			// in InputHandler::ProcessEvent).
			return ui && !ui->IsMenuOpen(RE::Console::MENU_NAME) &&
			       !ui->IsMenuOpen(RE::MessageBoxMenu::MENU_NAME);
		}
	}

	bool MenuInputBlock::Blocking()
	{
		if (!Armed()) {
			return false;
		}
		auto*      input = InputHandler::GetSingleton();
		const auto assignKey = MenuAssign::UsableModifier(Settings::AssignModifier());
		const auto groupKey = MenuAssign::UsableModifier(Settings::GroupModifier());
		return (assignKey != 0 && input->IsHeld(RE::INPUT_DEVICE::kKeyboard, assignKey)) ||
		       (groupKey != 0 && input->IsHeld(RE::INPUT_DEVICE::kKeyboard, groupKey));
	}

	RE::BSEventNotifyControl MenuInputBlock::ProcessEvent(
		RE::MenuControls*                    a_this,
		RE::InputEvent* const*               a_event,
		RE::BSTEventSource<RE::InputEvent*>* a_source)
	{
		if (!a_event || !*a_event) {
			return _ProcessEvent(a_this, a_event, a_source);
		}

		const bool          armed = Armed();
		// A modifier the menu uses for its own controls is left entirely to the menu.
		const std::uint32_t assignKey = armed ? MenuAssign::UsableModifier(Settings::AssignModifier()) : 0;
		const std::uint32_t groupKey = armed ? MenuAssign::UsableModifier(Settings::GroupModifier()) : 0;
		auto*               input = InputHandler::GetSingleton();
		const auto*         ue = RE::UserEvents::GetSingleton();

		// What is held, as of the start of this chain. Our input sink updates it only after
		// MenuControls has been through the chain, so it lags by one dispatch; it is kept in
		// step below as the chain itself presses and releases the modifiers, so a modifier
		// and a key landing in the same frame still resolve in the right order.
		bool assignHeld = armed && assignKey != 0 && input->IsHeld(RE::INPUT_DEVICE::kKeyboard, assignKey);
		bool groupHeld = armed && groupKey != 0 && input->IsHeld(RE::INPUT_DEVICE::kKeyboard, groupKey);

		// Every event is walked -- tracking has to see presses that go through while nothing
		// is held, or their releases would later be taken for strangers and withheld. The
		// chain is relinked around whatever is withheld, the original runs over the rest, and
		// every `next` is put back: the chain belongs to the input manager and is walked
		// again by the sinks after us, so it must come out of here exactly as it went in.
		std::vector<std::pair<RE::InputEvent*, RE::InputEvent*>> saved;
		RE::InputEvent*                                          head = nullptr;
		RE::InputEvent*                                          tail = nullptr;

		for (auto* e = *a_event; e; e = e->next) {
			saved.emplace_back(e, e->next);

			const bool blocking = assignHeld || groupHeld;
			bool       deliver = true;

			if (e->GetEventType() == RE::INPUT_EVENT_TYPE::kChar) {
				// SkyUI's type-search runs off char events. They have no release, so there is
				// nothing to keep in step -- just hold them back while a modifier is down.
				deliver = !blocking;
			} else if (auto* button = e->AsButtonEvent(); button && Tracked(button)) {
				const auto key = KeyOf(button);
				if (button->IsDown()) {
					deliver = !(blocking && WithholdPress(button, ue));
					if (deliver) {
						g_delivered.insert(key);
					}
				} else if (button->IsPressed()) {
					deliver = g_delivered.contains(key);  // repeat: only for a delivered press
				} else {
					deliver = g_delivered.erase(key) > 0;  // release: only for a delivered press
				}

				if (armed && button->device.get() == RE::INPUT_DEVICE::kKeyboard) {
					const auto id = button->GetIDCode();
					if (assignKey != 0 && id == assignKey) {
						assignHeld = button->IsPressed();
					}
					if (groupKey != 0 && id == groupKey) {
						groupHeld = button->IsPressed();
					}
				}
			}

			if (!deliver) {
				continue;
			}
			(tail ? tail->next : head) = e;
			tail = e;
		}
		if (tail) {
			tail->next = nullptr;
		}

		// head may be null (everything withheld); vanilla handles an empty chain -- it just
		// runs its own prologue/epilogue, which it would have run anyway.
		const auto result = _ProcessEvent(a_this, &head, a_source);

		for (auto& [node, next] : saved) {
			node->next = next;
		}
		return result;
	}

	void MenuInputBlock::Install()
	{
		REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_MenuControls[0] };  // BSTEventSink<InputEvent*>
		_ProcessEvent = vtbl.write_vfunc(0x1, &MenuInputBlock::ProcessEvent);
		logger::info("MenuControls::ProcessEvent hooked (menu keys withheld while assigning)");
	}
}
