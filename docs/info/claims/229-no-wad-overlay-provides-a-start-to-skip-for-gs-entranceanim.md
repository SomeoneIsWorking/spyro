---
id: C229
kind: claim
status: holds
created: 2026-09-28
tags: re,overlay,skip,input,stage,camera
depends: tools/overlay_image.py, tools/probe_guest_disasm.py
---

## Claim

No WAD overlay of `SCUS_942.28` provides a Start-to-skip for `GS_EntranceAnimation` (stage 9), and
that is now a bounded negative over the whole overlay corpus rather than an unmeasured one.
`docs/issues/0138` left exactly this open ("the WAD overlays were not scanned ... this is the one
place a guest-owned skip for stage 9 could still be hiding"). Three independent measurements close
it:

1. **No overlay code is on stage 9's call chain.** Stage 9's dispatch arm is `0x80033954..0x80033968`,
   whose only call is `jal 0x8002E000` at `0x8003395C`. The transitive main-image closure of that
   function's two callees (`0x8004A200`, `0x80037BD4`) is **176 functions** and contains **zero**
   `jal` sites landing in the overlay arena `0x8007AA38..0x800FFFFF`. Its 72 `jalr` sites all take
   their target from one of 13 globals; 9 of those 13 are inside the main image and can never hold an
   arena address, and the 4 that are above the image (`0x800758E4`, `0x800758CC`, `0x80075694`,
   `0x80075734`) do hold arena addresses in live RAM — so this closure CAN reach overlay code, and
   the negative does **not** rest on the arena being unreachable.
2. **No overlay acts on stage 9's state or its two gate inputs.** Across the 36 code entries
   (501,760 aligned words): 15 immediate-form `sw $zero, 0x800757D8($at)` sites store `GS_Playing`,
   stage 9's terminal, and **0 of 15** is intraprocedurally reachable from a basic block that loads a
   pad global. 51 `sh` sites write `g_Camera.m_Rotation.y` (0x80076E1E), stage 9's gate input, and
   **0 of 51** is pad-reachable; all are GTE-derived (`mflo`/`sra` of a `jal 0x80016AB4` trig call,
   or an interpolated value). 50 overlay loads of `g_Gamestate` exist and their consumers compare it
   against **7** (10 reads), **8** (35 reads) and **7** (5 reads) — never **9**.
3. **`andi rt, rs, 0x840` is a census of 44, not 2.** The previous count of two covered only the
   resident main image. The overlays add **44** sites over 501,760 words. 22 test a pad global
   (7 in the title overlay `OV_5B800`, 1 in each of 15 level overlays, all on the EDGE word
   `0x80077378`); the other 22 do not — 3 resolve to the constant 16, and 19 test a register whose
   last definition in its function is a computed value. None of the 22 pad sites is stage 9's owner.

The stage-9 negative therefore survives the extension into the overlays that `docs/issues/0138` could
not perform. **What this does NOT claim:** that stage 9 is unskippable by any means, only that the
guest provides no route. The one line `mem_w32(kGamestate, 0)` that issue 0138 describes is still
unwritten, and that remains a product decision rather than an RE question.

## Evidence

**Overlay bytes and the load base.** The overlays are WAD index entries, not files. `WAD.WAD` was
read straight out of `/mnt/Boy/ROM/PSX CHD/Spyro the Dragon (USA).chd` by this session
(`chdman extractcd`, then a hand ISO9660 walk of the 2352-byte-sector image, which needs a 24-byte
user-data offset and not the 16 a PSX executable's own sectors use): `WAD.WAD;1` at LBA 37,
110,260,224 bytes, **sha256
`7ba8961c3626bcec816fbd6feecc4eff67dc8018ba43974e7838f4c341bf54a1`**. That is byte-identical to a
concurrently-extracted copy another agent left in `scratch/wad_census/`, so the corpus is
authenticated against the disc rather than trusted from a peer. `tools/wad_index.py` enumerates 79
entries; 36 score >= 90% on its own code-opcode share metric.

**The overlay address formula, cross-checked.** `guest = 0x8007AA38 + (offset within the entry)`, read
LITTLE-endian, exactly as `tools/probe_guest_disasm.py` reads the executable and for the same reason
(a PSX image stores instructions little-endian and data big-endian, so the wrong half of that choice
decodes into valid-looking garbage rather than failing). The main-image formula was deliberately NOT
reused: `tools/decomp_image.py` states why a second copy of a formula is a second unverified claim,
and `tools/overlay_image.py` imports `probe_guest_disasm` for the executable instead.

- Ground truth, from claim C111: guest `0x8007CBA0` holds `0x16020029`. The WAD image at that address
  reads `0x16020029`. **A base four bytes lower does not** (it reads `0x00000000`), so the formula
  discriminates. `tools/overlay_image.py --selftest` asserts both, plus two positive controls.
- **Residency, measured against live RAM, not assumed.** The product's own `dumpram` verb wrote
  `scratch/ov9/live_title.bin` mid-run. At base `0x8007AA38`, **entry 2 (OV_5B800) matches
  3584/3584 words, 100.0%** of its full length. Four archived dumps in `scratch/raw/` show the same
  base carrying five different overlays at 97.5-99.9% (entry 9 at 98.2% in `snap_2190`, entry 11 at
  97.5% in `snap_4512`, entry 29 at 99.9% in `snap_9346`, entry 67 at 99.4% in `snap_15210`); each
  deficit is one contiguous run of a few hundred words at the entry's tail, which is runtime scratch,
  not a wrong base.
- **Every named site matches live RAM byte-for-byte**: `0x8007AC48` (`0x8C427380`), `0x8007AC50`
  (`0x30420840`), `0x8007B88C` (`0x8C637378`), `0x8007B894` (`0x3062A000`), `0x8007B8BC`
  (`0x30620840`). The code read here is the code the port executes.

**The instrument's own controls, both directions.** Positive: the census finds the main image's two
recorded Start sites (`0x8002E988`, `0x80033354`, both `0x30420840`) and the overlay's 44. Negative:
two earlier versions of the backward walk reported **0 of 14** and **0 of 44** pad reads — a uniform
answer, which is the tell that an instrument is broken rather than informative. The causes were real
bugs, not missing data: the walk started at the instruction *after* the `andi` (so it always landed on
the following branch and stopped), and it did not skip non-writing instructions, so a `nop` in the
branch delay slot ended the run. `0x8007B894`'s `0xA000` mask is a **second, previously unrecorded**
test in the block the skip map credits with `0x840`; `docs/issues/0141` records it.

**A correction to `docs/findings/start-skip-map.md`, against it.** The map attributes "Start/X in the
sub-state-1 arm" to `0x8007B88C`. `0x8007B88C` is the `lw` of the EDGE word; the `0x840` test is four
instructions later at `0x8007B8BC`, and the mask actually consumed first is `0xA000` at `0x8007B894`.
The site's meaning is unchanged and the existing conclusion is unaffected, but the address and the
mask were both one step off.

**Live run.** `tools/drive.py gameplay` under the product slot, headless and offscreen, one instance,
killed by PID: reached `GS_Playing` at frame 6360. Its own census over 648 samples reads
`playing=57, title_screen=296, cutscene=295; never reached: level_transition, dragon,
entrance_animation, credits`, so stage 9 was **not** observed and no runtime claim is made about it.

## What would falsify it

- A WAD entry the census did not scan: `--min-score` below 90 admits 43 more entries, and the code
  share metric is a heuristic, so an entry scoring 48-90% that is in fact code would be a false
  negative. The denominator is printed for exactly this reason.
- Any overlay site that ends the screen by a route this does not model: a store through a computed
  pointer, a `jr` to a register rather than `jr $ra`, a self-modifying word, or a level-entry table
  passed in from level DATA (the level entries' own headers are only a function-pointer table; a
  data-supplied callback is not enumerable from the archive). Every such route is a store or a call
  this census has no instruction to see.
- A live run that reaches `GS_EntranceAnimation` and shows a Start edge changing the screen. That is
  the one measurement no static scan substitutes for, and it is blocked on the portal traversal
  `docs/issues/0138` already records — not on this question.
