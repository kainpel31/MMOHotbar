#include "Localization.h"

#include <algorithm>
#include <cstdio>
#include <unordered_map>
#include <vector>

namespace HKS::Localization
{
	namespace
	{
		// Built-in English. Also the authoritative list of tokens -- ship a translation
		// file with these keys to localize the mod.
		const std::unordered_map<std::string, std::string>& Defaults()
		{
			static const std::unordered_map<std::string, std::string> kDefaults{
				// --- assign-modifier collides with the Favorites-menu key ---
				{ "$STB_HK_ModifierConflict_Body",
					"STB Hotkey System: the hotkey-assign modifier ({0}) is on the same key that "
					"opens the Favorites menu.\n\n"
					"They cannot share a key -- chord capture and opening/closing the menu fight "
					"over the same press, and the menu misbehaves.\n\n"
					"Pick another modifier (it will be saved to the INI):" },
				{ "$STB_HK_ModifierConflict_Use", "Use {0}" },
				{ "$STB_HK_ModifierConflict_Keep", "Leave it as is" },

				// --- the key being bound is already a gameplay control ---
				{ "$STB_HK_KeyConflict_Body",
					"{0} is already bound to \"{1}\".\n\n"
					"Press it in game and both will fire. Assign the hotkey anyway?" },
				{ "$STB_HK_KeyConflict_Assign", "Assign anyway" },
				{ "$STB_HK_KeyConflict_Cancel", "Cancel" },

				// --- button hints along the bottom of the item menus ---
				{ "$STB_HK_Hint_Assign", "Assign hotkey" },
				{ "$STB_HK_Hint_Group", "Add to group" },

				// --- hand markers drawn after the keycap ---
				{ "$STB_HK_Hand_Right", "R" },
				{ "$STB_HK_Hand_Left", "L" },
			};
			return kDefaults;
		}

		std::unordered_map<std::string, std::string> g_strings;

		std::string Utf16ToUtf8(const std::vector<char>& a_raw, std::size_t a_offset)
		{
			std::string out;
			out.reserve(a_raw.size());
			const auto  count = (a_raw.size() - a_offset) / 2;
			const auto* u16 = reinterpret_cast<const unsigned char*>(a_raw.data() + a_offset);
			for (std::size_t i = 0; i < count; ++i) {
				std::uint32_t cp = static_cast<std::uint32_t>(u16[i * 2]) |
				                   (static_cast<std::uint32_t>(u16[i * 2 + 1]) << 8);
				// surrogate pair
				if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < count) {
					const std::uint32_t lo = static_cast<std::uint32_t>(u16[(i + 1) * 2]) |
					                         (static_cast<std::uint32_t>(u16[(i + 1) * 2 + 1]) << 8);
					if (lo >= 0xDC00 && lo <= 0xDFFF) {
						cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
						++i;
					}
				}
				if (cp < 0x80) {
					out.push_back(static_cast<char>(cp));
				} else if (cp < 0x800) {
					out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
					out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
				} else if (cp < 0x10000) {
					out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
					out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
					out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
				} else {
					out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
					out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
					out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
					out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
				}
			}
			return out;
		}

		bool ReadFile(const std::string& a_path, std::string& a_text)
		{
			std::FILE* f = nullptr;
			if (fopen_s(&f, a_path.c_str(), "rb") != 0 || !f) {
				return false;
			}
			std::vector<char> raw;
			char              buf[4096];
			std::size_t       n = 0;
			while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
				raw.insert(raw.end(), buf, buf + n);
			}
			std::fclose(f);
			if (raw.empty()) {
				return false;
			}

			const auto* u = reinterpret_cast<const unsigned char*>(raw.data());
			if (raw.size() >= 2 && u[0] == 0xFF && u[1] == 0xFE) {
				a_text = Utf16ToUtf8(raw, 2);  // UTF-16 LE
			} else if (raw.size() >= 3 && u[0] == 0xEF && u[1] == 0xBB && u[2] == 0xBF) {
				a_text.assign(raw.begin() + 3, raw.end());  // UTF-8 BOM
			} else {
				a_text.assign(raw.begin(), raw.end());
			}
			return true;
		}

		void Parse(const std::string& a_text)
		{
			std::size_t pos = 0;
			while (pos <= a_text.size()) {
				auto eol = a_text.find('\n', pos);
				if (eol == std::string::npos) {
					eol = a_text.size();
				}
				std::string line = a_text.substr(pos, eol - pos);
				pos = eol + 1;

				while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
					line.pop_back();
				}
				if (line.empty() || line[0] != '$') {
					continue;  // comments and blanks: a token line always starts with '$'
				}
				const auto tab = line.find('\t');
				if (tab == std::string::npos) {
					continue;
				}
				std::string key = line.substr(0, tab);
				std::string val = line.substr(tab + 1);
				while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) {
					val.erase(val.begin());
				}
				if (!key.empty() && !val.empty()) {
					// \n in the file means a real line break in the message box.
					std::string unescaped;
					for (std::size_t i = 0; i < val.size(); ++i) {
						if (val[i] == '\\' && i + 1 < val.size() && val[i + 1] == 'n') {
							unescaped.push_back('\n');
							++i;
						} else {
							unescaped.push_back(val[i]);
						}
					}
					g_strings[std::move(key)] = std::move(unescaped);
				}
			}
		}

		std::string GameLanguage()
		{
			if (auto* setting = RE::GetINISetting("sLanguage:General")) {
				if (const char* s = setting->GetString(); s && *s) {
					std::string lang(s);
					std::transform(lang.begin(), lang.end(), lang.begin(),
						[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					return lang;
				}
			}
			return "english";
		}
	}

	void Load()
	{
		g_strings.clear();

		const std::string lang = GameLanguage();
		const std::string base = "Data/Interface/STB_HotkeySystem_";

		std::string text;
		std::string used;
		if (lang != "english" && ReadFile(base + lang + ".txt", text)) {
			used = lang;
		} else if (ReadFile(base + "english.txt", text)) {
			used = "english";
		}

		if (!used.empty()) {
			Parse(text);
			logger::info("localization: loaded {} ({} strings)", used, g_strings.size());
		} else {
			logger::info("localization: no file for '{}', using built-in English", lang);
		}
	}

	std::string Get(std::string_view a_token)
	{
		const std::string key{ a_token };
		if (const auto it = g_strings.find(key); it != g_strings.end()) {
			return it->second;
		}
		const auto& def = Defaults();
		if (const auto it = def.find(key); it != def.end()) {
			return it->second;
		}
		return key;
	}

	std::string Format(std::string_view a_token, std::initializer_list<std::string_view> a_args)
	{
		std::string out = Get(a_token);
		std::size_t idx = 0;
		for (const auto& arg : a_args) {
			const std::string ph = "{" + std::to_string(idx++) + "}";
			for (std::size_t at = out.find(ph); at != std::string::npos; at = out.find(ph, at + arg.size())) {
				out.replace(at, ph.size(), arg);
			}
		}
		return out;
	}
}
