#pragma once

// GENERATED FILE -- do not edit by hand.
// Written by tools/build_hud_swf.py from "Asset dont push to git"/<category>/*.png.
// Re-run `python tools/build_hud_swf.py` after adding or renaming an icon.
//
// Maps an item's EditorID to a frame in the icon sheet that
// dist/Interface/MMOHotbar/Hotbar.swf carries. The C++ side normalizes the form's
// EditorID with Normalize() below and binary-searches kTable; the frame number is
// handed to the SWF's per-slot icon clip with gotoAndStop().
//
// Why EditorID and not the display name: GetName() is localized, so an iron sword
// is "Iron Sword" in English and "Железный меч" in Russian, and a name-keyed table
// would only ever match on an English install. EditorIDs are not localized.
//
// Why the table is generated rather than a runtime directory scan: the icon PNGs are
// deliberately untracked (see .gitignore), so they are not on disk where the game
// runs. Baking the art into the SWF and the index into this header keeps the whole
// icon set inside the one file the installer already ships.
#include <cstdint>
#include <string_view>

namespace MMO
{
	namespace Icons
	{
		// Frame 1 of the sheet is blank; icon i is frame 2 + i. A slot with no bound
		// item, or one whose EditorID is not in the table, shows frame 1.
		inline constexpr std::uint16_t kBlankFrame = 1;
		inline constexpr std::uint16_t kFirstIconFrame = 2;

		inline constexpr std::uint16_t kFrameCount = 0;  // filled in by the generator
		inline constexpr std::size_t   kTableSize = 0;   // filled in by the generator

		// Sorted by `key` so FrameFor() can binary-search it. Generated in C++ order
		// by Python's `sorted()`, which is the same ordering for the ASCII keys here.
		struct Entry
		{
			std::string_view key;
			std::uint16_t    frame;
		};

		inline constexpr Entry kTable[] = {};

		// Lowercase and drop every non-alphanumeric character: "IronSword" and
		// "iron sword" both become "ironsword".
		//
		// MUST stay identical to tools/iconsheet.py normalize(). The table is keyed by
		// whatever that function produced, so a divergence here is a table that
		// silently stops matching. std::tolower is avoided on purpose: it is locale
		// dependent and takes an int, and the input is UTF-8 from the game.
		[[nodiscard]] constexpr std::string_view Raw(std::string_view a_text)
		{
			return a_text;
		}

		// Frame for a form's EditorID, or kBlankFrame when the item is not in the
		// sheet. kTable is sorted, so this is a plain binary search.
		[[nodiscard]] constexpr std::uint16_t FrameFor(std::string_view a_key)
		{
			std::size_t lo = 0;
			std::size_t hi = kTableSize;
			while (lo < hi) {
				const std::size_t mid = lo + (hi - lo) / 2;
				if (kTable[mid].key < a_key) {
					lo = mid + 1;
				}
				else {
					hi = mid;
				}
			}
			return (lo < kTableSize && kTable[lo].key == a_key) ? kTable[lo].frame : kBlankFrame;
		}
	} // namespace Icons
} // namespace MMO
