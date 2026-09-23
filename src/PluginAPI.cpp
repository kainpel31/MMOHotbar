#include "EquipDispatch.h"
#include "HotkeyManager.h"
#include "InputHandler.h"

#include "../api/STB_HotkeySystemAPI.h"

#include <algorithm>
#include <vector>

// The implementation behind api/STB_HotkeySystemAPI.h. Bindings live only in this plugin --
// nothing about them is game state another mod could read -- so this is the one way in.
//
// Everything here is a translation layer: no CommonLibSSE type and no STL container crosses
// the boundary, because the consumer is a separate DLL that may have been built against a
// different CommonLib and a different standard library. Plain structs and counts only.
namespace HKS::PluginAPI
{
	namespace
	{
		namespace API = STB::HotkeySystem;

		[[nodiscard]] ItemId FromBinding(const API::Binding& a_b)
		{
			ItemId id;
			id.form = static_cast<RE::FormID>(a_b.form);
			id.ench = static_cast<RE::FormID>(a_b.ench);
			id.health = a_b.health;
			id.hands = a_b.hands;
			return id;
		}

		void ToBinding(const ItemId& a_id, API::Binding& a_out)
		{
			a_out.form = static_cast<std::uint32_t>(a_id.form);
			a_out.ench = static_cast<std::uint32_t>(a_id.ench);
			a_out.health = a_id.health;
			a_out.hands = a_id.hands;
			a_out.pad[0] = a_out.pad[1] = a_out.pad[2] = 0;
		}

		class Implementation final : public API::IVersion1
		{
		public:
			[[nodiscard]] std::uint32_t Version() const noexcept override { return 1; }

			[[nodiscard]] std::uint32_t Resolve(API::Device a_device, std::uint32_t a_key,
				API::Binding* a_out, std::uint32_t a_max) const noexcept override
			{
				const auto items = InputHandler::GetSingleton()->ResolveForKey(
					static_cast<RE::INPUT_DEVICE>(a_device), a_key);

				const auto n = static_cast<std::uint32_t>(items.size());
				if (a_out) {
					for (std::uint32_t i = 0; i < n && i < a_max; ++i) {
						ToBinding(items[i], a_out[i]);
					}
				}
				return n;  // the true count, so a caller can tell its buffer was too small
			}

			[[nodiscard]] bool EquipNow(const API::Binding& a_binding) const noexcept override
			{
				return EquipDispatch::EquipNow(FromBinding(a_binding));
			}

			[[nodiscard]] API::Chord GetHotkey(std::uint32_t a_form) const noexcept override
			{
				return ToChord(HotkeyManager::GetSingleton()->BindOfForm(static_cast<RE::FormID>(a_form)));
			}

			[[nodiscard]] API::Chord GetHotkeyExact(const API::Binding& a_entry) const noexcept override
			{
				return ToChord(HotkeyManager::GetSingleton()->BindOfItem(FromBinding(a_entry)));
			}

		private:
			static API::Chord ToChord(const Bind& a_bind)
			{
				API::Chord chord{};
				if (!a_bind.IsValid()) {
					return chord;  // keyCount 0 -- not bound
				}
				chord.device = static_cast<API::Device>(a_bind.device);
				// Clamped rather than asserted: kMaxChord is 2 today and Chord is sized for
				// that, but a longer chord must not walk off the end of the return value.
				chord.keyCount = (std::min)(static_cast<std::uint32_t>(a_bind.keys.size()),
					static_cast<std::uint32_t>(std::size(chord.keys)));
				for (std::uint32_t i = 0; i < chord.keyCount; ++i) {
					chord.keys[i] = a_bind.keys[i];
				}
				return chord;
			}
		};

		Implementation g_implementation;
	}
}

// Exported by name; a consumer reaches it with GetModuleHandle + GetProcAddress. Returning
// null for an unknown version is the whole compatibility story: a newer consumer asking an
// older build for an interface it does not have gets nothing and carries on without us.
extern "C" DLLEXPORT void* STB_HotkeySystem_RequestAPI(std::uint32_t a_version)
{
	if (a_version != 1) {
		logger::warn("plugin API: version {} requested, only 1 is available", a_version);
		return nullptr;
	}
	logger::info("plugin API v1 handed out");
	return static_cast<STB::HotkeySystem::IVersion1*>(&HKS::PluginAPI::g_implementation);
}
