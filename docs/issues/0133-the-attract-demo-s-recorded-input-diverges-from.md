---
id: 133
title: The attract demo's recorded input diverges from the console at g_GameTick 556: player position offsets (+9,+51,-2) growing to (+59,+405,0), with the camera following
status: investigating
symptom: oracle compare on the no-input attract route holds 553 per-iteration comparisons then DIVERGE on player.position at tick 556; the level entry itself MATCHES every decisive range
tags: oracle,attract-demo,physics,divergence,level-entry
created: 2026-09-26
updated: 2026-09-27
---

## THE LEAD IS EARLIER STILL: `Moby+0x42` already differs at the FIRST sample (tick 3)

Measured 2026-09-27 with `tools/probe_emitter_predicate.py`. **One byte**, on the two class-`0x71`
mobies `0x80173B80` and `0x80173BD8`:

| tick | `Moby+0x42` product / console |
|---|---|
| 40 | `0x00` / `0x00` — agree |
| 41 | **`0x00` / `0x03`** |

The product's value is `0x00` at all eight sampled ticks (3, 10, 20, 30, 38, 39, 40, 41) while the
console's oscillates `0x00/0x01/0x03`. **The product never sets it.** And it already differs at the
FIRST sample of the window, on 7 mobies (2 class-`0x71`, 5 class-10), product `0x00` / console `0x01`.

So the tick-41 `rand()` divergence — 25 calls on the product against 27 on the console, with 37 updates
before it agreeing exactly — is **not the first divergence in absolute terms**, and the cause of
`Moby+0x42` predates this probe's window. Two cores reach `demo_playing` at the same `g_GameTick` 3
but by different field counts (5928 vs 4319 advances), so the window's start is not a common state and
the existing harness cannot step both cores back before tick 3.

### The site, and a gate that is not a gate

`jal RandRange` at **`0x8008387C`**, inside level 11's class-`0x71` handler (reached from
`beq $v1,$v0(0x71)` at `func_level_11_8007DA78.s:250-251`). Its full predicate, with `Moby[0x48] == 0`
and `m_Props[0] == 0` holding on both cores at tick 40:

```
0x80083858  lw    $v0, D_80075794
0x80083860  beqz  $v0, 0x80083910      -> (Moby[0x42] & 2) != 0
0x80083868  lw    $v0, 0x0($s0)        (s0 = Moby->m_Props, 0x80083740)
0x80083870  bnez  $v0, 0x800838C8       -> m_Props[0] == 0
```

**`D_80075794` is not a global gate.** It is rewritten *per moby* at `0x8007DB3C` as `Moby[0x42] & 2`
(`andi $a0,$v0,0x2` at `0x8007DB34`), so reading it at a park measures the LAST live moby, not the one
being asked about. This is why the predicate has to be evaluated per moby from the pool on both cores —
195 pool slots read per core, 193 live.

### The previous attribution to `func_80039AA8` is REFUTED

The console's PC observer at `0x80037EA0` (`RandRange`, body read out of the image) recorded
`ra=80083884` on **both** records — one site, `0x8008387C`, `a0=3 a1=6` both times. The observer armed on
`0x80039AA8` itself recorded **`entries=0`**: the emitter was not executed at all in that update. And a
full 2 MiB main-RAM scan of both cores for `jal 0x80039AA8` and for a big-endian `0x80039AA8` data
pointer found **0 of each on both cores**. Level 11 has no caller for it at all.

The two calls are separated by moby, read from the console's own registers at the latch test
(`0x80083870`, where `a0` is the delay slot's `addu $a0,$s3,$zero`): `a0=80173B80` and `a0=80173BD8`.
Full decision chain for update 41: visit 1 `v0=2` moby `0x80173B80` `m_Props[0]=0` → call; visit 2
`v0=2` moby `0x80173BD8` `m_Props[0]=0` → call; visit 3 `v0=0` → `beqz` taken, no call.
`dropped=0 pairing_errors=0` over 9 records.

The console's post-call `m_Props[0] = 3` is the **return value** of `RandRange(3,6)`: `0x80083884
sw $v0,0($s0)` stores `v0`, which the call has just overwritten — the `addiu $v0,$v0,-1` in the branch
delay slot is dead. So the latch is "fire once from 0, then freeze at a random 3..6", and the console's
`3` is direct evidence that the two calls happened.

### Who writes `Moby+0x42`: no available instrument can name it

A scripted search of the whole reconstructed tree (`external/spyro-1/asm`, 429,885 overlay instructions)
finds **abundant** `sb`/`sh`/`sw` to `Moby+0x40` and `Moby+0x41` (268 `sb $zero,0x40($s3)`, 231
`sb $v1,0x41($a0)`, …) and **zero stores of any width covering `Moby+0x42`**. The listing only ever
*reads* it: `lbu $v0, 0x42($s3)` at `0x8007DB24` and `0x8007DB40` in level 11 and at the same offset in
ten other overlays. So the writer is in a function the decomp does not carry — exactly as `0x80037EBC`
was. And the product **executes** the site's latch store at other ticks, so this is not "the class-`0x71`
handler never runs on the product": it is that whatever sets `Moby+0x42` never runs there.

`PSXPORT_STORE_OBSERVE` cannot close this: it matches store **PCs**, so it can only confirm a PC already
known. `PSXPORT_CW` sees host-side stores only. The console observer needs a PC up front.

### The store observer fires, and its silence is ambiguous

It works: 24,332 per-callback lines, split exactly `0x8007DB3C x12166` and `0x8007DB54 x12166`
(before+after), with both positive controls — `sw $a0, D_80075794` and `sw $v0, D_800757F4`, executed
once per live moby per update — firing.

But it also **killed the product**: `[executor:error] frame-update required a completed guest call, but
execution exited as fault at 0x80083884 after 21662 cycles: Lightrec selected-store observer rejected
unsupported translated PC` → `abort()`, REPL exit 139. So **a missing callback line is ambiguous
between "the store never happened" and "the instrument refused it"**, separable only by reading the
fault line. That is why `probe_tick_divergence.py` now prints it as a warning rather than as a zero.

### The rand-count oracle does NOT agree, and no fix was made

Over 400 updates, bit-identical to the previous run: 399/399 updates had a bounded LCG path on both
cores with 0 unexplained, and **338/399 updates MISMATCH** (26,356 product calls against 26,422
console). First still-mismatching update is the one ending at `g_GameTick` 41. The oracle is the
measurement here because nothing was changed.

**What would prove a fix:** `tools/probe_tick_divergence.py --ticks 400 --rand-calls` agreeing on the
per-update `rand()` count for 400 consecutive updates, with 0 unexplained and 0 mismatching. Nothing
weaker counts — the seed chain needs no observer and no product edit.

### What is now known not to be the cause

- `D_800757F4` (`0x800757F4`, native 0 / console 1) is *written from* the divergent byte —
  `sw $v0, D_800757F4` at `0x8007DB54` with `andi $v0,$v0,0x1` at `0x8007DB4C` — so it is downstream of
  `Moby+0x42`, not upstream, and it flips sign between samples, making it a per-frame residue of
  whichever moby is processed last. The site's own gate reads the per-moby bit directly
  (`0x8007DB34/3C`), so it cannot be the site's input.
- `D_80075858` / `D_800758FC` (both 6 vs 4) are the **cyclorama scroll offsets**: `func_80050BD0`
  advances both by 2, wraps one mod `0x1000`, and clamps. They are a call-count residue of the field
  cadence, in a function that touches no Moby and calls no `rand`.
- `dist2d` / `OctDistance` are faithful — 5 of 5 listing-derived values match, including both asymmetric
  arms a swapped transcription gets wrong.

## The store lead was a RED HERRING, and the real lead is 28 ticks EARLIER

Corrected 2026-09-27. Three measurements dissolve the earlier lead, and the reason the earlier lead
looked solid is worth recording: **`--ram-diff` is ADDRESS-sorted, so "the first word to differ" carried
no temporal information at all.** The per-word `FIRST AT` ticks, which do, put two divergences that
PERSIST to the end of the window 28 ticks before the movement one:

- tick 311 — `D_80075788` 350 native vs 174 console
- **tick 528 — `g_DynMobyCount` (`0x800756A4`) = 5 native vs 15 console, with `g_MobyAllocPtr`
  (`0x8007573C`)**

`g_DynMobyCount` is incremented once per call by `func_800524C4` (`asm/42CC4.s:12`) = `MobyAlloc`,
which also advances `g_MobyAllocPtr`. **The product made 10 FEWER `MobyAlloc` calls** — a moby
population difference that feeds the very routine writing the diverging word. This is territory an
earlier pass declared eliminated, and that declaration was wrong.

About the store itself, three facts:

- **No static guest store of `1` to `0x80078AE0` exists.** A sweep of 200 files / 516,085
  instructions / 65,086 stores found exactly ONE store reaching that word: `0x8003FE7C`, storing
  `$zero`. A per-function sweep of everything writing into `g_Spyro` agrees — only `func_8003FE40`
  touches `+0x88`. The value arrives through a **pointer parameter**, the class the scan documents
  as un-attributable. "The decompiled tree contains no store of 1 to it" was TRUE and MISLEADING.
- It is a real write, not a skipped clear. The per-word mode shows the word AGREEING at 555
  (`00000000` both) and differing at 556 (`00000001` native, `00000000` console).
- The writer is a **guest** instruction: `PSXPORT_CW` on `0x80078AE0` logged 38 host-reaching
  stores, 35 of them `=00000001`, and `PSXPORT_CW_BT=1` gives
  `Core::cw_check_slow -> Core::writeGuestMemory<uint32_t> -> lightrec_rw -> Lightrec block thunk`.
  Statically that is `func_8004E3C8` (the `g_MobyCollisionChain` walker, which sets `$gp = $a0` at
  `0x8004E424`): `0x8004EB4C  sw $s7, 0x00($gp)`, called from `func_8003FE40` at `0x80040810` with
  `$a0 = $s2` built as `g_Spyro+0x88` at `0x8004075C`/`0x8004079C`. The console's
  `0x8003FE7C`/`AC208AE0` is not an alternative to this — the console ran the clear AND this
  function; `$s7` simply came out different.

**So the divergence is NOT the store and NOT a native override** — no override exists for
`func_8004E3C8` or `func_8003FE40`, and the writer is guest code. A count difference means fewer
CALLS, so the defect is upstream in the spawner's control flow, not in `MobyAlloc`. Next step:
per-tick `g_DynMobyCount` on both cores plus a product PC observation at the spawn call site.

### A framework belief that was measured false, and cost time

This investigation was stalled for a session on the belief that `PSXPORT_CW` "sees host-side stores
only" and "cannot attribute a guest-executed one". **That is false**, and it is the likeliest reason
the instrument holding the answer was set aside: `cw` fires for guest stores Lightrec routes through
`lightrec_rw`, which re-enters `Core::writeGuestMemory`, and that is exactly how the writer above was
found. What is true is narrower: most stores to a hot word are inlined by Lightrec and never reach the
host, so the watch is UNBIASED TOWARD THE SLOW PATH and a quiet watch means "no store took the slow
path", not "no store happened".

**`PSXPORT_STORE_OBSERVE` is not the unbiased instrument this paragraph once claimed, and psxport
`9bd4e9a8` corrects both the framework's documentation and this line.** It matches the guest PC of a
translated STORE — `LightrecExecutor::Impl::observeStore` compares `target.guestPc != guestPc` — so it
answers "what does this store write, and with what registers", NOT "which instruction wrote this word".
Arming a DATA address therefore matches nothing. That is not a subtlety: the per-target report row
echoes the armed value in a column that reads like an address. Measured on Mega Man X4, arming the data
address `0x80078AE0` produced `MATCHED NONE of the 9,366,306 executed JIT instruction(s)` on a word
that demonstrably changes every frame, while arming two real store PCs here produced 24,332 correctly
attributed lines. **To find which instruction wrote a word you must already know the instruction.** The
same commit fixes a use-after-scope in the observer's callback context that made every observation
increment a dead stack object — the first callback printed `seen=140723098021161`, which a
freshly-zeroed local cannot produce.

### Known gaps in the current evidence

- `SPYRO_FIELDS` in `tools/probe_tick_divergence.py` MISLABELS offsets: it calls `0x88`
  `m_floorIdleTime` and `0x8C/0x90/0x94` `m_airTime`/…, while the value evidence and the header's own
  `// 0x8c` comment say `0x8C` is `m_previousPosition` and `0x88` is `m_touchingMoby`. Not rewritten:
  the header, the port's render offsets and the table disagree across the `0x28-0x40` band and guessing
  would be worse than flagging it.
- 50,935 of 524,288 RAM words already differed at tick 555, mostly transient `0x801C05xx-0x801C09xx`.
  The watch set is 556 words, so an earlier PERSISTENT divergence outside it is not excluded.
- `interp_pc` is Core's CACHED pc at the last executor boundary, not the faulting pc. `0x8004EB4C` was
  identified statically and the backtrace confirmed, but the product's pc at the store was not observed.
- Why this hot per-frame main-RAM store takes Lightrec's slow path at all is unexplained.
  `0x80078AE0` masks into main RAM, which Lightrec normally inlines. Observation only.

## What the route is
`tools/oracle_compare.py --policy demo` (tools/oracle_spyro1_demo.py) drives BOTH cores with NO pad
input at all, so the title screen times out into TSM_Demo/TSD_DemoLevel and loads
`g_DemoLevelIds[0]` -- level 11. The game then replays a RECORDED input stream from the level's own
data (gamepad.c:165-180), so the pad is not a variable and the route needs no camera-relative
steering. That is what makes it the first route that crosses a LEVEL ENTRY, which docs/issues/0114
recorded as never compared.

## What matches

- `demo_playing` -- the level entry -- MATCHES on every decisive range: `gamestate`, `level_id` 11,
  `load_stage`, `game_tick`, `demo.mode`, `player.position`, `player.state`, the three `pad.*` words,
  `occlusion_result` and `state_switch`.
- 553 consecutive per-iteration comparisons MATCH, to `g_GameTick` 555.
- The overlay hand-off, the WAD load, the discard/reload of guest code at the reused load address and
  the invalidation that follows are therefore crossed on both cores with the state after them equal.

## What diverges

At `g_GameTick` 556, `player.position` (decisive):

| | X | Y | Z |
|---|---|---|---|
| product | 0x2f394 | 0x26493 | 0x5124 |
| console | 0x2f39d | 0x264c6 | 0x5122 |
| delta | **+9** | **+51** | **-2** |

growing to **(+59, +405, 0)** by tick 702. `camera` (informational) first differs at the SAME tick at
offset +0x28, which is `m_Position` -- downstream of Spyro, not upstream.

The offset grows and Z barely moves, which reads as an integration or scaling difference rather than a
missed input: +51 against +9 is a ratio, and +405 tracks +59.

## Ruled out, with evidence rather than argument

- **Input delivery.** The three `pad.*` decisive ranges match at every comparison, and during demo
  playback the guest owns the pad anyway.
- **The level entry.** `level_id`, `load_stage`, `gamestate`, `demo.mode`, `occlusion_result`,
  `player.state` and `state_switch` all match at tick 556.
- **A phase/shutter artefact.** The park is an iteration boundary -- immediately after `g_GameTick++`,
  the first statement of the GS_Playing update (update.c:1094) -- and the SAME byte pair recurs at
  ticks 556, 701 and 702 while Spyro's position is FROZEN on both cores. No phase offset produces a
  constant offset on a constant value. The phase artefact was real and was removed: with a field-granular
  unit alone, the product's position at field N equals the console's at N+1 on every one of 48 sampled
  fields, so the route now parks on `g_GameTick` inside demo playback instead.

## Also measured on this route, and not a defect

`load_stage` reads 1 on the product and 2 on the reference at `demo_level_load`. `g_LoadStage` is a
CD-paced progress counter, not a state: per-step census inside the load, the product advances a stage
every 2 fields and the reference needs up to 661 fields for one, and the two agree on 0 of 900
lockstep fields. Both cores load the same 13 stages in the same order to the same terminal -1, and
`load_stage` matches at all 554 later comparisons. A one-field shutter inside a blocking
`while (g_LoadStage < 6)` loop can and does move that counter by a stage.

## Per-word localisation (2026-09-26, `tools/probe_tick_divergence.py`)

The next measurement named above was made. The instrument is `tools/probe_tick_divergence.py`: it
drives both cores with the ROUTE's own predicates and advance, steps them in lockstep, and records
for every watched guest word the first iteration at which it differs, so the answer is the smallest
such tick read off a table rather than argued. `--ram-diff FIRST:LAST` widens the watch set to ALL
of main RAM (524,288 words per core).

Windows and denominators, each from one run:

| scan | what it compared | result |
|---|---|---|
| 580 iterations, ticks 3..582 | 540-word watch set, 310,880 word comparisons | 433 words equal on every iteration; 7 words already differed on the first sample; the rest first differed later |
| full RAM at ticks 555 and 556 | 524,288 words per core | 50,935 already differed at 555, 50,869 of those still differ at 556, and **107 words agreed at 555 and differ at 556** |
| 560 iterations, ticks 3..562 | the 107-word set re-checked | the same tick, the same values |

### The first guest word to differ

Not `player.position`. The full-RAM diff puts the collision raycast's own output first, in the order
the update produces it:

| address | symbol | product | console | delta |
|---|---|---|---|---|
| `0x80076B80` | `g_CollisionPoint` (`game.bss.s:28`) | 0x2F018 / 0x2650D / 0x4FB0 | 0x2F023 / 0x2653F / 0x4FC0 | **(-11, -50, -16)** |
| `0x80078A58` | `g_Spyro.m_Position` | 0x2F394 / 0x26493 / 0x5124 | 0x2F39D / 0x264C6 / 0x5122 | (-9, -51, +2) |
| `0x80078B64` | `g_Spyro.m_Physics.m_TrueVelocity` | -4064 / 512 / 128 | -3776 / 2144 / 64 | (-288, -1632, +64) |
| `0x80078B80` | `g_Spyro.m_Physics.m_TrueSpeed` | 0x1000 (4096) | 0x10DA (4314) | -218 |
| `0x80078AE0` | `g_Spyro + 0x88` (unnamed; read at `0x80042FF0`, `0x80043448`, `0x80043F4C`) | 1 | 0 | +1 |

Still EQUAL at tick 556: `g_CollisionNormal` (`0x80077368`), `g_CollisionTriangleIndex`
(`0x80075808`, 10708 on both), `m_Physics.m_Velocity` (`0x80078B58`), `m_Physics.m_Acceleration`
(`0x80078B4C`), `m_slopeAngle`, `m_againstWall`, `m_airTime`, `m_onSlope`, the whole 0x110-byte
camera, all 0xA8 bytes of `g_Pad`, `g_DemoDataPtr` (0x8007585C), `g_DeltaTime`, the level identity
and the load stage.

### What the numbers say

`m_TrueVelocity` is the movement the collision left over, in 1/32 units: its delta is **exactly 32x**
the position delta in all three components (-288/-9, -1632/-51, +64/+2), and `m_TrueSpeed` is the
faithful magnitude of each (4096 vs 4314 against |(-4064,512,128)| = 4098 and
|(-3776,2144,64)| = 4343). So the true velocity is a CONSEQUENCE of the position, not a second
independent fault, and the magnitude functions are not implicated.

`g_DeltaTime` is 2 on both (a demo assigns `g_UnprocessedFrames = 2`, gamepad.c:214), and the physics
runs `for (i = 0; i < g_DeltaTime; i++)` sub-steps, so an update integrates twice. `m_previousPosition`
(`g_Spyro + 0x8C`, 0x80078AE4) is written once per update, before the sub-step loop, and HOLDS the
tick-555 position on both cores at the park of 556. Therefore the whole (-9, -51, +2) is the sum over
the update's sub-steps, and the equal `m_previousPosition` says the sub-step loop started from equal
state.

The collision response is IDENTICAL on both cores. Console step (-125, +79, +4) with a retained true
velocity of (-118, +67, +2); product step (-134, +28, +6) with a retained (-127, +16, +4). Both lost
exactly 7 in x, 12 in y and 2 in z to the collision. Only the pre-collision MOVEMENT differs.

### The shape

Z is Spyro's UP axis, not depth: the floor normal is `(332, -255, 3328)` and `UpdateSlopeFloorCollision`
seeds "up" as `m_floorPositonOnSlope.z = 0x1000` (pete.c:755-757). So the offset is in the ground
plane with the height matching -- the product's Spyro is 9 units behind and 51 units off-line at tick
556 and 59/405 at 702, standing at the same height. Per-tick ground-plane steps at 556: console
`(-125, +79)` = speed 147.8 at heading 147.7 degrees; product `(-134, +28)` = speed 136.9 at heading
168.2 degrees. The product is 7% slower AND 20.5 degrees off heading in the same tick, and the
separation then grows because two equal-speed agents on different headings separate at second order.
That is why +405 tracks +59 at tick 702 without either being a ratio of the other: 405/59 = 6.9 while
the tick ratio is 702/556 = 1.26, so the growth is geometric, not proportional.

### Ruled out by measurement, not by argument

- **The recorded input stream and its pointer walk.** Over 580 iterations, 0 of the 42 words of
  `g_Pad` and 0 of `g_DemoDataPtr` ever differed. The demo's own words, including the STICKS at
  `g_Pad + 0x14` that the route's three `pad.*` ranges do not cover, are bit-identical throughout.
- **Every native override group in the physics path.** Rebuilt with `spyro_register_native_leaves`
  (copy3/zero3/fill/copyw) removed, and then with leaves + `native_vec` + `native_gte` + `native_angle`
  (vadd/vsub/vsra/angtblA/angtblB/angdist, mvmva/veclen/isqrt/vscale, angdiff8/spin60) removed as well,
  the divergence is BIT-IDENTICAL both times: tick 556, (-9, -51, +2), `g_Spyro + 0x88` = 1 against 0.
  Guest execution reproduces the product's behaviour exactly, so the fault is not in a hand-written
  body. (`VecRotateByMatrix` at 0x80017048 -- the `mvmva` override, which builds the collision ray --
  was additionally checked instruction by instruction against the real bytes in SCUS_942.28 and
  Beetle's COP2 field layout: CR0..CR4 and IR1..IR3 exactly as the guest writes them.)
- **The collision normal and triangle selection.** Both agree at 556, so the raycast hit the same
  triangle with the same normal and only the intersection POINT differs.
- **The store watch as a guest tracer.** `PSXPORT_CW` sees host-side stores only: armed on
  `g_Spyro.m_Position` it reported 8,970 hits, and every hit printed was from a native override
  (`0x80017758` vadd, `0x80017700` copy3); armed on `g_CollisionPoint` it reported 3 stores in a
  whole 560-iteration run, all three from boot, while the collision raycast writes that word on every
  call. It cannot attribute a guest-executed store, which is what the next step needs.

### The lead, and what the console's own PC observer says about it

`g_Spyro + 0x88` is 0 on both cores at ticks 552..555 and 1 on the product and 0 on the console at
556. The only store to it in the whole decompiled tree is the CLEAR at `0x8003FE7C` in `func_8003FE40`,
and it gates three branches in the physics: `bnez` at `0x80042FF0` (skip to `.L80043058`), a divide
compare at `0x80043448`, and a `bnez` at `0x80043F4C`. So one tick of this field decides whether a
movement adjustment runs, and it is the one small integer that differs in the same tick as the
movement.

Armed on the console for exactly the update at tick 556 (console field 7038) on `0x8003FE40`,
`0x8003FE7C` and `0x80041670` (`uv run --frozen python tools/probe_tick_divergence.py --ticks 557
--observe 0x8003FE7C,0x8003FE40,0x80041670:0x80078A80:16 --observe-at 556`), the observer reports
**scanned 677,984 matched 3 retained 3 dropped 0 pairing_errors 0, observation complete**:

| pc | instruction | registers |
|---|---|---|
| 0x8003FE40 | 27BDFF68 | v0=00000001 a0=801FFE80 |
| **0x8003FE7C** | **AC208AE0** | at=80080000 a0=80078B10 |
| 0x80041670 | 3C038008 | a0=0000003F a1=00000001 a2=0000000B a3=00002F00 |

`AC208AE0` is `sw $zero, -0x7520($at)` and `$at` holds 0x80080000, so the target is exactly
0x80078AE0 = `g_Spyro + 0x88`: the console EXECUTED that clear inside `func_8003FE40` in this update.
`a0 = 0x3F` at 0x80041670 is the sub-step index the `for (i = 0; i < g_DeltaTime; i++)` loop passes,
and the function was entered once in the update (retained 3, dropped 0).

The same observer with the RAM range on the field itself
(`--observe 0x8003FE7C,0x8003FE40:0x80078AD8:16`, 16 bytes at `g_Spyro + 0x80`; scanned 677,984
matched 2 retained 2 dropped 0, complete) reads, at BOTH records and therefore before the store:

| address | offset | value |
|---|---|---|
| 0x80078AD8 | +0x80 | 250 |
| 0x80078ADC | +0x84 | 4 |
| **0x80078AE0** | **+0x88** | **0** |
| 0x80078AE4 | +0x8C | 193432 |

So the console's field was already 0 when it cleared it, and the product's is 1 at the park with no
listed store that could put a 1 there. The two readings together say: the product executed a store to
0x80078AE0 with the value 1 that the reference did not execute, and it is not in the decompiled tree.

The call site of the console's clear is `func_80047B60 + 0x88` (0x80047BE8), immediately after
`func_8003E628` (the moving-platform capture) and immediately before `UpdateSlopeFloorCollision` --
the order that matters, because the field is cleared BEFORE the collision query and read by the
movement code after it.

### The next measurement

The console's path is observable per PC; the product's is not, and that is the actual gap.
`PSXPORT_CW` is host-side only, so nothing the product's Lightrec executes leaves a trace. The
framework already has the other half -- `psx::cpu::LightrecExecutor::configureStoreObserver`, which
reports the guest PC of a translated store together with the whole register file (the API
tests/test_dynarec_contract.cpp:623 uses) -- but it is a C++ API with no REPL or environment surface,
so nothing in this repository can arm it today. Exposing it (a REPL verb that arms a guest PC and
prints the hit's GPRs, product-side only, test-only) is what turns the next step from a bisection of
guesses into a comparison: arm both cores on the sub-step block at 0x80047B60 and find the first
instruction where the two register files differ.

## Open

The cause is not yet established, and it is now known to sit at or before `Moby+0x42`, which differs on
the very first sample of the window. What is established is where it is not: not the input, not the
level entry, not any native override, not the collision response, not the arithmetic of the movement,
not `dist2d`/`OctDistance`, not `D_800757F4` (which is downstream of the divergent byte), and not the
cyclorama scroll offsets (a field-cadence call-count residue). The console's own PC observer shows it
executed a store to `g_Spyro + 0x88` in the update that the product did not, and the product holds a 1
there that nothing in the decompiled tree can write.

The blocker on the next step is an INSTRUMENT GAP, not an idea: the reconstructed tree has no store of
any width covering `Moby+0x42` (429,885 instructions searched, abundant stores to `+0x40` and `+0x41`),
so the writer is in a function the decomp does not carry, and no instrument here can name it —
`PSXPORT_STORE_OBSERVE` needs the PC up front, `PSXPORT_CW` only sees the slow path, and the console
observer needs a PC up front too. Closing that needs either the missing decomp or an instrument that
can ask "which translated store last wrote this word" without being given a PC.

No fix is claimed and no tolerance is proposed: this is a decisive declared range, so it either matches
or it is a defect.

## Route result

The route exits 1. It is not a clean pass, and it is not tuned to be one: the level entry matches, 553
iterations match, and one real divergence 555 ticks in is reported rather than hidden. The second level
entry (the demo's return to the title and the next demo) is still uncompared, because the route stops
at the first decisive divergence.
