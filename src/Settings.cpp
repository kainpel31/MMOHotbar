#include "Settings.h"

#include <SimpleIni.hpp>

namespace HKS
{
	void Settings::Load()
	{
		// MMOHotbar.ini is the file we ship and the one the mod writes back to (see
		// ModifierConflict::PersistModifier). The upstream name is read first as a base
		// layer, so an install that predates the rename keeps every setting it had; keys
		// present in MMOHotbar.ini then win over it. LoadFile merges into what is already
		// there -- never Reset() between the two.
		constexpr auto kPrimary = L"Data/SKSE/Plugins/MMOHotbar.ini";
		constexpr auto kLegacy = L"Data/SKSE/Plugins/STB_HotkeySystem.ini";

		CSimpleIniA ini;
		ini.SetUnicode();
		const bool legacy = ini.LoadFile(kLegacy) >= 0;
		const bool primary = ini.LoadFile(kPrimary) >= 0;
		if (!primary && !legacy) {
			logger::info("no INI found, using defaults (assign modifier = 0x{:X})", _assignModifier);
			return;
		}
		if (!primary) {
			logger::info("settings read from the legacy STB_HotkeySystem.ini -- rename it to "
			             "MMOHotbar.ini to keep editing it there");
		} else if (legacy) {
			logger::warn("both MMOHotbar.ini and STB_HotkeySystem.ini are present; keys set in "
			             "MMOHotbar.ini win -- move your settings into it, then delete the old file");
		}

		_assignModifier = static_cast<std::uint32_t>(
			ini.GetLongValue("Assignment", "iModifierScanCode", static_cast<long>(_assignModifier)));

		_groupModifier = static_cast<std::uint32_t>(
			ini.GetLongValue("Assignment", "iGroupModifierScanCode", static_cast<long>(_groupModifier)));
		// One key can't mean both "replace" and "stack", and a session that already gave up
		// on groups stays given up (see DisableGroups).
		if (_groupsDisabled || _groupModifier == _assignModifier) {
			_groupModifier = 0;
		}

		_enableChords = ini.GetBoolValue("Assignment", "bEnableChords", _enableChords);

		_blockMenuKeys = ini.GetBoolValue("Assignment", "bBlockMenuKeys", _blockMenuKeys);

		_showAssignHint = ini.GetBoolValue("Assignment", "bShowAssignHint", _showAssignHint);

		_warnKeyConflict = ini.GetBoolValue("Assignment", "bWarnKeyConflict", _warnKeyConflict);

		_migrateVanillaHotkeys = ini.GetBoolValue("Compatibility", "bMigrateVanillaHotkeys", _migrateVanillaHotkeys);

		_debugLog = ini.GetBoolValue("Debug", "bDebugLog", _debugLog);

		_rememberHand = ini.GetBoolValue("Gameplay", "bRememberHand", _rememberHand);
		_equipMode = static_cast<int>(ini.GetLongValue("Gameplay", "iEquipMode", _equipMode));

		_iconAfterName = ini.GetBoolValue("Icons", "bAfterName", _iconAfterName);
		_iconScale = static_cast<float>(ini.GetDoubleValue("Icons", "fScale", _iconScale));
		_iconY = static_cast<float>(ini.GetDoubleValue("Icons", "fY", _iconY));
		_iconX = static_cast<float>(ini.GetDoubleValue("Icons", "fX", _iconX));
		_iconGap = static_cast<float>(ini.GetDoubleValue("Icons", "fGap", _iconGap));
		_iconsInv = ini.GetBoolValue("Icons", "bShow", _iconsInv);
		_showHandLabel = ini.GetBoolValue("Icons", "bShowHandLabel", _showHandLabel);
		_handLabelSize = static_cast<float>(ini.GetDoubleValue("Icons", "fHandLabelSize", _handLabelSize));
		_handLabelGap = static_cast<float>(ini.GetDoubleValue("Icons", "fHandLabelGap", _handLabelGap));

		// Per-menu overrides (default to the inventory values if unset).
		_iconsCont = ini.GetBoolValue("IconsContainer", "bShow", _iconsInv);
		_iconXCont = static_cast<float>(ini.GetDoubleValue("IconsContainer", "fX", _iconX));
		_iconGapCont = static_cast<float>(ini.GetDoubleValue("IconsContainer", "fGap", _iconGap));
		_iconsMagic = ini.GetBoolValue("IconsMagic", "bShow", _iconsInv);
		_iconXMagic = static_cast<float>(ini.GetDoubleValue("IconsMagic", "fX", _iconX));
		_iconGapMagic = static_cast<float>(ini.GetDoubleValue("IconsMagic", "fGap", _iconGap));
		_iconsFav = ini.GetBoolValue("IconsFavorites", "bShow", _iconsInv);
		_iconXFav = static_cast<float>(ini.GetDoubleValue("IconsFavorites", "fX", _iconX));
		_iconGapFav = static_cast<float>(ini.GetDoubleValue("IconsFavorites", "fGap", _iconGap));

		logger::info("settings loaded: modifier=0x{:X} group=0x{:X} blockMenuKeys={} chords={} rememberHand={}",
			_assignModifier, _groupModifier, _blockMenuKeys, _enableChords, _rememberHand);
		logger::info("icons: show inv/cont/magic/fav={}/{}/{}/{} handLabel={} afterName={} scale={} Y={} X={}/{}/{}/{} gap={}/{}/{}/{}",
			_iconsInv, _iconsCont, _iconsMagic, _iconsFav, _showHandLabel, _iconAfterName, _iconScale, _iconY,
			_iconX, _iconXCont, _iconXMagic, _iconXFav, _iconGap, _iconGapCont, _iconGapMagic, _iconGapFav);
	}
}
