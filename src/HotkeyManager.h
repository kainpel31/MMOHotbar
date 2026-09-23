#pragma once

#include "Hotkey.h"

#include <mutex>

namespace HKS
{
	// Owns the live set of hotkeys. Pure data + lookups -- it never reads or mutates
	// game inventory/extra-data (the reference mod's UpdateHotkeys() did, and erased
	// valid binds whenever an inventory snapshot didn't line up). The only source of
	// truth is the chord -> FormID table held here and mirrored into the co-save.
	class HotkeyManager
	{
	public:
		enum class AssignResult
		{
			kAdded,     // the item is now on this chord
			kReplaced,  // it was on another chord (or this chord held something else) and moved
			kRemoved,   // same chord + same item again -> toggled off
		};

		static HotkeyManager* GetSingleton();

		// Bind `a_bind` to exactly this one item. Toggle semantics: the chord already holds
		// this item and nothing else -> remove it (kRemoved); otherwise drop whatever the
		// chord held, take the item off any other chord, and set it (kAdded / kReplaced).
		AssignResult Assign(const Bind& a_bind, const ItemId& a_id);

		// Stack the item onto whatever `a_bind` already holds, turning the chord into a
		// group that equips as a set. Already a member -> drop it from the group instead
		// (kRemoved), so the same keystroke both adds and removes. An item can only live on
		// one chord, so it is taken off its previous one (kReplaced).
		AssignResult AddToGroup(const Bind& a_bind, const ItemId& a_id);

		bool RemoveByBind(const Bind& a_bind);

		// Drop the item from whichever chord holds it; a chord left with no items goes too.
		bool RemoveByItem(const ItemId& a_id);

		// Exact identity lookup (form + ench + health all equal) across every group member.
		[[nodiscard]] const Hotkey* FindByItem(const ItemId& a_id) const;

		// Match by base form only (any instance). Used where the list only ever shows the
		// bound instance anyway (Favorites menu), so instance data isn't needed.
		[[nodiscard]] const Hotkey* FindByForm(RE::FormID a_form) const;

		[[nodiscard]] const Hotkey* FindByBind(const Bind& a_bind) const;

		// The chord holding an item, COPIED under the lock -- an invalid (empty) Bind when
		// nothing holds it. The Find* above hand back pointers into the store, which the
		// main thread prunes every few frames; these are what the plugin API needs, and
		// they copy one small vector rather than the whole table per call.
		[[nodiscard]] Bind BindOfItem(const ItemId& a_id) const;
		[[nodiscard]] Bind BindOfForm(RE::FormID a_form) const;

		// Longest-match resolution: among hotkeys whose device matches and whose whole
		// chord is currently held, return the one with the most keys (so "G" never
		// fires when "Alt+G" was pressed). Returns nullptr if nothing matches.
		//
		// a_trigger (0 = no constraint) is the just-pressed key. When set, only chords
		// that CONTAIN it are considered -- this is essential: otherwise an unrelated key
		// left in the held set (another held hotkey, or a phantom from a missed key-up on
		// alt-tab) can win resolution, and since the trigger isn't in that chord the press
		// is silently dropped. Filtering by the trigger guarantees the matched chord is
		// one the pressed key actually completes.
		[[nodiscard]] const Hotkey* ResolveChord(
			RE::INPUT_DEVICE                            a_device,
			const std::unordered_set<std::uint32_t>&    a_held,
			std::uint32_t                               a_trigger = 0) const;

		// ResolveChord, but returning a COPY of the matched chord's members. The pointer
		// form is only safe while the lock is held, and the input thread resolves chords
		// while the main thread prunes un-favorited items out of the very same vector.
		[[nodiscard]] std::vector<ItemId> ResolveChordItems(
			RE::INPUT_DEVICE                         a_device,
			const std::unordered_set<std::uint32_t>& a_held,
			std::uint32_t                            a_trigger = 0) const;

		[[nodiscard]] const std::vector<Hotkey>& GetAll() const { return _hotkeys; }

		// Locked copy, safe to iterate off-lock (the input sink and the menu/main threads
		// both mutate the store). Used by the un-favorite reconciliation.
		[[nodiscard]] std::vector<Hotkey> Snapshot() const;

		// Bulk replace (load path). Caller has already validated/resolved forms.
		void ReplaceAll(std::vector<Hotkey> a_hotkeys);
		void Clear();

	private:
		HotkeyManager() = default;
		HotkeyManager(const HotkeyManager&) = delete;
		HotkeyManager& operator=(const HotkeyManager&) = delete;

		mutable std::recursive_mutex _lock;
		std::vector<Hotkey>          _hotkeys;
	};
}
