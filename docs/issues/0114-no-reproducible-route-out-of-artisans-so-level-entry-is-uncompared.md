---
id: 114
title: No reproducible route out of Artisans, so level entry is still uncompared
status: open
symptom: the oracle matches the console byte for byte at every frame inside Artisans, but any route that walks out of it diverges, so the one boundary that discards and reloads guest code at a reused address has never been compared
state_items: S011
tags: oracle,camera,gameplay,timing,invalidation
created: 2026-09-19
updated: 2026-09-28
---

## Why a level entry is the checkpoint worth having

Everything the oracle compares today happens inside one resident WAD image. A portal entry is the
only place the product must discard and reload guest code at a reused load address and invalidate
every translation that came from it. That is the part of the runtime a matched walk around Artisans
cannot exercise at all, and it is why `tools/oracle_spyro1.py` carried a `level` checkpoint at all.

## What is measured, and it is not the product

Inside Artisans the product is exact. With `tools/oracle_compare.py --frame-step 1`, the twelve
representative gameplay segments are compared after **every one** of their 477 game frames rather
than once per segment: 485 comparisons including `save_picker` and `playing`, **zero divergences**,
exit 0, 153 s. Every decisive range agrees byte for byte at every frame, including
`g_Spyro.m_Position`, `g_Spyro.m_State`, `g_GameTick`, `g_StateSwitch`, the three pad words and the
occlusion result.

Outside it, nothing is reproducible. Two attempts:

| instrument | what happened |
|---|---|
| one `spyro1_steering.Walk` per core, each steering from its own camera | agreed for two decisions, drifted from the third (`bearing +42` against `+38`), pressed different buttons from the tenth (`up+left` against `up`), took 16 decisions against 15, and entered level 11 from two different places. The decisive `player.position` DIVERGE it reported was those two places, not a product defect |
| record the reference's 473 steered frames and replay exactly those on the product | the product did not reach any portal within the 6000-frame budget: `game_tick=6002, level=10` |

Replay excludes the input as the cause. Under the console's own pad, frame for frame, the product
goes somewhere else.

## Why, and it is one counter

Spyro's d-pad is camera-relative, so an identical pad with a different camera is a different
direction. The per-frame Artisans report says exactly where the camera parts company. Taking the
first comparison at which each declared range stops being equal:

| range | first unequal at | bytes |
|---|---|---|
| `player` +0x2a0 | `gameplay[1]` frame 43 | `m_damageSoundChannel`: native 3, console 2 |
| `dragon_cutscene` +0x8 | `gameplay[2]` frame 9 | native 1, console 0 |
| `delta_time` | `gameplay[3]` frame 26 | native 2, console 4 |
| `camera` +0x00 | `gameplay[3]` frame 39 | the projection and view matrices |

The sound-channel index is an SPU voice allocation and means nothing here. The camera one does. At
that frame the camera's `m_Position`, `m_DestinationPosition`, `m_State`, `m_OcclusionGroup` and all
four `SphericalCoordsOffset` blocks are byte-identical; what differs is only the two rotation
matrices and the Euler `m_Rotation` they come from:

```
m_Rotation   native ff0f e80f 3103   console ff0f f70f 2c03
             x 0x0fff / 0x0fff   y 4072 / 4087   z 817 / 812
```

15 units of 4096 is about 1.3 degrees of yaw. The camera is in the same place, pointing slightly
differently, one frame after `g_DeltaTime` read 2 against the console's 4. That counter is the lag
the last update was told about, derived from `g_LevelTicks`, whose constant offset from each load is
what [issue 0110](0110-artisans-camera-checkpoints-are-not-yet-phase-aligned.md) parked as a
pacing-model convention. Camera smoothing consumes it; heading consumes the camera; a 473-frame
walk integrates the difference.

So 0110's residual is not confined to counters after all. It does not move the player under held
input over 477 frames, which is why every decisive range still matches, but it does steer.

## What would close this

Either of these, and the first is the real fix:

1. Make `g_DeltaTime` reproduce retail's value per update, which makes the camera's rotation match
   and the recorded route replay. This is 0110 reopened at its cause rather than at its symptom.
2. Reach a level without a camera-relative route at all, if the title offers one that does not write
   guest state. A cheat or a forced teleport would be writing the answer, not measuring it, and does
   not count.

Until then `tools/oracle_spyro1.py` registers only the checkpoints it can honestly compare, and
S011 stays `missing` with WAD invalidation uncovered by the oracle.

## Not the same as issue 0113

0113 was a hard stop on the same route: the per-face colour program a secondary actor's triangle
selects was unimplemented, so the product aborted shortly after entering Stone Hill. That is fixed
and is not why the route is unreproducible; the walk diverges long before it, inside Artisans.

## 2026-09-28: item 1 is ANSWERED, and the answer is a CD read, not a pacing rule

### The cadence rule exists and it is now read from bytes

[Issue 0110](0110-artisans-camera-checkpoints-are-not-yet-phase-aligned.md) carries the derivation.
In short: `main()` at `0x80012204` sets `g_DeltaTime = clamp(g_UnprocessedFrames, 2, 4)` at
`0x80012238..0x80012268` and zeroes the counter at `0x80012270`; the counter is incremented once per
completed VSync callback by `PadVSync` at `0x800542C8`. So the rule is **the number of display
fields the guest spent since the main loop last ran, clamped to [2,4]** — and it is unbounded above.

**So the divergence is not the product's pacing model.** The product's count reaches that clamp on
**499 of 499** main-loop iterations, and the console's count exceeds 2 on **1 of 497**. The console
is the one that spends extra fields, and the single place it does is a synchronous CD read.

### The one divergent iteration, identified

`tools/oracle_cadence_probe.py` censuses both cores one FIELD at a time over the whole Artisans
route. The console's widest window, with the words live in it:

```
field 8283  window=6  g_DeltaTime=4  g_CDMaxReadTime=600  g_CDReadTime=0
                g_GameTick=362  g_LevelTicks=837  g_StateSwitch=0
```

`g_CDMaxReadTime` is **600** there and **0 at all 496 other boundaries**. That is retail's CD-read
budget: `src/cd.c` `CDLoadSync`/`CDLoadAsync` set `g_CDMaxReadTime = maxTime` (600 fields = 10 s) and
then spin in `while (g_CdState.m_IsReading || CdSync(1,0) != CdlComplete) CDLoadTime();`, with
`g_CDReadTime` incremented once per VSync by `PadVSync`. The 6-field window is one update that
waited on the CD drive, and `g_DeltaTime` clamped 6 to 4.

**This is the first `delta_time` inequality this issue recorded**, and it reproduces in two
independent `oracle_compare.py --frame-step 1` runs at the same iteration, the same console field
8283, the same `g_GameTick` 362 and the same `g_LevelTicks` 837. It is a disk wait, not jitter.

### The camera follows it, and it follows the disk wait

`camera` first differs 13 comparisons later (`gameplay[3]` frame 39, `m_Rotation` as quoted above),
at the same place with the same spherical blocks, one frame after the 6-field update. So the camera
divergence is downstream of the CD read too, and the chain is now named end to end:

```
synchronous CD read costs the console 4 extra display fields
  -> g_UnprocessedFrames reaches 6, g_DeltaTime clamps to 4
  -> camera smoothing integrates 4 instead of 2
  -> m_Rotation differs by ~1.3 degrees of yaw
  -> a camera-relative pad is a different direction
  -> the two walks diverge
```

### The proper fix, and it is NOT a change to the pacing clamp

`spyro1_frame_driver.cpp:111` already writes `clamp(elapsedFields(), 2, 4)` and the census confirms
the product's count reaches it. Changing `kFrameStepMin`/`kFrameStepMax`, or special-casing the
route, would be a fitted constant against a number that is already right.

**What is missing is that the product performs its archive reads at zero guest-time cost.** It
delivers a WAD transfer synchronously in host code (`game/core/cd_queue.cpp` -> `ArchiveTransfer::read`,
plus the `cd_override.cpp` synchronous loaders), so a read that costs the console 4 display fields
costs the product none. The fix belongs to the CD/archive owner: a synchronous read must advance the
guest's display field clock by the time the console's read takes, so the count the main loop sees
reaches the same 6. That is a real change to field accounting in a subsystem this issue does not own,
it needs its own evidence, and **nothing is shipped for it here.**

## 2026-09-28: a LEVEL ENTRY IS NOW COMPARED — on the no-input attract route, and it is compared

`tools/oracle_spyro1_demo.py` already existed for this; what was missing was the measurement, and it
is now taken. `--policy demo --frame-step 1`, 99.9 s, exit 1, **556 checkpoints**:

| checkpoint | decisive ranges | unequal |
|---|---:|---:|
| `demo_level_load` (the WAD load under way) | 15 | **1** (`load_stage` 1 vs 2) |
| `demo_playing` (**the level entry**) | 15 | **0** |
| `gameplay[0]` 1..400f | 15 each | **0** |
| `gameplay[1]` 1..153f | 15 each | **0** |
| `gameplay[1]` 154f | 15 | **1** (`player.position`, issue 0133) |

**2 of 8340 decisive comparisons unequal over the whole run**, and the level entry itself is 15 of
15. At `demo_playing`, byte-identical on both cores: `g_Gamestate` 0, `g_LevelId` **11**,
`g_LoadStage` **0xFFFFFFFF**, `g_GameTick` 2, `g_StateSwitch` 0, `g_DemoMode` 1,
`g_Spyro.m_Position` `be9e0200 4f250200 6a510000`, `g_Spyro.m_State` 0, `D_80075844` 9. The guest
state agrees **across** the WAD reload, and it agrees for 553 further per-iteration comparisons.

The one unequal at `demo_level_load` is `load_stage` 1 against 2, which is a CD-paced progress
counter sampled at a park that is not the same point in the load on both cores — issue 0133 records
the same reading and why it is not a state.

### The reload and invalidation evidence, on both sides of the boundary

`tools/oracle_level_entry.py` drives the product with **real pad input** through a portal
(`drive.Seeker` over the shared `spyro1_steering.Walk`, stop condition = the guest's own
`g_LevelId` changing; no teleport, no written guest byte) and reads, from the product's own log,
which transfer supplied the resident code on each side. Log split at the crossing: **25 of 33**
transfer lines before, **8** after.

| side | resident overlay entry (`g_UpdateMoby`) | transfers whose COVERED range contains it |
|---|---|---:|
| before, `g_LevelId` 10 | `0x8007D9C8` | **5 of 25** |
| after, `g_LevelId` 11 | `0x8007DA78` | **1 of 8** |

and the reuse claim, from the two sides' own numbers:

```
of the after-side transfers, 1 ALSO covers the before-side entry 0x8007D9C8:
  0x8007AA38..0x8008AA38 (65536 bytes)
```

**The same guest addresses were written by a load on each side of the level boundary.** That is the
address reuse 0114 exists to cover, measured rather than asserted. The prologue words differ across
the crossing (`27BDFE40 AFBF01BC…` against `27BDFE00 AFBF01FC…`), so the two images really are
different code at nearby addresses in one arena.

**Which parts are comparable and why the rest is not.** Comparable and measured: the resident image
identity on each side, as the guest's own overlay pointer; the address range a load published on each
side; the address reuse between them; and the guest state agreeing across the crossing. **Not
comparable, and not claimed:** the per-`Core` invalidation COUNT and the image generation NUMBERS.
Those live in psxport (`LightrecExecutor`'s counters, `ImageCatalog::activate`) and no REPL command
reads them, so naming a number would be inventing one. The ranges reported above ARE the ranges
handed to the invalidation owner, and that is a path claim rather than a re-derivation:
`cd_queue.cpp::transfer` -> `ArchiveTransfer::read` writes with `Core::mem_w8` and then calls
`Core::imageCatalog().activate()` over the same span, and `Core::writeGuestMemory` is the owner that
calls `psx::cpu::notifyExecutableWrite`. **What is still unproven: that Lightrec actually discarded
the old translations for those spans.** That is a framework-counter question and needs one REPL
read command, named below.

## What this issue's honest status is now

**Still `open`**, and narrower than it was:

- **Closed:** the cause. It is a synchronous CD read the product performs at zero guest-time cost,
  named from bytes and measured on both cores, reproducible, with the chain to the camera written out.
- **Closed:** the claim that a level entry is uncompared. One IS compared, 15 of 15 decisive ranges
  equal, 553 further per-iteration comparisons equal, and the WAD reload at a reused address is
  measured on both sides of the boundary from the product's own log.
- **Still open, and it is what keeps this issue open:** a route out of ARTISANS still does not replay,
  because the camera carries the one CD-wait divergence and the d-pad is camera-relative. The fix is
  in the CD/archive owner, not in the pacing code, and it is not shipped.
- **Still open:** Lightrec's actual invalidation of the old translations at the crossing. The
  evidence above establishes that the right ranges were handed to the owner, not that Lightrec acted.

### The next step, named

1. **CD/archive owner:** make a synchronous archive read advance the guest's display field clock by
   the time the console's read costs, so `g_UnprocessedFrames` reaches 6 on that iteration. Then
   re-run `tools/oracle_cadence_probe.py --core console` and `--core native`: the product's window
   distribution should gain a 6 at field 8283 and `delta_time` should read 4 there too. That is the
   falsifier for this whole section.
2. **Framework:** one REPL read command exposing `LightrecExecutor`'s invalidation counters and
   `ImageCatalog`'s active identities, so the generation pair and the invalidated-block list are
   measurements rather than a path argument.
3. Then re-drive the Artisans walk. `tools/drive.py gameplay --seek-portal` on the product alone
   already leaves level 10 in 384 fields with real pad input, so the product half of a portal route
   is not the obstacle; the two-core replay is.
