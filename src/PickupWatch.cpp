#include "PickupWatch.h"

#include "Favorites.h"
#include "HotkeyManager.h"
#include "Settings.h"

namespace HKS
{
	namespace
	{
		class ContainerSink : public RE::BSTEventSink<RE::TESContainerChangedEvent>
		{
		public:
			static ContainerSink* GetSingleton()
			{
				static ContainerSink singleton;
				return &singleton;
			}

			RE::BSEventNotifyControl ProcessEvent(
				const RE::TESContainerChangedEvent*          a_event,
				RE::BSTEventSource<RE::TESContainerChangedEvent>* /*a_source*/) override
			{
				if (!a_event || !a_event->baseObj) {
					return RE::BSEventNotifyControl::kContinue;
				}

				// Only items the player *gains*. A pickup / purchase / pickpocket sets
				// newContainer to the player; drops and consumption move it away.
				auto* player = RE::PlayerCharacter::GetSingleton();
				if (!player || a_event->newContainer != player->GetFormID()) {
					return RE::BSEventNotifyControl::kContinue;
				}

				// Only forms we still hold a bind for -- match on base form (favorite state
				// is per base object, and a fungible potion/gear bind is form-only anyway).
				if (!HotkeyManager::GetSingleton()->FindByForm(a_event->baseObj)) {
					if (Settings::DebugLog()) {  // fires on every single pickup
						logger::info("pickup {:08X}: no binding for this form, ignoring",
							a_event->baseObj);
					}
					return RE::BSEventNotifyControl::kContinue;
				}

				// If it's already favorited (e.g. picking up more of an item still in the
				// pack) leave it be -- SetFavorite toggles, so re-running it here would
				// UN-favorite it. We only want to restore the star when it was lost.
				if (Favorites::IsFavorited(a_event->baseObj)) {
					if (Settings::DebugLog()) {
						logger::info("pickup {:08X}: binding present and already favorited",
							a_event->baseObj);
					}
					return RE::BSEventNotifyControl::kContinue;
				}

				logger::info("re-favoriting bound form {:08X} on pickup", a_event->baseObj);
				Favorites::EnsureFavorited(a_event->baseObj);  // defers to the main-thread task queue
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	void PickupWatch::Register()
	{
		if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
			holder->AddEventSink(ContainerSink::GetSingleton());
			logger::info("pickup watch registered (re-favorite on return)");
		}
	}
}
