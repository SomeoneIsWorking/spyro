---
id: C213
kind: claim
status: holds
created: 2026-08-21
tags: ownership,ndiff,reach,text
depends: game/core/native_text_sprites.cpp#buildTextSpritesNative, game/core/text_sprites.h#classifyTextGlyph, titles/spyro1/core/spyro1_runtime.cpp#Spyro1Runtime::registerOverrides, tests/test_text_sprites.cpp#main, external/spyro-1/src/gamestates/draw.c#func_800181AC
reconfirmed: 2026-08-22
verified_at: 2026-08-22 19:10:18
---

## Claim

Spyro natively owns the reached BuildTextSprites body at 0x800181AC while retaining its generated parent and already-owned FillWord/CopyVector children

## Evidence

The corrected ready-nonleaf ranker selected 0x800181AC as its top static opportunity (55 direct callers, 168 instructions, only already-owned 0x80016914 and 0x80017700 children, no jalr). The ordinary 3,000-field native and reference gates explicitly left it cold, so no body was imported from those runs. Extending the real reference-leg corpus to its gameplay horizon (`scratch/logs/gate-boot-20260821-115352.log`) reached 0x800181AC 1,625 times, first at field 5,397 from ra=0x8001E7E8, while the same FNTRACE run also reached positive control 0x8005BBF4 once and explicitly reported several ready candidates NEVER CALLED. The SCUS_942.28 slice 0x800181AC..0x8001844C is exactly 672 bytes / 168 instructions (SHA-256 `abd220f600bf905329074305b9ad933a9d2ca24fe8f8902962aa38bad51d4e64`); Ghidra, `external/open-spyro/src/c/BuildTextSprites.c.wip`, and byte-matching `external/spyro-1/src/gamestates/draw.c` agree on its glyph state machine and the two child boundaries. The focused `text_sprites` CTest checks digit/letter endpoints, all four special glyphs, fallback, and both capitalization answers. After repinning to framework `3418a79b624765614f3f198dc1e89632e1e650f0`, an explicitly configured Clang build passed the full 13/13 CTests, including cpp-policy over 43/43 compile-backed first-party C++ translation units. The final 9,000-field reference gate with `PSXPORT_NDIFF=8` (`scratch/logs/gate-boot-20260821-141206.log`) reported calls #1 through #8 of `text-sprites@0x800181AC` matching the retained generated body exactly across RAM, scratchpad, GPRs, hi/lo, and COP2 state; the run reached stages 13 and 0 and passed 13/13 checks. In the same nested comparison window, `spu-pio@0x8005BE88` calls #1 and #2 and parent `spu-init@0x8005BBF4` call #1 all matched, with no divergence. The separate default native-renderer shipping gate (`scratch/logs/gate-boot-20260821-141345.log`) passed 14/14 checks and attributed 705238 native-producer primitives. The selection FNTRACE itself showed both answers in one corpus: seven ready candidates reached and seven were explicitly NEVER CALLED, alongside the reached positive control. NDIFF does not snapshot the host-side projected-depth provenance tracker; the native body preserves the executable-derived `gte_hold_src`/`gte_copy_pz` register, address, and ordering sequence, but that host-only surface is not independently claimed as compared.

## What would falsify it

if the executable bytes or direct-child set changes, the 9,000-field reference corpus no longer reaches 0x800181AC, the focused glyph/capitalization test fails, PSXPORT_NDIFF reports a compared-state divergence on a reached call, or an instrumented projected-depth provenance comparison disagrees on the currently blind host-only surface

## Re-confirmed 2026-08-21 14:14:17

Reconfirmed after the final Clang rebuild against exact pushed psxport 3418a79b624765614f3f198dc1e89632e1e650f0: full CTest 13/13; long reference gate scratch/logs/gate-boot-20260821-141206.log matched text-sprites calls 1..8 and the nested SPU parent/child under PSXPORT_NDIFF=8, then passed 13/13; default shipping gate scratch/logs/gate-boot-20260821-141345.log passed 14/14 with 705238 native-producer prims.

## Re-confirmed 2026-08-21

Post-landing BuildTextSprites calls 1-8 match the generated oracle exactly in the 9000-field gate; focused glyph test and full CTest pass 13/13; default gate passes 14/14.

## Re-confirmed 2026-08-22 18:45:07

The ownership body and focused test are unchanged; only registration moved into Spyro1Runtime::registerOverrides. Full 27/27 CTests and the current 3,000-field SCUS_942.28 shipping gate pass; the prior 9,000-field NDIFF=8 evidence remains the call-level proof for this later-reached body.

## Re-confirmed 2026-08-22

Post-commit 987f9f8 root rebuilt the authoritative Clang tree; 27/27 CTests pass and the clean-framework native gate passes 14/14 with 1,491,438 primitives.
