#include "HotkeyManager.h"

namespace HKS
{
	HotkeyManager* HotkeyManager::GetSingleton()
	{
		static HotkeyManager singleton;
		return &singleton;
	}

	namespace
	{
		// Take the item off every chord that holds it, dropping any chord left empty.
		// One item lives on exactly one chord: two keycaps on one row would be ambiguous,
		// and "which key does this equip" has to have a single answer.
		bool DetachItem(std::vector<Hotkey>& a_hotkeys, const ItemId& a_id)
		{
			bool removed = false;
			for (auto& h : a_hotkeys) {
				const auto before = h.items.size();
				std::erase_if(h.items, [&](const ItemId& i) { return i.Same(a_id); });
				removed = removed || h.items.size() != before;
			}
			std::erase_if(a_hotkeys, [](const Hotkey& h) { return h.items.empty(); });
			return removed;
		}
	}

	HotkeyManager::AssignResult HotkeyManager::Assign(const Bind& a_bind, const ItemId& a_id)
	{
		std::scoped_lock lk(_lock);

		// Same chord, and it already holds just this item -> toggle off.
		for (auto it = _hotkeys.begin(); it != _hotkeys.end(); ++it) {
			if (it->bind == a_bind && it->items.size() == 1 && it->items.front().Same(a_id)) {
				_hotkeys.erase(it);
				return AssignResult::kRemoved;
			}
		}

		// A plain assignment replaces the chord wholesale -- including a group that was
		// built on it. That is the escape hatch: Ctrl+K on one item resets K to that item.
		bool replacing = std::erase_if(_hotkeys, [&](const Hotkey& h) { return h.bind == a_bind; }) > 0;
		replacing = DetachItem(_hotkeys, a_id) || replacing;

		_hotkeys.push_back(Hotkey{ a_bind, { a_id } });
		return replacing ? AssignResult::kReplaced : AssignResult::kAdded;
	}

	HotkeyManager::AssignResult HotkeyManager::AddToGroup(const Bind& a_bind, const ItemId& a_id)
	{
		std::scoped_lock lk(_lock);

		for (auto it = _hotkeys.begin(); it != _hotkeys.end(); ++it) {
			if (it->bind != a_bind || !it->Has(a_id)) {
				continue;
			}
			// Already a member -> the same keystroke takes it back out.
			std::erase_if(it->items, [&](const ItemId& i) { return i.Same(a_id); });
			if (it->items.empty()) {
				_hotkeys.erase(it);
			}
			return AssignResult::kRemoved;
		}

		const bool moved = DetachItem(_hotkeys, a_id);

		// DetachItem may have deleted the target chord (if the item was its only member),
		// so look it up again rather than caching the iterator.
		for (auto& h : _hotkeys) {
			if (h.bind == a_bind) {
				h.items.push_back(a_id);
				return moved ? AssignResult::kReplaced : AssignResult::kAdded;
			}
		}

		_hotkeys.push_back(Hotkey{ a_bind, { a_id } });
		return moved ? AssignResult::kReplaced : AssignResult::kAdded;
	}

	bool HotkeyManager::RemoveByBind(const Bind& a_bind)
	{
		std::scoped_lock lk(_lock);
		return std::erase_if(_hotkeys, [&](const Hotkey& h) { return h.bind == a_bind; }) > 0;
	}

	bool HotkeyManager::RemoveByItem(const ItemId& a_id)
	{
		std::scoped_lock lk(_lock);
		return DetachItem(_hotkeys, a_id);
	}

	const Hotkey* HotkeyManager::FindByItem(const ItemId& a_id) const
	{
		std::scoped_lock lk(_lock);
		for (const auto& h : _hotkeys) {
			if (h.Has(a_id)) {
				return &h;
			}
		}
		return nullptr;
	}

	const Hotkey* HotkeyManager::FindByForm(RE::FormID a_form) const
	{
		std::scoped_lock lk(_lock);
		for (const auto& h : _hotkeys) {
			if (h.HasForm(a_form)) {
				return &h;
			}
		}
		return nullptr;
	}

	const Hotkey* HotkeyManager::FindByBind(const Bind& a_bind) const
	{
		std::scoped_lock lk(_lock);
		for (const auto& h : _hotkeys) {
			if (h.bind == a_bind) {
				return &h;
			}
		}
		return nullptr;
	}

	const Hotkey* HotkeyManager::ResolveChord(
		RE::INPUT_DEVICE                         a_device,
		const std::unordered_set<std::uint32_t>& a_held,
		std::uint32_t                            a_trigger) const
	{
		std::scoped_lock lk(_lock);

		const Hotkey* best = nullptr;
		for (const auto& h : _hotkeys) {
			if (h.bind.device != a_device) {
				continue;
			}
			// The just-pressed key must be part of the chord, so an unrelated held/phantom
			// key can never shadow the bind the user actually triggered.
			if (a_trigger != 0 &&
				std::find(h.bind.keys.begin(), h.bind.keys.end(), a_trigger) == h.bind.keys.end()) {
				continue;
			}
			if (!h.bind.IsSatisfiedBy(a_held)) {
				continue;
			}
			// Prefer the most specific (longest) satisfied chord.
			if (!best || h.bind.keys.size() > best->bind.keys.size()) {
				best = &h;
			}
		}
		return best;
	}

	Bind HotkeyManager::BindOfItem(const ItemId& a_id) const
	{
		std::scoped_lock lk(_lock);
		for (const auto& h : _hotkeys) {
			if (h.Has(a_id)) {
				return h.bind;
			}
		}
		return {};
	}

	Bind HotkeyManager::BindOfForm(RE::FormID a_form) const
	{
		std::scoped_lock lk(_lock);
		for (const auto& h : _hotkeys) {
			if (h.HasForm(a_form)) {
				return h.bind;
			}
		}
		return {};
	}

	std::vector<ItemId> HotkeyManager::ResolveChordItems(
		RE::INPUT_DEVICE                         a_device,
		const std::unordered_set<std::uint32_t>& a_held,
		std::uint32_t                            a_trigger) const
	{
		std::scoped_lock lk(_lock);
		const auto*      hk = ResolveChord(a_device, a_held, a_trigger);
		return hk ? hk->items : std::vector<ItemId>{};
	}

	std::vector<Hotkey> HotkeyManager::Snapshot() const
	{
		std::scoped_lock lk(_lock);
		return _hotkeys;
	}

	void HotkeyManager::ReplaceAll(std::vector<Hotkey> a_hotkeys)
	{
		std::scoped_lock lk(_lock);
		_hotkeys = std::move(a_hotkeys);
	}

	void HotkeyManager::Clear()
	{
		std::scoped_lock lk(_lock);
		_hotkeys.clear();
	}
}
