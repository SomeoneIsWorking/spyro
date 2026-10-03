---
id: 149
title: The shared field owner is measured against Spyro 1's own attract demo, field by field, and the corpus mismatch count is not the instrument that can see this
status: resolved
symptom: after commit a915b23 moved Spyro 1's field delivery into game/core/field_owner.*, the attract-demo route's override differential reported one mismatching call each in allocate_particle_slot (0x80053570) and camera_collision_update (0x80034480), where the same route on main reported none, and main was recorded as aborting at field 14832 with `NATIVE RENDER NOT IMPLEMENTED — stage selector = 5` where this tree did not
tags: fields,shared-owner,attract-demo,regression,evidence
created: 2026-09-30
updated: 2026-09-30
---

## The question

`a915b23` is a WIP commit that moves Spyro 1's field delivery — the whole `deliver()` sequence, the
guest vblank dispatch, the counter, the REPL park, the presentation fence, the audio frame and the
host turn — out of `titles/spyro1/core/spyro1_field_scheduler.cpp` into the shared
`game/core/field_owner.*`, so Spyro 2 can use it. The one question that matters about code moved out
of a title is whether the title still behaves the same, and the recorded answer was a mismatch count
on the route corpus. This issue measures that question directly.

## Why the corpus's number cannot answer it

's mismatch tally is a count of *sampled override calls whose result differed
from the original body*. Two properties of that instrument make it unable to answer "did the title's
behaviour change":

1. **The denominator is the run, not the route.** The attract-demo route ends when its 420-second
   clock expires, and the two builds did not spend that clock the same way. A build that reaches
   further samples more calls, so a count that is 0 on one build and 1 on another can be the same
   behaviour sampled at two depths. The two mismatches here are `allocate_particle_slot` sample 636
   and `camera_collision_update` sample 68, i.e. calls a shorter run never makes.
2. **The instrument is not transparent.** The differential re-runs the original body and replays its
   journaled side effects, and it says so when it cannot: `original path serviced asynchronous
   pending work (interrupt or host turn)` is a verdict of *incomparable*, printed on the very first
   `camera_collision_update` call of the route. An instrument that sometimes executes the guest body
   twice is measuring itself as much as the product.

So the route corpus is left as the aggregate it is, and the comparison below is a new one.

## The instrument: a per-field trace of the no-input route

 reads two channels the product already prints and needs no new one:

* `[pace]`, one line per **delivered field**, carrying the delivered-field index `vbl`, the `site`
  that asked for it, and the guest 60 Hz counter the title's own root ticks — the delivery order and
  the title's own field clock;
* `[skipmap]`, one line per **guest state change**, carrying the field it was observed on with the
  load stage, the gamestate and the title screen's mode/state/substate.

 --debug pace,skipmap` arms both. The attract demo takes no input, so its path is
fixed by the guest and its field timing, which makes the trace a comparison rather than a sample.

**The instrument's own control, which is the part that makes it trustworthy:** the same binary run
twice produces an identical trace. Two runs of `main`'s product agreed on every delivered field and
every state change over the shared prefix, so a difference between two builds would be a difference
in the product, not in the run.

## What the comparison found

**Nothing changed.** Four independent measurements, each with its own denominator, and each with a
same-binary control run first.

The two builds compared:

| side | tree | commit | product | framework |
|---|---|---|---|---|
| `main` | `~/repo/psx/spyro/scratch/wt/gate-b1r` | `7d7f2eb`, clean | `build/bin/spyro_port` built 2026-09-30 14:58, no source newer than it | `psxport` `644ee5a6` |
| this tree | `~/repo/psx/spyro/scratch/wt/spyro2-boot` | `a915b23` | `build/bin/spyro_port` | `psxport` `644ee5a6` |

Both are Clang Release with an identical `CMakeCache.txt` apart from paths, and both were built
against the same framework commit, so the only variable is the port tree. The two binaries were
checked against their own sources: the new binary carries `host field clock armed at field` and not
`native host field clock armed at the gameplay boundary`; the main binary carries the reverse.

### 1. The delivery trace — identical over 35,734 delivered fields

 --debug pace,skipmap --timeout 240`, no input:

| | delivered fields | guest state changes | presents | native-render refusals |
|---|---:|---:|---:|---:|
| main | 35,734 | 105 | 17,649 | **0** |
| this tree | 40,752 (its clock ran out later) | 119 | 23,243 | **0** |

Comparing the 35,734 fields both runs delivered: **the field index, the delivery
site and the 60 Hz counter are identical on every field, and the guest state changes are identical at
every field both runs reported one.** The only difference reported is the truncation, which is the
shorter run.

### 2. The whole log — identical line for line, over 53,663 lines

Normalising only the timestamp, the elapsed-milliseconds field and the two worktree paths, main's
**entire** 240-second log and this tree's log are identical except for **two lines, both of them log
text this commit deliberately changed**:

```
[fields] guest vblank callbacks use IRQ stack [0x8000C000,0x8000E000)   vs   [fields] Spyro 1 guest vblank callbacks ...
[fields] native host field clock armed at the gameplay boundary        vs   [fields] Spyro 1 host field clock armed at field 436
```

That is the whole diff: 8 diff lines out of 53,663.

### 3. All of guest RAM — 0 of 524,288 words differ

`PSXPORT_GRAMDUMP=10000:<path>` (present frame 10,000, 2 MiB of main RAM), the strongest form of the
claim:

| comparison | guest words compared | differ |
|---|---:|---:|
| main vs main (control) | 524,288 | **0** |
| main vs this tree | 524,288 | **0** |

The control is what makes the row above it mean anything: the capture is deterministic, so a
non-zero count would have been a real difference and zero is a measurement rather than an absence.

### 4. On the corpus's own instrumented condition — the same two mismatches, on both builds

The corpus arms the reach recorder and the override differential (62 selectors, the first 16 calls of
each and every 64th after). Repeating the route with both armed, 420 s, no input:

| | delivered fields | state changes | refusals | `allocate_particle_slot` | `camera_collision_update` |
|---|---:|---:|---:|---|---|
| main | 66,070 | 189 | 0 | 637 sampled / **1 mismatch** | 115 sampled / **1 mismatch** |
| this tree | 64,430 | 177 | 0 | 637 sampled / **1 mismatch** | 115 sampled / **1 mismatch** |

The field traces are identical over the 64,430 shared fields, the two logs are identical line for
line over the whole of the shorter run, and the two mismatches are on the **same calls with the same
verdict and the same first difference**:

```
camera_collision_update @0x80034480 call 3328 (sample 68): match -> mismatch
  first difference ram [0x00077E04,0x00077E06): original 0C46 native 1473
allocate_particle_slot @0x80053570 call 39680 (sample 636): match -> mismatch
  first difference register ra: original 0x800535A0 native 0x8008A4BC
```

**So "0 mismatches on main" is false on main itself.** The recorded contrast is a property of how
deep each build's run went, not of the code. Those two mismatching calls are a pre-existing finding
of the attract route, present at `7d7f2eb`, and they are recorded where they belong rather than being
folded into this one.

### 5. The instrument's own control, which is what makes 1-4 mean anything

The same binary run twice: identical field trace over the shared prefix, and identical guest RAM at
present 10,000. The attract demo takes no input, so a difference between two runs of one binary would
mean the instrument cannot support the claim. It can.

## What is NOT claimed

* This says nothing about Spyro 2. The shared owner is exercised by Spyro 1 here; Spyro 2's own boot
  remains the frontier in
  [0092](0092-spyro-2-boot-prefix-stops-before-its-post-displa.md).
* The trace compares the delivery sequence, the title's field counter and the guest state changes. It
  is not a whole-RAM comparison; the RAM captures below are the separate, stronger claim.
