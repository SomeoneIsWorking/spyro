---
id: I049
kind: instrument
status: trusted
created: 2026-08-12
---

## Instrument

PSXPORT_DEBUG=unscoped (psxport render_queue.cpp:641-675) — names the CALL SITE of every native prim pushed with no ProducerScope open

## Validated by

VALIDATED IN BOTH DIRECTIONS 2026-08-12 in spyro, which matters because its whole purpose is to print NOTHING when the port is fully scoped — a silence indistinguishable from 'I never looked'. POSITIVE: with fx_title_menu.cpp's ProducerScope commented out and rebuilt, it printed 3 distinct stacks naming RenderQueue::push2dQuad <- SpyroRenderer::spriteEmit <- SpyroRenderer::titleMenuRender (scratch/logs/unscoped_sabotage3000.log), alongside 'unscoped-native 1378'. NEGATIVE: with the scope restored (byte-identical binary, md5 7c50393977d76c687a660989319217dd) it printed nothing and the run-end line read 'unscoped-native 0' (scratch/logs/unscoped_native3000.log). HOW TO READ ITS SILENCE: never on its own. The DENOMINATOR is the '[producers] run-end' line's unscoped-native counter, which is emitted unconditionally; the stacks only say WHICH producer. TWO BLIND SPOTS, both by design: it is gated off while guestGp0Depth > 0 (a guest-origin push can never print, so it says nothing about the reference leg's guest prims), and it dedupes by stack into a 64-entry table — which is why it reports 'distinct-call-site table FULL' rather than silently truncating. It also cannot see a native producer that writes GUEST PACKETS instead of pushing to RenderQueue (spyro's native_terrain.cpp), because such a producer makes no census note at all.

## Known failure modes

(none recorded yet)
