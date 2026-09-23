#pragma once

// STB Hotkey System -- plugin API.
//
// Copy this header into your own SKSE plugin. It is standalone on purpose: no CommonLibSSE
// types cross the boundary, so it does not matter which version of CommonLib either side
// was built against, and nothing here has to match your STL.
//
// Why an API is needed at all: this mod deliberately does NOT store bindings in game state.
// Vanilla keeps them in ExtraHotkey slots and MagicFavorites::hotkeys[8], which the engine
// renumbers and clears on stack splits, equips and container moves -- the reason vanilla
// hotkeys "reset". Here a binding is a chord -> form table owned by the plugin and mirrored
// into its own co-save record, so there is nothing in the game world for another plugin to
// read. This is the way in.
//
// Usage, from kPostLoad or later (every plugin is loaded by then):
//
//     namespace HKS = STB::HotkeySystem;
//     const HKS::IVersion1* g_hotkeys = nullptr;
//
//     if (auto* mod = GetModuleHandleA(HKS::kModuleName)) {
//         if (auto request = reinterpret_cast<HKS::RequestAPI_t>(
//                 GetProcAddress(mod, HKS::kRequestFunction))) {
//             g_hotkeys = static_cast<const HKS::IVersion1*>(request(1));
//         }
//     }
//
// A null result means the mod is absent, too old, or refused the version -- carry on
// without it. The returned object is owned by STB Hotkey System and lives for the whole
// process: never delete it, and do not cache anything it hands back across frames.

#include <cstdint>

namespace STB::HotkeySystem
{
	inline constexpr const char* kModuleName = "STB_HotkeySystem.dll";
	inline constexpr const char* kRequestFunction = "STB_HotkeySystem_RequestAPI";

	// Mirrors RE::INPUT_DEVICE.
	enum class Device : std::uint32_t
	{
		kKeyboard = 0,
		kMouse = 1,
		kGamepad = 2,
	};

	// Which hand the binding puts its form into, recorded when it was assigned. kNone means
	// the form was not in hand at that moment and the engine picks as usual.
	enum Hand : std::uint8_t
	{
		kHandNone = 0,
		kHandRight = 1 << 0,
		kHandLeft = 1 << 1,
		kHandBoth = kHandRight | kHandLeft,
	};

	// One bound entry. `form` is the base object, already resolved against the current load
	// order; the rest identifies a specific instance of it, because an enchanted or tempered
	// copy binds separately from a plain one.
	struct Binding
	{
		std::uint32_t form;    // FormID of the base object, spell or shout
		std::uint32_t ench;    // ExtraEnchantment FormID, 0 for a plain copy
		std::int32_t  health;  // temper as fixed point x100, 0 for a plain copy
		std::uint8_t  hands;   // Hand mask
		std::uint8_t  pad[3];
	};
	static_assert(sizeof(Binding) == 16);

	// A chord is at most two simultaneously-held keys, so it fits in the return value and
	// needs no buffer. Codes are device-native -- scancodes for the keyboard -- and sorted
	// ascending, the same order the mod stores them in.
	struct Chord
	{
		Device        device;    // meaningless when keyCount is 0
		std::uint32_t keyCount;  // 0 = the entry is not bound to anything
		std::uint32_t keys[2];
	};
	static_assert(sizeof(Chord) == 16);

	// Interface version 1. Methods are safe to call from an input handler on the main
	// thread; everything is copied out under the store's lock.
	class IVersion1
	{
	public:
		[[nodiscard]] virtual std::uint32_t Version() const noexcept = 0;

		// What pressing a_key on a_device would fire at this instant, taking the keys
		// currently held into account -- so a chord resolves the same way it would for a
		// real press. Call it while handling the key-down event, not later: the answer
		// depends on what is held right now.
		//
		// A key can carry a group of any size, so this one does need a buffer you own:
		// nothing that grows can be returned across a DLL boundary without agreeing on an
		// allocator. Writes up to a_max entries and returns how many there actually are,
		// which may exceed a_max -- 0 means the key is bound to nothing.
		[[nodiscard]] virtual std::uint32_t Resolve(Device a_device, std::uint32_t a_key,
			Binding* a_out, std::uint32_t a_max) const noexcept = 0;

		// Equip one binding immediately, on the calling thread, which must be the main one.
		// For anything held in a hand this is what lets you act on the same press -- a mod
		// that starts charging a shout needs it in the voice slot before it returns.
		//
		// false means nothing was equipped: the player un-favorited the entry (the binding
		// is dropped) or no longer holds the form. Do not go ahead with whatever you were
		// going to do -- for a shout key that would fire whatever is still in the slot.
		//
		// A successful call also tells STB Hotkey System that you have handled this press,
		// so it will not equip the same entry again a moment later from its own input sink.
		[[nodiscard]] virtual bool EquipNow(const Binding& a_binding) const noexcept = 0;

		// The other direction: which key an entry sits on, for drawing it on a row or a HUD.
		// keyCount 0 means the entry is not bound, so this is also how you ask whether it
		// is bound at all.
		//
		// By base form: any instance of it will do. If the player has a plain sword on one
		// key and an enchanted copy of the same base on another, you get whichever the
		// lookup reaches first -- fine for "does this form have a hotkey", wrong for
		// labelling a specific inventory row.
		[[nodiscard]] virtual Chord GetHotkey(std::uint32_t a_form) const noexcept = 0;

		// By exact instance, which is what a row in an item list needs: fill in `ench` and
		// `health` from the row's extra data (0 and 0 for a plain copy) and only the key
		// bound to that copy comes back. `hands` is ignored -- it describes the binding, not
		// the item. A Binding handed out by Resolve round-trips through here unchanged.
		[[nodiscard]] virtual Chord GetHotkeyExact(const Binding& a_entry) const noexcept = 0;

	protected:
		~IVersion1() = default;  // owned by the provider; consumers must not delete it
	};

	using RequestAPI_t = void* (*)(std::uint32_t a_version);
}
