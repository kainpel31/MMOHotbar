// This file is part of MMOHotbar, a full fork of STB Hotkey System.
// SPDX-License-Identifier: GPL-3.0-only
#include "HotbarHUDView.h"

#include "HotbarHUD.h"
#include "InputHandler.h"

namespace MMO
{
	namespace
	{
		// Built by tools/build_hud_swf.py. The path is what the game's Scaleform
		// resolver expects for an Interface/ subfolder movie.
		constexpr const char* kHudMovie = "Interface/MMOHotbar/Hotbar.swf";
		constexpr const char* kClipName = "MMOHotbar";

		// The SWF's own stage: 12 slots of SLOT_PX, laid out along the bottom edge.
		// These must match the constants in tools/build_hud_swf.py; they are what the
		// clip is positioned by, since a loaded movie keeps the stage size it was
		// published with and the HUD draws it 1:1.
		constexpr double kBarWidth = 12.0 * 64.0;
		constexpr double kBarHeight = 64.0;
		constexpr double kBottomMargin = 96.0;

		// Set once the movie is attached, so a failure logs exactly one line instead of
		// one per frame for the rest of the session.
		bool g_attached = false;
		bool g_failed = false;

		// The attached clip. It is owned by the HUD movie's display list, so it is only
		// ever touched from the HUD thread that is advancing that very movie.
		RE::GFxValue g_clip;

		// The movie g_clip belongs to. The game tears the HUD down and builds a new one
		// on a save load / new game, which would leave g_clip pointing into a freed
		// display list -- writing to it after that is a use-after-free, not a cosmetic
		// bug. Comparing the live movie against this is what tells us to re-attach.
		RE::GFxMovieView* g_attachedMovie = nullptr;

		// The labels currently on screen, so we only write a slot whose key actually
		// changed. SetText on twelve fields every frame is wasted work and makes the
		// AS text engine re-lay-out the HUD for nothing.
		std::array<std::string, HotbarHUD::kSlotCount> g_labels{};
		bool g_labelsValid = false;
		bool g_dirty = true;

		void Detach()
		{
			g_attached = false;
			g_attachedMovie = nullptr;
			g_labelsValid = false;
			g_clip = RE::GFxValue{};
		}
	} // namespace

	void HotbarHUDView::Install()
	{
		// AdvanceMovie is IMenu vfunc 0x5 (see RE/IMenu.h); the plugin's other menu
		// hooks write the same slot, so this is the established way in.
		REL::Relocation<std::uintptr_t> v{ RE::VTABLE_HUDMenu[0] };
		_AdvanceHud = v.write_vfunc(0x5, &HotbarHUDView::AdvanceHud);
		logger::info("hotbar HUD hook installed ({} slots)", HotbarHUD::kSlotCount);
	}

	void HotbarHUDView::MarkDirty()
	{
		g_dirty = true;
	}

	void HotbarHUDView::AdvanceHud(RE::HUDMenu* a_this, float a_interval, std::uint32_t a_currentTime)
	{
		// Vanilla first: our clip is a child of this movie's display list, and the HUD may
		// rebuild that list under us. Relocation has no bool conversion (get() asserts on a
		// null address), so the guard is on the raw address.
		if (_AdvanceHud.address() != 0) {
			_AdvanceHud(a_this, a_interval, a_currentTime);
		}

		if (g_failed) {
			return;
		}

		// A different movie than the one we attached to means the game rebuilt the HUD
		// (save load / new game) and our clip went with the old one. Drop it before
		// touching anything, then attach again on this movie.
		auto* movie = a_this->uiMovie.get();
		if (g_attached && movie != g_attachedMovie) {
			logger::info("hotbar HUD: movie was rebuilt, re-attaching");
			Detach();
		}

		if (!g_attached && !Attach(a_this)) {
			return;
		}
		Update();
	}

	bool HotbarHUDView::Attach(RE::HUDMenu* a_menu)
	{
		auto* movie = a_menu->uiMovie.get();
		if (!movie) {
			return false;  // still loading; try again next frame
		}

		RE::GFxValue root;
		if (!movie->GetVariable(&root, "_root") || !root.IsObject()) {
			// A UI overhaul that replaced the HUD movie root. Nothing to attach to, and
			// retrying will not change the answer, so stop rather than log every frame.
			g_failed = true;
			logger::warn("hotbar HUD: HUD movie has no _root -- bar not shown");
			return false;
		}

		// A fresh empty clip at the top of the display list, the standard Scaleform
		// sequence: getNextHighestDepth() keeps us above whatever the HUD has added.
		RE::GFxValue depth;
		if (!root.Invoke("getNextHighestDepth", &depth) || !depth.IsNumber()) {
			g_failed = true;
			logger::warn("hotbar HUD: could not read the next free depth -- bar not shown");
			return false;
		}

		RE::GFxValue clip;
		if (!root.CreateEmptyMovieClip(&clip, kClipName, static_cast<std::int32_t>(depth.GetNumber())) ||
			!clip.IsObject()) {
			g_failed = true;
			logger::warn("hotbar HUD: could not create the bar clip -- bar not shown");
			return false;
		}

		// loadMovie pulls the movie in; sendProgress = true (1) makes it synchronous, so
		// the named display objects exist by the time Attach returns.
		RE::GFxValue args[2];
		args[0] = kHudMovie;
		args[1] = 1.0;
		if (!clip.Invoke("loadMovie", nullptr, args, 2)) {
			g_failed = true;
			logger::warn("hotbar HUD: could not load '{}' -- is it installed? "
			             "Bar not shown", kHudMovie);
			return false;
		}

		// Anchor to the bottom-centre. The clip sits in the HUD movie's coordinate
		// space, whose origin is the safe area, so x is centred on it and y is measured
		// up from the bottom edge.
		clip.SetMember("_xscale", RE::GFxValue{ 1.0 });
		clip.SetMember("_yscale", RE::GFxValue{ 1.0 });
		clip.SetMember("_x", RE::GFxValue{ -kBarWidth / 2.0 });
		clip.SetMember("_y", RE::GFxValue{ -kBottomMargin });

		g_clip = clip;
		g_attachedMovie = movie;
		g_labelsValid = false;
		g_dirty = true;
		g_attached = true;
		logger::info("hotbar HUD attached ({} slots, '{}')", HotbarHUD::kSlotCount, kHudMovie);
		return true;
	}

	void HotbarHUDView::Update()
	{
		// Never draw over a menu: the assign flow and the bottom-bar hints are read
		// against the inventory, and a bar across it is just in the way. FiringSuppressed
		// already encodes "a menu owns the screen" for both the paused and the
		// SkyrimSoulsRE-unpaused case, so the bar follows the same rule the chords do.
		const bool hidden = HKS::InputHandler::FiringSuppressed();

		if (!g_labelsValid || g_dirty) {
			const auto slots = HotbarHUD::Snapshot();

			for (std::size_t i = 0; i < HotbarHUD::kSlotCount; ++i) {
				// SetText on twelve fields every frame is wasted work and makes the AS
				// text engine re-lay-out the HUD for nothing, so only write what changed.
				if (g_labelsValid && slots[i].label == g_labels[i]) {
					continue;
				}
				g_labels[i] = slots[i].label;

				RE::GFxValue key;
				if (g_clip.GetMember((std::string("key") + std::to_string(i)).c_str(), &key) &&
					key.IsDisplayObject()) {
					// An unbound slot keeps the SWF's own placeholder (its position), so
					// an empty bar still shows its shape instead of a row of blanks.
					if (slots[i].bound) {
						key.SetText(slots[i].label.c_str());
					}
				}
			}

			g_labelsValid = true;
			g_dirty = false;
		}

		g_clip.SetMember("_visible", RE::GFxValue{ !hidden });
	}
} // namespace MMO