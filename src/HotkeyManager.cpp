#include "HotkeyManager.h"

#include <algorithm>

namespace HKS
{
	HotkeyManager* HotkeyManager::GetSingleton()
	{
		static HotkeyManager singleton;
		return &singleton;
	}

	namespace
	{
		bool InBank(const Hotkey& a_hotkey, std::uint8_t a_bank)
		{
			return a_hotkey.bank == a_bank;
		}

		// One item lives on at most one chord per bank. Across banks it is independent:
		// both presets may contain the same item, and removing it globally is a separate
		// operation used when its shared favorite state is lost.
		bool DetachItem(std::vector<Hotkey>& a_hotkeys, const ItemId& a_id, std::uint8_t a_bank)
		{
			bool removed = false;
			for (auto& h : a_hotkeys) {
				if (!InBank(h, a_bank)) {
					continue;
				}
				const auto before = h.items.size();
				std::erase_if(h.items, [&](const ItemId& i) { return i.Same(a_id); });
				removed = removed || h.items.size() != before;
			}
			std::erase_if(a_hotkeys, [](const Hotkey& h) { return h.items.empty(); });
			return removed;
		}
	}

	HotkeyManager::AssignResult HotkeyManager::Assign(
		const Bind& a_bind, const ItemId& a_id, std::uint8_t a_bank)
	{
		std::scoped_lock lk(_lock);

		// Same chord, same bank, and it already holds just this item -> toggle off.
		for (auto it = _hotkeys.begin(); it != _hotkeys.end(); ++it) {
			if (InBank(*it, a_bank) && it->bind == a_bind && it->items.size() == 1 &&
				it->items.front().Same(a_id)) {
				_hotkeys.erase(it);
				return AssignResult::kRemoved;
			}
		}

		// A plain assignment replaces the chord wholesale in this bank, including a group.
		bool replacing = std::erase_if(_hotkeys, [&](const Hotkey& h) {
			return InBank(h, a_bank) && h.bind == a_bind;
		}) > 0;
		replacing = DetachItem(_hotkeys, a_id, a_bank) || replacing;

		_hotkeys.push_back(Hotkey{ a_bind, { a_id }, a_bank });
		return replacing ? AssignResult::kReplaced : AssignResult::kAdded;
	}

	HotkeyManager::AssignResult HotkeyManager::AddToGroup(
		const Bind& a_bind, const ItemId& a_id, std::uint8_t a_bank)
	{
		std::scoped_lock lk(_lock);

		for (auto it = _hotkeys.begin(); it != _hotkeys.end(); ++it) {
			if (!InBank(*it, a_bank) || it->bind != a_bind || !it->Has(a_id)) {
				continue;
			}
			std::erase_if(it->items, [&](const ItemId& i) { return i.Same(a_id); });
			if (it->items.empty()) {
				_hotkeys.erase(it);
			}
			return AssignResult::kRemoved;
		}

		const bool moved = DetachItem(_hotkeys, a_id, a_bank);
		for (auto& h : _hotkeys) {
			if (InBank(h, a_bank) && h.bind == a_bind) {
				h.items.push_back(a_id);
				return moved ? AssignResult::kReplaced : AssignResult::kAdded;
			}
		}

		_hotkeys.push_back(Hotkey{ a_bind, { a_id }, a_bank });
		return moved ? AssignResult::kReplaced : AssignResult::kAdded;
	}

	bool HotkeyManager::RemoveByBind(const Bind& a_bind, std::uint8_t a_bank)
	{
		std::scoped_lock lk(_lock);
		return std::erase_if(_hotkeys, [&](const Hotkey& h) {
			return InBank(h, a_bank) && h.bind == a_bind;
		}) > 0;
	}

	bool HotkeyManager::RemoveByItem(const ItemId& a_id)
	{
		std::scoped_lock lk(_lock);
		bool removed = false;
		for (auto& h : _hotkeys) {
			const auto before = h.items.size();
			std::erase_if(h.items, [&](const ItemId& i) { return i.Same(a_id); });
			removed = removed || h.items.size() != before;
		}
		std::erase_if(_hotkeys, [](const Hotkey& h) { return h.items.empty(); });
		return removed;
	}

	const Hotkey* HotkeyManager::FindByItem(const ItemId& a_id, std::uint8_t a_bank) const
	{
		std::scoped_lock lk(_lock);
		for (const auto& h : _hotkeys) {
			if (InBank(h, a_bank) && h.Has(a_id)) {
				return &h;
			}
		}
		return nullptr;
	}

	const Hotkey* HotkeyManager::FindByForm(RE::FormID a_form, std::uint8_t a_bank) const
	{
		std::scoped_lock lk(_lock);
		for (const auto& h : _hotkeys) {
			if (InBank(h, a_bank) && h.HasForm(a_form)) {
				return &h;
			}
		}
		return nullptr;
	}

	bool HotkeyManager::HasForm(RE::FormID a_form) const
	{
		std::scoped_lock lk(_lock);
		return std::any_of(_hotkeys.begin(), _hotkeys.end(),
			[&](const Hotkey& h) { return h.HasForm(a_form); });
	}

	const Hotkey* HotkeyManager::FindByBind(const Bind& a_bind, std::uint8_t a_bank) const
	{
		std::scoped_lock lk(_lock);
		for (const auto& h : _hotkeys) {
			if (InBank(h, a_bank) && h.bind == a_bind) {
				return &h;
			}
		}
		return nullptr;
	}

	const Hotkey* HotkeyManager::ResolveChord(
		RE::INPUT_DEVICE                         a_device,
		const std::unordered_set<std::uint32_t>& a_held,
		std::uint8_t                              a_bank,
		std::uint32_t                             a_trigger) const
	{
		std::scoped_lock lk(_lock);

		const Hotkey* best = nullptr;
		for (const auto& h : _hotkeys) {
			if (!InBank(h, a_bank) || h.bind.device != a_device) {
				continue;
			}
			if (a_trigger != 0 && !h.bind.keys.empty() &&
				std::find(h.bind.keys.begin(), h.bind.keys.end(), a_trigger) == h.bind.keys.end()) {
				continue;
			}
			if (!h.bind.IsSatisfiedBy(a_held)) {
				continue;
			}
			if (!best || h.bind.keys.size() > best->bind.keys.size()) {
				best = &h;
			}
		}
		return best;
	}

	Bind HotkeyManager::BindOfItem(const ItemId& a_id, std::uint8_t a_bank) const
	{
		std::scoped_lock lk(_lock);
		for (const auto& h : _hotkeys) {
			if (InBank(h, a_bank) && h.Has(a_id)) {
				return h.bind;
			}
		}
		return {};
	}

	Bind HotkeyManager::BindOfForm(RE::FormID a_form, std::uint8_t a_bank) const
	{
		std::scoped_lock lk(_lock);
		for (const auto& h : _hotkeys) {
			if (InBank(h, a_bank) && h.HasForm(a_form)) {
				return h.bind;
			}
		}
		return {};
	}

	std::vector<ItemId> HotkeyManager::ResolveChordItems(
		RE::INPUT_DEVICE                         a_device,
		const std::unordered_set<std::uint32_t>& a_held,
		std::uint8_t                              a_bank,
		std::uint32_t                             a_trigger) const
	{
		std::scoped_lock lk(_lock);
		const auto* hk = ResolveChord(a_device, a_held, a_bank, a_trigger);
		return hk ? hk->items : std::vector<ItemId>{};
	}

	std::vector<Hotkey> HotkeyManager::Snapshot() const
	{
		std::scoped_lock lk(_lock);
		return _hotkeys;
	}

	std::vector<Hotkey> HotkeyManager::Snapshot(std::uint8_t a_bank) const
	{
		std::scoped_lock lk(_lock);
		std::vector<Hotkey> result;
		result.reserve(_hotkeys.size());
		std::copy_if(_hotkeys.begin(), _hotkeys.end(), std::back_inserter(result),
			[&](const Hotkey& h) { return InBank(h, a_bank); });
		return result;
	}

	void HotkeyManager::ReplaceAll(std::vector<Hotkey> a_hotkeys)
	{
		std::scoped_lock lk(_lock);
		std::erase_if(a_hotkeys, [](const Hotkey& h) {
			return !h.bind.IsValid() || h.items.empty() || h.bank >= kBankCount;
		});
		_hotkeys = std::move(a_hotkeys);
	}

	void HotkeyManager::Clear()
	{
		std::scoped_lock lk(_lock);
		_hotkeys.clear();
	}
}

