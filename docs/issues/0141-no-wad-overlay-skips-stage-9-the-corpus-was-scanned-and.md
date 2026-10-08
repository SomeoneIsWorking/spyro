---
id: 0141
title: No WAD overlay skips stage 9 — the corpus was scanned, and a second Start mask was found
status: open
symptom: docs/issues/0138 closed with "the WAD overlays were not scanned" as the single unmeasured place a guest-owned skip for GS_EntranceAnimation could be hiding, and named it the unblocking action for the operator's question. The overlays were unreadable here only because no extractor existed, not because the bytes were unavailable.
state_items: S011
tags: re,overlay,skip,input,stage,camera
created: 2026-09-28
---

## Asked

The operator asked whether the guest itself already provides a Start-to-skip for `GS_EntranceAnimation`
(stage 9, the level-entrance camera sweep) in one of its WAD overlays. Issue 0138 had proven no
faithful native arm can exist and had left this as the one place the answer could still hide.

**Answer: no.** The overlays were scanned, and the negative is now bounded by a stated corpus
instead of resting on the resident image alone. Claim C229 carries the full measurement.

## Getting the bytes, and proving residency

`docs/issues/0138` said this needed an overlay extractor the repository lacks. The bytes were never
the obstacle:

- `WAD.WAD` is a file on the disc. `chdman extractcd` plus a hand ISO9660 walk of the 2352-byte-sector
  image yields `WAD.WAD;1` at LBA 37, 110,260,224 bytes, sha256
  `7ba8961c3626bcec816fbd6feecc4eff67dc8018ba43974e7838f4c341bf54a1`. It is byte-identical to a copy
  another agent had already extracted to `scratch/wad_census/`, so the corpus is authenticated against
  the disc rather than trusted from a peer. `tools/provision_title.py` was NOT re-run and
  `scratch/assets/spyro1/` was not touched.
- enumerates 79 index entries and scores 36 of them >= 90% on its code-opcode
  share. Those 36 are the corpus: **501,760 aligned words**.

The load base is `0x8007AA38`, and it is a claim that was checked rather than assumed:

- Claim C111's ground truth word — guest `0x8007CBA0` == `0x16020029` — comes out of the WAD image
  exactly, and **a base four bytes lower does not** (`0x00000000`), so the formula discriminates.
- **Residency measured against live RAM.** The product's own `dumpram` verb wrote a mid-run dump: at
  that base, entry 2 (the `OV_5B800` title overlay) matches **3584/3584 words, 100.0%**. Four archived
  dumps show the same base carrying five different overlays at 97.5-99.9%, each deficit one
  contiguous tail run. The overlay formula is a DIFFERENT formula from the main image's and was not
 copied from it; imports `probe_guest_disasm` for the executable, for the
  reason `tools/decomp_image.py` gives.
- Every site this issue quotes was compared with the live dump and matches byte-for-byte.

## What the two named Start tests actually gate

Both are in the title/attract overlay, and neither is stage 9:

- `0x8007AC48` `lw $v0, 0x7380($v0)` then `0x8007AC50` `andi $v0,$v0,0x840` — the HELD word, gated on
  the title timer being >= `0x12C` (300). It calls `0x80056200` and continues to `0x8007ACCC`.
- `0x8007B88C` `lw $v1, 0x7378($v1)` — the EDGE word, loaded once and then tested twice: first
  `0x8007B894 andi $v0,$v1,0xA000` (beqz -> `0x8007B8BC`), then `0x8007B8BC andi $v0,$v1,0x840`.

**A correction to `docs/findings/start-skip-map.md`.** The map attributes "Start/X in the sub-state-1
arm" to `0x8007B88C`. That address is the `lw`; the `0x840` test is `0x8007B8BC`, and the mask
consumed first is **`0xA000`**, a mask the map does not mention. A census that only looks for `0x840`
cannot see it: there are 10 `0xA000` sites across the overlays and one in the main image. The
existing conclusion is unaffected; the address and the mask were one step off.

## The `andi …,0x840` census, with its denominator

The previous count of **2** was true and is still true — of the **main image**, where both sites are
the pause menu's confirm and stage 14's own skip. The overlays add **44** more:

| | count |
|---|---|
| WAD code entries scanned | 36 (of 79 index entries) |
| aligned words scanned | 501,760 |
| `andi rt,rs,0x840` sites | **44** |
| … whose tested register is a load of a pad global | **22** |
| … tested register is not a pad word | **22** (3 resolve to the constant 16; 19 to a computed value) |
| `andi rt,rs,0xA000` sites (the second mask) | 10 |
| loads of a pad global (`0x80077378` EDGE / `0x80077380` HELD) | 128, across 35 entries |

The 22 pad sites are **7 in the title overlay and exactly 1 in each of 15 level overlays**; all 22
test the EDGE word. Every one of the 15 per-level sites is the same shape — read the EDGE word, test
`0x840`, and on a press drive a **level-local** flag (`0x800777EC` in entry 9), never the gamestate
and never the camera. The block is the level's own card, which is the same presentation the map
already covers for stage 1 and the flyby card; it is not stage 9.

## Why none of it can be stage 9's

Stage 9's dispatch arm is `0x80033954..0x80033968` and its only call is `jal 0x8002E000` at
`0x8003395C`. Three separate reachability results, all negative:

1. **The call chain.** The transitive main-image closure of `func_8002E000`'s two callees
   (`0x8004A200`, `0x80037BD4`) is **176 functions** and contains **zero** `jal` into the arena. Its
   72 `jalr` sites all take their target from 13 globals; 4 of those DO hold arena addresses in live
   RAM, so this is **not** a claim that the arena is unreachable — the closure genuinely can enter
   overlay code, and the negative comes from the three tests below rather than from that.
2. **The terminal.** 15 overlay `sw $zero, 0x800757D8($at)` sites store `GS_Playing`, stage 9's
   terminal. **0 of 15** is intraprocedurally reachable from a basic block that loads a pad global.
   The 5 whose enclosing function is small have no pad read at all; the other 10 are in one large
   per-level state machine where the store is block 246 and the pad reads are blocks 0-3, and the
   forward walk from a pad block never arrives.
3. **The gate input.** 51 overlay `sh` sites write `g_Camera.m_Rotation.y` (`0x80076E1E`), stage 9's
   gate input. **0 of 51** is pad-reachable; all are GTE-derived (`mflo`/`sra` of a `jal 0x80016AB4`
   trig result, or an interpolated value). Separately, the 50 overlay loads of `g_Gamestate` have
   consumers comparing against **7** (10), **8** (35) and **7** (5) — **never 9**. No overlay watches
   for stage 9 at all.

## Bugs found in the instruments, and NOT papered over

Both are recorded because both produced a *confident wrong answer* first, and a clean negative is
worthless without knowing the instrument can say no.

- **The backward walk reported 0 of 14, then 0 of 44, pad reads.** A uniform result is the tell. Two
  real bugs: it started at the instruction *after* the `andi` (always landing on the following
  branch, which it correctly refuses to cross), and it did not skip non-writing instructions, so the
  `nop` in a branch delay slot ended the run. Fixed; the site count then matched `0x8007AC48` and
 `0x8007B88C` by hand. --selftest` now pins the known-good word, the
  discriminating wrong base, and both main-image positive controls.
- **A Python precedence bug hid a 100% residency match as 0.03%.** `ARENA_BASE & 0x1FFFFF + k*4` is
  `ARENA_BASE & (0x1FFFFF + k*4)` — right for `k == 0`, wrong for every word after — and the tool
  refused to report residency, printing "no entry above 50%". Uniform-near-zero across a whole
  corpus is the shape of a broken instrument; fixed and the tool now reproduces every archived
  dump's number.
- **`PSXPORT_WWATCH` did nothing.** A run armed with it logged `[cfg:warn] UNKNOWN knob PSXPORT_WWATCH
  is set and matched nothing — it did NOTHING in this run` and fired exactly once, at frame 0. The
  maintained knobs are `PSXPORT_STORE_OBSERVE` (which observes **store-instruction PCs, not target
  words**, so it cannot answer this question) and `PSXPORT_RAMDUMP_FRAME` / the `dumpram` verb. Not
  fixed here: it is a framework defect, and the port's own `PSXPORT_VK_HEADLESS` warns the same way.

## Not determined

- **Stage 9 was never observed at runtime.** The live run's own census over 648 samples reads
  `playing=57, title_screen=296, cutscene=295; never reached: level_transition, dragon,
  entrance_animation, credits`. Every statement here is static plus residency; none of it is a
  runtime observation of the screen, and a run that reaches it and shows a Start edge changing it
  would falsify the claim. That is blocked on the portal traversal issue 0138 already records.
- **43 index entries scoring below 90% were not scanned.** The code-share metric is a heuristic, and
  `docs/issues/0138` itself recorded that level entries score differently from the verified overlay.
  An entry that is in fact code but scores 48-90% would be a false negative. The denominators are
  printed so this is checkable rather than hidden.
- **Routes with no instruction to find them**: a store through a fully computed pointer, a `jr` to a
  register rather than `jr $ra`, a self-modifying word, or a callback supplied by level DATA — the
  level entries' headers are a function-pointer table, and data-supplied callbacks are not
  enumerable from the archive.
- **Entries 31, 43 and 55 have no RAM-confirmed residency** of their own. Their load base rests on
  the arena base plus their 98-99% siblings; only entries 2, 9, 11, 19, 29 and 67 were measured
  against a dump.
- **Whether stage 9 is worth a native arm at all** is still a product call, and it is now the only
  thing blocking it. The one line is `mem_w32(kGamestate, 0)` on a Start edge in stage 9, which
  issue 0138 declined to write. It is deliberately still not written here.
