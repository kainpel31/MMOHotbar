# Plugin API

Copy [`STB_HotkeySystemAPI.h`](STB_HotkeySystemAPI.h) into your own SKSE plugin. That one
header is the whole thing: no CommonLibSSE type and no STL container crosses the boundary,
so neither side has to match the other's CommonLib version or standard library.

## Why there has to be an API

Vanilla keeps favorites hotkeys in game state — `ExtraHotkey` slots on the item and
`MagicFavorites::hotkeys[8]` — which any plugin can read. It is also why vanilla hotkeys
reset: the engine renumbers and clears those on stack splits, equips and container moves.

This mod stores a binding as a chord → form table owned by the plugin and mirrored into its
own co-save record. Nothing about it exists in the game world, and SKSE's serialization
interface only ever hands a plugin its own records. So there is no way to read a binding
from outside, by design, and this is the way in.

## Getting the interface

From `kPostLoad` or later — every plugin's DLL is loaded by then:

```cpp
#include "STB_HotkeySystemAPI.h"

namespace API = STB::HotkeySystem;
const API::IVersion1* g_hotkeys = nullptr;

void FetchAPI()
{
    auto* mod = GetModuleHandleA(API::kModuleName);
    if (!mod) {
        return;  // not installed
    }
    auto request = reinterpret_cast<API::RequestAPI_t>(
        GetProcAddress(mod, API::kRequestFunction));
    g_hotkeys = request ? static_cast<const API::IVersion1*>(request(1)) : nullptr;
}
```

A null result means absent, too old, or the version was refused — carry on without it. The
object belongs to STB Hotkey System and lives for the process: never delete it.

## What v1 offers

Both directions are covered: key → what it fires, and entry → which key it is on.

| | |
|---|---|
| `Resolve(device, key, out, max)` | What pressing that key would fire **right now**, chords included — it reads the keys currently held, so call it while handling the key-down. Returns the true count, which may exceed `max`. |
| `EquipNow(binding)` | Equip one entry synchronously on the main thread, so you can act on the same press. `false` means nothing was equipped — do not go ahead. Also tells the mod you have handled this press. |
| `GetHotkey(form)` | The `Chord` a form sits on, any instance of it. `keyCount == 0` means unbound, so this is also the "is it bound" question. |
| `GetHotkeyExact(binding)` | The same, for one specific instance — fill in `ench` and `health` from the row's extra data. This is the one an item list wants: a plain sword and an enchanted copy of the same base are different bindings. |

`Resolve` is the only call that takes a buffer, and only because a key can carry a group of
any size — nothing that grows can cross a DLL boundary without both sides agreeing on an
allocator. A chord is at most two keys, so the lookups return a `Chord` by value instead.

### Claiming a press

`EquipNow` does double duty. STB Hotkey System's own input sink runs *after* the player
input handlers in the same event dispatch, so without a signal it would equip the entry a
second time — landing on top of whatever you had just started, a shout mid-charge being the
obvious case. A successful `EquipNow` marks the entry handled for the rest of that press, so
the sink leaves it alone. The rest of a group still goes through: a shout and a cuirass on
one key still puts the cuirass on.

### A worked example

[STB Quick Hotkey Cast](https://github.com/STB-Team) hooks `ShoutHandler::CanProcess` so a
hotkeyed shout charges on its own key. On key-down it calls `Resolve`, looks for a voice
form among the results, and calls `EquipNow`; only if that returns `true` does it claim the
press, because handing the key to the vanilla handler with the old shout still in the slot
would charge and fire the wrong one.

## Versioning

`STB_HotkeySystem_RequestAPI(version)` returns null for anything it does not know. A future
`IVersion2` will be a separate class and a separate version number — `IVersion1` will keep
working, so asking for 1 goes on being correct.
