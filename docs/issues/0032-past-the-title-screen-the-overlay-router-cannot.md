---
id: 32
title: Past the title screen the overlay router cannot identify the resident overlay at 0x800857CC
status: resolved
symptom: With FORCE_BUTTONS driving past the title (now possible since the EvMdINTR fix), the port reaches a new state and the router fails: 'addr 0x800857CC in slot 0x8007AA38 but NO resident overlay matches'. Best candidate OV_237D000 matches only 10/16 signature words.
tags: overlay,router,blocker
created: 2026-07-29
updated: 2026-07-29
---

NEW GROUND, reachable only since issue 0027 was fixed — before that the port never left the title screen, so this code was unreachable. The gate does not cover it either, because the gate presses no buttons.

The router logs resident[0..16] = 0B 00 00 00 A0 B0 07 80 04 B9 07 80 D4 BE 07 80, and the closest candidate OV_237D000 matches 10/16. A partial match on a signature that is supposed to identify an image exactly suggests either (a) an overlay this port has not extracted, or (b) the arena holding a MIXTURE — one overlay loaded over part of another, so no single image matches the first words.

Note 0x800857CC is well past the arena base 0x8007AA38 (about 0xAD94 in), so whatever occupies it is large. OV_B83800 spans [0x8007AA38,0x8008AA38) and would contain that address.

First step: run with PSXPORT_DEBUG=cd,ovload and read which overlay was actually LOADED most recently before the failure — the load-time identity (overlay_note_load) is authoritative where a content signature is not, and cd_queue.cpp already records identity at load time for exactly this reason (C032, issue 0013).

### Note (2026-07-29)
CAUSE: an unextracted overlay, and the tooling already handles it. Spyro's overlays are byte ranges in WAD.WAD with nothing enumerating them, so the known set is bounded by how far a run gets — overlay_scan.py exists precisely for this and MERGES rather than truncates. Fixing issue 0027 let the port reach new code, and a rescan found FIVE more: WAD +0x18F000, +0x18F800, +0x20F800, +0x287800, +0x7F2800. The set went 7 -> 12.

The two the router choked on are +0x287800 (75776 bytes) and +0x7F2800 (57344); the cdq log shows both being streamed to the arena base, and ovload had already labelled them '(none/unmatched, dest=0x8007AA38)' at load time — the load-time identity was reporting the problem before the router did.

The 10/16 partial match was a red herring in the sense that mattered: the resident header is word0=11 with a pointer table from 0x8007B0A0, while OV_B83800 is word0=12 from 0x8007B150. Same STRUCTURE, different overlay — which is exactly what a partial signature match on a count-plus-pointer-table header looks like. It was not a mixture of two images, and not a corrupt load.

ensure_recomp.py has emitted the 12 modules (ov_ov_7f2800 recompiled 9 functions after jal discovery). Rebuild is in progress; verification pending — do not close this until a run past the title shows no router error and no recomp-MISS.

### Resolution (2026-07-29)
NOT REPRODUCIBLE — measured, with the caveat stated. A 100s run with PSXPORT_FORCE_BUTTONS=start and PSXPORT_DEBUG=ovload produces ZERO 'none/unmatched' arena loads and zero 'NO resident overlay matches'. The gate agrees over many runs: 'arena loads UNMATCHED 0' every time, and 7 distinct overlays identified.

CAUSE: this entry was filed in the same session as issue 0032, which added the five overlays the router could not match. The specific failure ('addr 0x800857CC in slot 0x8007AA38, best candidate OV_237D000 matches 10/16 signature words') was the router meeting an overlay that had not been extracted. Once it was, the signature matched.

HONEST CAVEAT, since absence of a symptom is weaker evidence than presence: one forced-input run and many gate runs is not a proof that no scene anywhere loads an unextracted overlay. What makes this safe to close rather than leave open is that the failure is LOUD and MECHANICALLY CHECKED — the gate asserts 'arena loads UNMATCHED eq 0' on every run, so a regression reopens itself rather than hiding. That is the standard for closing this kind of entry: not 'I did not see it', but 'something will shout if it comes back'.
