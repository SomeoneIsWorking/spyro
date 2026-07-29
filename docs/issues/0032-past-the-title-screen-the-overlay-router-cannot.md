---
id: 32
title: Past the title screen the overlay router cannot identify the resident overlay at 0x800857CC
status: open
symptom: With FORCE_BUTTONS driving past the title (now possible since the EvMdINTR fix), the port reaches a new state and the router fails: 'addr 0x800857CC in slot 0x8007AA38 but NO resident overlay matches'. Best candidate OV_237D000 matches only 10/16 signature words.
tags: overlay,router,blocker
created: 2026-07-29
updated: 2026-07-29
---

NEW GROUND, reachable only since issue 0027 was fixed — before that the port never left the title screen, so this code was unreachable. The gate does not cover it either, because the gate presses no buttons.

The router logs resident[0..16] = 0B 00 00 00 A0 B0 07 80 04 B9 07 80 D4 BE 07 80, and the closest candidate OV_237D000 matches 10/16. A partial match on a signature that is supposed to identify an image exactly suggests either (a) an overlay this port has not extracted, or (b) the arena holding a MIXTURE — one overlay loaded over part of another, so no single image matches the first words.

Note 0x800857CC is well past the arena base 0x8007AA38 (about 0xAD94 in), so whatever occupies it is large. OV_B83800 spans [0x8007AA38,0x8008AA38) and would contain that address.

First step: run with PSXPORT_DEBUG=cd,ovload and read which overlay was actually LOADED most recently before the failure — the load-time identity (overlay_note_load) is authoritative where a content signature is not, and cd_queue.cpp already records identity at load time for exactly this reason (C032, issue 0013).
