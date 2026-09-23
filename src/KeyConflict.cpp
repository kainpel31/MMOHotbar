#include "KeyConflict.h"

#include <array>

namespace HKS::KeyConflict
{
	namespace
	{
		// The vanilla favorites slots. Bound on the number row by default, and replaced by
		// this mod, so they must never be reported as a clash.
		bool IsVanillaHotkeySlot(std::string_view a_event)
		{
			const auto* ue = RE::UserEvents::GetSingleton();
			if (!ue) {
				return false;
			}
			const std::array slots{
				&ue->hotkey1, &ue->hotkey2, &ue->hotkey3, &ue->hotkey4,
				&ue->hotkey5, &ue->hotkey6, &ue->hotkey7, &ue->hotkey8,
			};
			for (const auto* slot : slots) {
				if (a_event == std::string_view{ slot->c_str() ? slot->c_str() : "" }) {
					return true;
				}
			}
			return false;
		}
	}

	std::string KeyName(std::uint32_t a_code)
	{
		switch (a_code) {
		// Modifiers / control keys
		case 0x01: return "Esc";
		case 0x0E: return "Backspace";
		case 0x0F: return "Tab";
		case 0x1C: return "Enter";
		case 0x1D: return "Left Ctrl";
		case 0x9D: return "Right Ctrl";
		case 0x2A: return "Left Shift";
		case 0x36: return "Right Shift";
		case 0x38: return "Left Alt";
		case 0xB8: return "Right Alt";
		case 0x39: return "Space";
		case 0x3A: return "Caps Lock";
		// Number row
		case 0x02: return "1";
		case 0x03: return "2";
		case 0x04: return "3";
		case 0x05: return "4";
		case 0x06: return "5";
		case 0x07: return "6";
		case 0x08: return "7";
		case 0x09: return "8";
		case 0x0A: return "9";
		case 0x0B: return "0";
		case 0x0C: return "-";
		case 0x0D: return "=";
		// Letters
		case 0x10: return "Q";
		case 0x11: return "W";
		case 0x12: return "E";
		case 0x13: return "R";
		case 0x14: return "T";
		case 0x15: return "Y";
		case 0x16: return "U";
		case 0x17: return "I";
		case 0x18: return "O";
		case 0x19: return "P";
		case 0x1A: return "[";
		case 0x1B: return "]";
		case 0x1E: return "A";
		case 0x1F: return "S";
		case 0x20: return "D";
		case 0x21: return "F";
		case 0x22: return "G";
		case 0x23: return "H";
		case 0x24: return "J";
		case 0x25: return "K";
		case 0x26: return "L";
		case 0x27: return ";";
		case 0x28: return "'";
		case 0x29: return "`";
		case 0x2B: return "\\";
		case 0x2C: return "Z";
		case 0x2D: return "X";
		case 0x2E: return "C";
		case 0x2F: return "V";
		case 0x30: return "B";
		case 0x31: return "N";
		case 0x32: return "M";
		case 0x33: return ",";
		case 0x34: return ".";
		case 0x35: return "/";
		// Function keys
		case 0x3B: return "F1";
		case 0x3C: return "F2";
		case 0x3D: return "F3";
		case 0x3E: return "F4";
		case 0x3F: return "F5";
		case 0x40: return "F6";
		case 0x41: return "F7";
		case 0x42: return "F8";
		case 0x43: return "F9";
		case 0x44: return "F10";
		case 0x57: return "F11";
		case 0x58: return "F12";
		default:
			return "Key " + std::to_string(a_code);
		}
	}

	std::string GameplayControl(std::uint32_t a_code)
	{
		auto* controlMap = RE::ControlMap::GetSingleton();
		if (!controlMap) {
			return {};
		}

		const auto name = controlMap->GetUserEventName(
			a_code, RE::INPUT_DEVICE::kKeyboard, RE::UserEvents::INPUT_CONTEXT_ID::kGameplay);
		if (name.empty() || IsVanillaHotkeySlot(name)) {
			return {};
		}
		return std::string{ name };
	}
}
