#pragma once

#include <initializer_list>
#include <string>
#include <string_view>

namespace HKS::Localization
{
	// UI strings, loaded from Data/Interface/STB_HotkeySystem_<language>.txt.
	//
	// Same shape as the game's own Translate_*.txt: one entry per line, `$TOKEN`, a TAB,
	// then the text. UTF-16 LE (what the CK and vanilla files use) and UTF-8 are both
	// accepted. The game's Scaleform translator can't help us here -- it only substitutes
	// a `$TOKEN` that makes up a whole string, and our messages have to interpolate a key
	// name and a control name -- so we read the file ourselves and do the substitution.
	//
	// Every token has a built-in English default, so a missing or partial file is fine.

	// Reads sLanguage:General and loads the matching file, falling back to english.
	// Safe to call again (e.g. after a settings reload).
	void Load();

	// Translated text for a_token, or the built-in default, or a_token itself.
	[[nodiscard]] std::string Get(std::string_view a_token);

	// Get() with `{0}`, `{1}`, ... replaced by a_args in order.
	[[nodiscard]] std::string Format(std::string_view a_token, std::initializer_list<std::string_view> a_args);
}
