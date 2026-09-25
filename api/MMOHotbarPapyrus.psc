
Scriptname MMOHotbarPapyrus
FormID 0000000F

; ============================================================
; MMOHotbar - Papyrus bindings
; ------------------------------------------------------------
; API stabil di v2.0.0.
; ------------------------------------------------------------
; Preset/bank model:
;   - The hotbar shows ONE row of 12 slots at a time.
;   - There are two independent banks (preset 1 and preset 2).
;   - Tapping X (preset toggle) only flips which bank is live;
;     X is never part of a chord and never needs to be held.
;   - While preset 2 is active, pressing a bound chord (F, Z,
;     F1, Home, ...) fires that chord directly -- no "X + key".
;   - Binding, keycaps, firing and lookups follow the visible
;     bank, so FireSlot(1..12) always addresses the row you see.
; ------------------------------------------------------------
; FireSlotSwap is gone (no swap hands anymore). Use FireSlot for
; every slot. If an old script calls FireSlotSwap it will simply
; fire the slot on the active preset.
; ============================================================

Scriptname MMOHotbarPapyrus extends Object

; ------------------------------------------------------------
; Fire a hotbar slot by number (1..12) on the ACTIVE preset
; ------------------------------------------------------------
Event FireSlot(Int slot)
    if slot < 1 || slot > 12
        Debug.StackTrace("MMOHotbar.FireSlot out of range: " + slot)
        return
    endif
    MMOHotbar.FireSlot(slot)
EndEvent

; ------------------------------------------------------------
; Which preset is currently active? 1 or 2
; ------------------------------------------------------------
Event GetPreset()
    return MMOHotbar.GetPreset()
EndEvent

; ------------------------------------------------------------
; Dump slot table to log (debug helper)
; ------------------------------------------------------------
Event DumpSlots()
    MMOHotbar.DumpSlots()
EndEvent

; ------------------------------------------------------------
; Native bridge (bound by plugin at load)
; ------------------------------------------------------------
Scriptname MMOHotbar extends Object

    Event EquipSlot(Int slot)
        Debug.StackTrace("MMOHotbar.EquipSlot " + slot)
    EndEvent

    Event GetPreset()
        return 1
    EndEvent

    Event DumpSlots()
        Debug.StackTrace("MMOHotbar.DumpSlots")
    EndEvent
EndScript
