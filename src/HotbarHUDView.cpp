// This file is part of MMOHotbar, a full fork of STB Hotkey System.
// SPDX-License-Identifier: GPL-3.0-only
#include "HotbarHUDView.h"

#include <atomic>
#include <memory>

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

		// The SWF's own geometry: one cell per slot, and the bar is 12 cells wide
		// whatever [Hotbar] iVisibleSlots says -- the unused cells are hidden, not
		// rebuilt, so changing the slot count needs no re-run of the SWF builder.
		// This must stay in step with SLOT_PX in tools/build_hud_swf.py.
		constexpr double kSlotPx = 64.0;

		// Item icons come from the PLAYER'S OWN UI FILES. An icon sheet that ships with
		// Skyrim or a UI overhaul is loaded at run time through the very same loadMovie()
		// call as our own bar below: nothing is copied into this repository, nothing is
		// redistributed, and a player without one of these just keeps the frames and
		// keycaps. Tried in order; the first that loads wins.
		constexpr const char* kIconSheets[] = {
			"Interface/SkyUI/IconsItem_PsychoSweve.swf",
			"Interface/IconsItem_PsychoSweve.swf",
		};

		// The sheet's frames are labelled by what they picture ("weapon_greatsword",
		// "armor_head", ...), so we address them by label rather than by number. Frame 1
		// carries no label and is the sheet's blank one, which is what an unbound or
		// unmapped slot shows.
		constexpr const char* kBlankIconFrame = "1";

		[[nodiscard]] std::string MemberName(const char* a_prefix, std::size_t a_index)
		{
			return std::string(a_prefix) + std::to_string(a_index);
		}

		// Set once the movie is attached, so a failure logs exactly one line instead of
		// one per frame for the rest of the session.
		bool g_attached = false;
		bool g_failed = false;

		// Attach retry brake. Attaching loads movies, so doing it on consecutive frames
		// while the HUD is still coming up will stall the game hard; these make the frame
		// path give up for a while and keep a count so a persistent failure is reported
		// once every ten tries instead of silently forever.
		constexpr std::uint32_t kAttachCooldownFrames = 180;  // ~3 s at 60 FPS
		std::uint32_t           g_attachCooldown = 0;
		std::uint32_t           g_attachFailures = 0;

		// The attached clip, held by pointer rather than by value. It is owned by the HUD
		// movie's display list, so it is only ever touched from the HUD thread that is
		// advancing that very movie.
		//
		// The indirection is what makes both teardown paths safe. A GFxValue holding a
		// display object releases through the MOVIE's object interface when it is
		// destroyed, so a value we can no longer trust to have a live movie must never be
		// overwritten or cleared: that release would write into freed movie state.
		// Detach() (movie alive) deletes it; Forget() (movie gone) abandons the 16-byte
		// wrapper with release(), so the dead display object is never touched again.
		std::unique_ptr<RE::GFxValue> g_clip;

		// The movie g_clip belongs to. The game tears the HUD down and builds a new one
		// on a save load / new game, which would leave g_clip pointing into a freed
		// display list -- writing to it after that is a use-after-free, not a cosmetic
		// bug. Comparing the live movie against this is the last of three signals that a
		// rebuild happened (the HUD menu events are the other two, and neither of those
		// looks at an address).
		RE::GFxMovieView* g_attachedMovie = nullptr;

		// The labels currently on screen, so we only write a slot whose key actually
		// changed. SetText on twelve fields every frame is wasted work and makes the
		// AS text engine re-lay-out the HUD for nothing.
		std::array<std::string, HotbarHUD::kSlotCount> g_labels{};
		bool g_labelsValid = false;

		// Same diffing for the icon clips, and whether the player's sheet loaded at all
		// -- when it did not there is nothing to drive and no point asking.
		std::array<std::string, HotbarHUD::kSlotCount> g_iconLabels{};
		bool                                           g_iconsValid = false;
		bool                                           g_iconsLoaded = false;
		// Written from input / load code, read on the HUD frame: atomic, not a plain bool.
		std::atomic<bool> g_dirty{ true };

		// Last visibility pushed to Scaleform, so _visible is only written on change
		// instead of one cross-call into the movie every frame.
		bool g_visible = false;
		bool g_visibleKnown = false;

		// FiringSuppressed() does a dozen menu-name lookups; the HUD only needs it a few
		// times a second, not every frame. Atomic because a menu open/close can bump it
		// from the event thread to have the next frame re-read instead of waiting.
		std::atomic<std::uint32_t> g_hiddenTick{ 0 };
		bool                      g_hidden = false;

	} // namespace

	void HotbarHUDView::Detach()
	{
		// The movie that owns this clip is still alive here -- that is the contract, and
		// the reason this runs from an event rather than from a frame.
		g_clip.reset();
		Forget();
	}

	void HotbarHUDView::Forget()
	{
		g_attached = false;
		g_failed = false;
		g_attachedMovie = nullptr;
		g_labelsValid = false;
		g_iconsValid = false;
		g_iconsLoaded = false;
		g_visibleKnown = false;
		g_attachCooldown = 0;
		g_attachFailures = 0;
		g_hiddenTick.store(0, std::memory_order_relaxed);
		g_dirty = true;
		// Abandoned, not destroyed: the display object died with its movie, and the
		// GFxValue destructor would reach back into it. One 16-byte wrapper per HUD
		// rebuild, on a path that only runs when we were not told about the rebuild.
		static_cast<void>(g_clip.release());
	}

	void HotbarHUDView::RefreshVisibility()
	{
		g_hiddenTick.store(0, std::memory_order_relaxed);
	}

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
			// A different address is what tells us here, and it is the weakest of the
			// three signals: an allocator free to hand the new movie the freed one's
			// address slips straight past it. The HUD menu close/open events do not look
			// at addresses at all, so the clip is expected to be gone by now and this is
			// only the safety net -- which is why it must not touch the clip.
			logger::info("hotbar HUD: movie was rebuilt, re-attaching");
			Forget();
		}

		// Attach loads movies on the frame path. A failure that does not latch g_failed
		// -- the "movie still loading" case below is the only one -- would otherwise redo
		// that work EVERY FRAME, which is enough to make the game unplayable. So: back off,
		// and say so loudly if it keeps happening rather than spinning quietly forever.
		if (!g_attached) {
			if (g_attachCooldown > 0) {
				--g_attachCooldown;
				return;
			}
			if (!Attach(a_this)) {
				g_attachCooldown = kAttachCooldownFrames;
				if (++g_attachFailures >= 10 && g_attachFailures % 10 == 0) {
					logger::warn("hotbar HUD: still not attached after {} attempts -- the bar "
					             "stays hidden until the HUD movie appears",
					             g_attachFailures);
				}
				return;
			}
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

		// A leftover clip from an earlier attach on this same (live) movie would leave a
		// second bar on screen once we create another under the same name.
		{
			RE::GFxValue stale;
			if (root.GetMember(kClipName, &stale) && stale.IsObject()) {
				stale.Invoke("removeMovieClip");
			}
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

		// Anchor to the bottom-centre of the VISIBLE stage. AS2 _xscale/_yscale are
		// percentages (100 = 1:1). The HUD root's origin is its top-left, not its
		// bottom-centre, so read the real visible rect and fall back to the stock
		// 1280x720 stage if a UI overhaul does not expose it.
		double stageX = 0.0;
		double stageY = 0.0;
		double stageW = 1280.0;
		double stageH = 720.0;
		{
			RE::GFxValue rect;
			RE::GFxValue vx, vy, vw, vh;
			if (movie->GetVariable(&rect, "Stage.visibleRect") && rect.IsObject() &&
				rect.GetMember("x", &vx) && rect.GetMember("y", &vy) &&
				rect.GetMember("width", &vw) && rect.GetMember("height", &vh) &&
				vx.IsNumber() && vy.IsNumber() && vw.IsNumber() && vh.IsNumber() &&
				vw.GetNumber() > 0.0 && vh.GetNumber() > 0.0) {
				stageX = vx.GetNumber();
				stageY = vy.GetNumber();
				stageW = vw.GetNumber();
				stageH = vh.GetNumber();
			}
		}
		// Where and how big, from [Hotbar]. Anchored by the DRAWN bottom-centre: the
		// numbers are read against the visible stage, so they mean the same thing at
		// 16:9 and ultrawide, and changing the scale grows the bar around that anchor
		// instead of walking it across the screen. AS2 _xscale/_yscale are percentages.
		const std::size_t visible = HotbarHUD::VisibleSlotCount();
		const double      scale = static_cast<double>(HotbarHUD::BarScalePercent()) / 100.0;
		const double      drawnW = static_cast<double>(visible) * kSlotPx * scale;
		const double      drawnH = kSlotPx * scale;

		clip.SetMember("_xscale", RE::GFxValue{ scale * 100.0 });
		clip.SetMember("_yscale", RE::GFxValue{ scale * 100.0 });
		clip.SetMember("_x", RE::GFxValue{ stageX + stageW / 2.0 + static_cast<double>(HotbarHUD::BarOffsetX()) -
		                                   drawnW / 2.0 });
		clip.SetMember("_y", RE::GFxValue{ stageY + stageH - static_cast<double>(HotbarHUD::BarOffsetY()) -
		                                   drawnH });

		// Slots past iVisibleSlots are hidden rather than removed: the movie still has
		// them, they are just not on screen and not fireable (HotbarHUD::ExecuteSlot
		// refuses them too, so what you see and what works stay the same thing).
		for (std::size_t i = visible; i < HotbarHUD::kSlotCount; ++i) {
			for (const char* part : {"frame", "icon", "key"}) {
				RE::GFxValue slot;
				if (clip.GetMember(MemberName(part, i).c_str(), &slot) && slot.IsDisplayObject()) {
					slot.SetMember("_visible", RE::GFxValue{ false });
				}
			}
		}

		// Item icons: load the player's own icon sheet into each slot's empty icon<i>
		// clip. One loadMovie per slot, not one shared clip, because each slot drives
		// its own frame on its own timeline; the game caches the SWF resource, so the
		// eleven after the first are cheap. sendProgress = 1 (true) again, so the frames
		// exist by the time Attach returns. Gated on bShowItemIcons, which is OFF by
		// default: see HotbarHUD::ItemIconsEnabled.
		if (HotbarHUD::ItemIconsEnabled()) {
			RE::GFxValue sheetArgs[2];
			sheetArgs[1] = 1.0;
			g_iconsLoaded = false;
			for (const auto* sheet : kIconSheets) {
				sheetArgs[0] = sheet;
				bool anyLoaded = false;
				for (std::size_t i = 0; i < HotbarHUD::kSlotCount; ++i) {
					RE::GFxValue iconClip;
					if (!clip.GetMember(MemberName("icon", i).c_str(), &iconClip) ||
						!iconClip.IsDisplayObject()) {
						continue;
					}
					anyLoaded = iconClip.Invoke("loadMovie", nullptr, sheetArgs, 2) || anyLoaded;
				}
				if (anyLoaded) {
					g_iconsLoaded = true;
					logger::info("hotbar HUD: item icons from '{}'", sheet);
					break;
				}
			}
			if (!g_iconsLoaded) {
				// Not an error: no UI overhaul ships the sheet, and the bar is complete
				// without it. Said once, at attach, and then never mentioned again.
				logger::info("hotbar HUD: no item icon sheet found -- slots show frames and keys only");
			}
		}

		g_clip = std::make_unique<RE::GFxValue>(clip);
		g_attachedMovie = movie;
		g_labelsValid = false;
		g_visibleKnown = false;
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
		// Defensive: the only caller attaches first, but every write below goes through
		// this pointer into a movie.
		if (!g_clip) {
			return;
		}

		// Re-evaluated every 4th frame (~15 Hz at 60 FPS); imperceptible for a show/hide,
		// and a menu open/close pulls the next frame forward instead of waiting it out.
		if ((g_hiddenTick.fetch_add(1, std::memory_order_relaxed) & 3U) == 0U) {
			g_hidden = HKS::InputHandler::FiringSuppressed();
		}
		const bool hidden = g_hidden;

		if (!g_labelsValid || g_dirty) {
			const auto slots = HotbarHUD::Snapshot();

			for (std::size_t i = 0; i < HotbarHUD::kSlotCount; ++i) {
				// Icon first, and on its own diff: the key's early-out below must not
				// skip it, because a slot can keep its key while its item changes.
				if (g_iconsLoaded) {
					std::string icon;
					if (!slots[i].items.empty()) {
						icon = HotbarHUD::ItemIconLabel(slots[i].items.front());
					}

					if (!g_iconsValid || icon != g_iconLabels[i]) {
						g_iconLabels[i] = icon;
						RE::GFxValue iconClip;
						if (g_clip->GetMember(MemberName("icon", i).c_str(), &iconClip) &&
							iconClip.IsDisplayObject()) {
							// Addressed by label: the sheet names its own frames, so the
							// mapping in HotbarHUD::ItemIconLabel reads like the art
							// instead of a wall of magic numbers. A label the sheet does
							// not have simply leaves the frame where it is; an unbound or
							// unmapped slot goes to the blank one.
							iconClip.GotoAndStop(icon.empty() ? kBlankIconFrame : icon.c_str());
						}
					}
				}

				// SetText on twelve fields every frame is wasted work and makes the AS
				// text engine re-lay-out the HUD for nothing, so only write what changed.
				if (g_labelsValid && slots[i].label == g_labels[i]) {
					continue;
				}
				g_labels[i] = slots[i].label;

				RE::GFxValue key;
				if (g_clip->GetMember(MemberName("key", i).c_str(), &key) && key.IsDisplayObject()) {
					// Written for unbound slots as well, and that is not cosmetic. Snapshot()
					// reports a free slot's label as its own number, which is exactly the
					// placeholder the SWF was built with, so this restores it. Skipping the
					// write would leave the last chord's keycap sitting on a slot that no
					// longer has one -- reassigning the same item toggles the bind off, and
					// an un-favorited item loses it too.
					key.SetText(slots[i].label.c_str());
				}
			}

			g_labelsValid = true;
			// Only "valid" when there was a sheet to be valid about: with none loaded we
			// keep it false so the block runs again if one ever appears.
			g_iconsValid = g_iconsLoaded;
			g_dirty = false;
		}

		if (!g_visibleKnown || g_visible == hidden) {
			g_visible = !hidden;
			g_visibleKnown = true;
			g_clip->SetMember("_visible", RE::GFxValue{ g_visible });
		}
	}
} // namespace MMO