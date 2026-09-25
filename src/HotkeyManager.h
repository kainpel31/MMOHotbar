#pragma once

#include "Hotkey.h"

#include <mutex>

namespace HKS
{
	// Owns the live set of hotkeys. Pure data + lookups -- it never reads or mutates
	// game inventory/extra-data (the reference mod's UpdateHotkeys() did, and erased
	// valid binds whenever an inventory snapshot didn't line up). The only source of
	// truth is the (bank, chord) -> FormID table held here and mirrored into the co-save.
	class HotkeyManager
	{
	public:
		enum class AssignResult
		{
			kAdded,     // the item is now on this chord in this bank
			kReplaced,  // it was on another chord (or this chord held something else) and moved
			kRemoved,   // same chord + same item again -> toggled off
		};

		static HotkeyManager* GetSingleton();

		// Bind `a_bind` to exactly this one item in `a_bank`. Toggle semantics: the chord
		// already holds this item and nothing else -> remove it; otherwise replace that
		// chord and move the item off any other chord in the same bank. The other preset is
		// deliberately untouched, so the same item/key may appear in both banks.
		AssignResult Assign(const Bind& a_bind, const ItemId& a_id, std::uint8_t a_bank);

		// Stack the item onto whatever `a_bind` already holds in `a_bank`, turning the chord
		// into a group that equips as a set. Already a member -> drop it from that group.
		AssignResult AddToGroup(const Bind& a_bind, const ItemId& a_id, std::uint8_t a_bank);

		bool RemoveByBind(const Bind& a_bind, std::uint8_t a_bank);

		// Remove an item from every bank. Favorite state is global to the form, so pruning
		// an un-favorited item must not leave a hidden copy behind in the other preset.
		bool RemoveByItem(const ItemId& a_id);

		// Exact identity lookup (form + ench + health) across every group member in a bank.
		[[nodiscard]] const Hotkey* FindByItem(const ItemId& a_id, std::uint8_t a_bank) const;

		// Match by base form in one bank. Used by the visible menu rows and the plugin API.
		[[nodiscard]] const Hotkey* FindByForm(RE::FormID a_form, std::uint8_t a_bank) const;

		// True when the form is bound in either bank. Used by pickup recovery, which must
		// notice a binding even when the other preset is currently visible.
		[[nodiscard]] bool HasForm(RE::FormID a_form) const;

		[[nodiscard]] const Hotkey* FindByBind(const Bind& a_bind, std::uint8_t a_bank) const;

		// Locked copies for callers on the plugin boundary. An empty Bind means unbound.
		[[nodiscard]] Bind BindOfItem(const ItemId& a_id, std::uint8_t a_bank) const;
		[[nodiscard]] Bind BindOfForm(RE::FormID a_form, std::uint8_t a_bank) const;

		// Longest-match resolution within one bank. The just-pressed key, when non-zero,
		// must be part of the chord so an unrelated held key cannot shadow the press.
		[[nodiscard]] const Hotkey* ResolveChord(
			RE::INPUT_DEVICE                         a_device,
			const std::unordered_set<std::uint32_t>& a_held,
			std::uint8_t                              a_bank,
			std::uint32_t                             a_trigger = 0) const;

		// ResolveChord, returning a copy that is safe after the store lock is released.
		[[nodiscard]] std::vector<ItemId> ResolveChordItems(
			RE::INPUT_DEVICE                         a_device,
			const std::unordered_set<std::uint32_t>& a_held,
			std::uint8_t                              a_bank,
			std::uint32_t                             a_trigger = 0) const;

		// Locked copies. Snapshot() returns both banks for save/pruning; Snapshot(bank)
		// preserves assignment order and is what the visible 12-slot view consumes.
		[[nodiscard]] std::vector<Hotkey> Snapshot() const;
		[[nodiscard]] std::vector<Hotkey> Snapshot(std::uint8_t a_bank) const;

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
