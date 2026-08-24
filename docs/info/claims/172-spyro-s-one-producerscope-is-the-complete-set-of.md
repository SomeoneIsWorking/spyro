---
id: C172
kind: claim
status: falsified
created: 2026-08-12
tags: producers,census,scoping,instrument
depends: game/render/fx_title_menu.cpp#spriteEmit
reconfirmed: 2026-08-12 21:16:05
verified_at: 2026-08-12 21:16:05
falsified_on: 2026-08-13
---

## Claim

Spyro's ONE ProducerScope is the COMPLETE set of census-visible native producers in this port's reachable window — enumerated by instrument with a positive control, not assumed

## Evidence

MEASURED 2026-08-12, both classes run, binary md5 7c50393977d76c687a660989319217dd. NATIVE LEG (PSXPORT_SPYRO_FRAME_LOOP=1, PSXPORT_NATIVE_FRAMES=3000, PSXPORT_DEBUG=unscoped, cap 300 s, rc=0, scratch/logs/unscoped_native3000.log): '[producers] run-end: 1 row(s); prims seen 1378 = attributed 1378 + unscoped-native 0 + guest-origin 0 + gp0-anon 0 + span-miss 0 + span-no-fn 0' / '[producers]   guest   0x8007CD38  native 1378  guest 0  frames 696 (f585..f1281)  titlefx:spriteEmit', and ZERO 'UNDECLARED native prim' lines. THE NEGATIVE IS NOT VACUOUS — POSITIVE CONTROL, same tree, ProducerScope line commented out and REBUILT (md5 be9089ddf13446c2a79036b668c7f706, scratch/logs/unscoped_sabotage3000.log): 'run-end: 0 row(s); prims seen 1378 = attributed 0 + unscoped-native 1378', plus EXACTLY THREE distinct UNDECLARED call-site stacks, all three RenderQueue::push2dQuad <- SpyroRenderer::spriteEmit <- SpyroRenderer::titleMenuRender (at three different return offsets in titleMenuRender: +0x497, +0x32a, +0x379) <- renderScene <- drawFrame <- spyro_frame_loop_run. The instrument dedupes by STACK and caps by NOVELTY (render_queue.cpp:641-675), so those three stacks are the whole set of undeclared push sites in the run — i.e. every native prim in the reachable window comes from the one producer already scoped. RESTORED and rebuilt to a BYTE-IDENTICAL binary (md5 back to 7c50393977d76c687a660989319217dd), gate green: 'tools/gate.sh 40' capped at 400 s rc=0, '[gate] tally: 19 PASS, 0 FAIL (checks = 19); 1 NOTE, 0 WARN (not checks)' (scratch/logs/ext_gate.log). SHIPPING (reference) LEG SWEPT TOO, on the SABOTAGED build so any native push would have printed: no PSXPORT_SPYRO_FRAME_LOOP, cap 3000 (scratch/logs/unscoped_refleg3000.log, rc=0) — 'prims seen 2962984 = attributed 0 + unscoped-native 0 + guest-origin 1481492 + gp0-anon 0 + span-miss 1481492 + span-no-fn 0' and 0 UNDECLARED lines, so NOT ONE native prim is pushed on the leg the port ships; every prim there is the guest's. (That also reproduces #61's double-count a third time, 2962984 = 1481492 + 1481492.) STATED BLIND SPOTS: (1) the unscoped diagnostic is suppressed while guestGp0Depth > 0 by design, so on the reference leg its silence means 'no push outside guest GP0 execution' — the unscoped-native counter, not the silence, is what carries that leg; (2) the window is the title screen, all this port's native leg reaches; (3) native_terrain.cpp (0x8004EBA8, PSXPORT_NATIVE_TERRAIN=1) is a native producer the census CANNOT see and MUST NOT be scoped — it writes GUEST PACKETS with mem_w32 into the guest packet pool and never touches RenderQueue, so its prims arrive later through the guest OT walk with its scope long closed; a ProducerScope there would count zero and be pure decoration, and the same holds for wide_clip.cpp's hooks, which super-call guest bodies.

## What would falsify it

any run whose [producers] run-end line reports unscoped-native > 0, or any PSXPORT_DEBUG=unscoped UNDECLARED stack naming a producer outside SpyroRenderer::spriteEmit — e.g. after a new native producer lands, or once the native leg reaches past the title screen

## Re-confirmed 2026-08-12 21:16:05

CONFIRMED STRUCTURALLY (stronger than the original's run evidence) 2026-08-12: 'grep -rn push2dQuad game/' has exactly ONE call site (fx_title_menu.cpp:263); 'grep -rn ProducerScope game/' exactly one construction (line 262); the only other rq uses are two flush() consumers (render_frame.cpp:115, vsync.cpp:323). So the complete set is 1 by enumeration of the render queue's single push function. Reproduced at runtime on a fresh build (scratch/logs/V_native3000.log, PSXPORT_DEBUG=unscoped, cap 3000, rc=0): 'attributed 1374 + unscoped-native 0' and 0 UNDECLARED lines. Reference leg swept (scratch/logs/V_refleg3000.log and V_refleg_after.log): 0 rows, unscoped-native 0, every prim guest-origin. Its blind-spot (3) is now CORROBORATED BY THE FRAMEWORK ITSELF, which prints 'otattr:warn GameConfig::packetPoolBase/Stride are 0 for this game — packet-pool attribution is STRUCTURALLY BLIND here'.

## FALSIFIED 2026-08-13

A second native RenderQueue producer now exists in game/render/fx_sprite_queue.cpp, scoped to guest 0x80022A2C; the claim's own falsifier explicitly names this condition.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
