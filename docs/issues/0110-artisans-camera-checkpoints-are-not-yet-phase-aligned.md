---
id: 110
title: Artisans native and full-console camera checkpoints are not yet phase aligned
status: investigating
symptom: At the same Artisans level tick and player position, native and console game ticks and camera states differ before movement input
state_items: S011
tags: oracle,camera,gameplay,timing,input
created: 2026-09-12
updated: 2026-09-13
---

The earlier Left-60 camera delta in [issue 0102](0102-native-delivered-fields-undercount-guest-vblank.md)
was sampled at native REPL and console `retro_run` boundaries. Those boundaries do not by
themselves prove the same guest update phase. The previously qualified console PC target
`0x80033C50` is `CameraUpdateMatrices` in the render path; its recorded entry and return
camera bytes are identical, so that capture cannot locate a camera-update divergence.

## Reached comparison

The full-console arm used the USA Spyro CHD SHA-256
`8fe0a6e735ee399a8251f2173cf61c6e20fa565611b934fa3d90788beab9d6cb`, admitted
SCPH-1001 v2.2 BIOS SHA-1 `10155d8d6e6e832d6ea66db9bc098321fb5e8ebf`, pinned
Beetle fork `5791d27a41e7ffd759f68e88114b50a1c6a168c0`, empty saves, and the
40-command recorded title-to-Artisans input route from the framework's console observer
qualification. The route was accepted only after reading stage 0, level tick 300, and
player `(84992, 47173, 9556)` from live RAM at console field 6738.

The native/Lightrec arm used the authenticated local executable and the `tools/drive.py`
state-driven New Game route **without** transition cancellation. It reached stage 0 and
advanced without input to level tick 300 at product step 6653. At that boundary:

| RAM field | Native | Full console |
|---|---:|---:|
| `g_Gamestate` | 0 | 0 |
| `g_LevelTicks` | 300 | 300 |
| `g_Spyro.m_Position` | `(84992, 47173, 9556)` | `(84992, 47173, 9556)` |
| `g_GameTick` | 147 | 150 |
| `g_Camera.m_State` | `0x80000010` | `0` |
| `g_Camera.m_Position` | `(84992, 44716, 10171)` | `(84992, 44702, 10174)` |

This is a matched level-tick/player checkpoint but **not** a matched game/camera phase.
The same native game-tick and camera-state mismatch appeared with transition cancellation
enabled, so the explicit skip command alone does not explain it. No guest state was written
to force alignment.

The source's `jal CameraUpdate` at `0x8002F48C` encodes target `0x80037BD4`.
An authentic-console observer on that target captured 36 entries and 36 saved-return
arrivals during a 72-field held-Left interval: 24,863,567 instructions scanned, zero
drops, pairing errors or pending returns. The entry/return records show camera state and
position changing, which positively checks that the target observes the intended work.
The same observer ABI's prior qualification also found zero matches with nonzero scanning
for unreachable PC `0xFFFFFFFC` and preserved RAM/video/audio hashes with observation
on and off. This run's endpoint samples are in gitignored
`scratch/oracle-comparison/console_movement.json` and `native_natural.json` until reviewed.
Native execution exited cleanly after 6,726 fields and 3,574 product steps, with
24,827,046 Lightrec blocks executed and zero fallback blocks/instructions.

Left movement is therefore a **positive reach discriminator**, not a parity result:
after 72 delivered fields the native player was `(84366, 45983, 9790)` and the console
player was `(84366, 45943, 9799)`, while their game ticks still differed by three.
The unchanged three-tick gap across this interval directs the first-divergence search
toward the New Game handoff, before the measured movement interval, but does not by itself
identify a faulty scheduler or camera update.
Neither an end-of-field image diff nor a camera-position delta can be attributed to
camera math until the first earlier lifecycle/tick divergence is found.

## Narrower handoff observation and its limit

The authentic-console arm replayed the same recorded input prefix, then sampled each field
from 6438 to 6578 with the admitted BIOS and disc. At Artisans level tick 1/game tick 1,
`g_Camera.m_State` became `0x80000010`; at tick 91/game tick 46 it became
`0x80000011`; at tick 107/game tick 54 it reached zero and remained zero through
tick 140/game tick 70. The camera target state at `g_Camera+0xC0` followed those
transitions. This is a positive lifecycle observation, not an empty camera trace.

Two native REPL probes used the same state-driven New Game route and delivered fields
one at a time after the scene reset. They did **not** reach the first game update at a
stable level tick: one first sampled game tick 1 at level tick 8 and camera state
`0x80000010` at tick 9; the other first reached game tick 1 and that camera state at
level tick 19. In the first run, camera state `0x80000011` appeared at game tick 46,
then zero at game tick 54, matching the console's *game-tick* transitions. The next
native sample at game tick 55 restarted state `0x80000010`, unlike the console's zero
state at game tick 55. The second run only followed the entrance to game tick 46;
it also reached `0x80000011` there. The native field-level probes therefore locate
a **candidate** post-entrance divergence but cannot establish that it is present in
ordinary uninterrupted execution. The differing initial level ticks and REPL stop
points show that level-tick endpoints from this method are not phase stable. Neither
probe wrote guest RAM. Their bounded traces are in gitignored
`scratch/oracle-comparison/console_cause.json`, `native_phase.json`, and
`native_cause.json`.

## Completed stage-update return discriminator

The native frame driver calls guest stage update `0x8003385C` through Lightrec.
The full console calls it from `0x80012230`, returning to
`0x80012238`. With the admitted disc, BIOS, Beetle fork, empty saves, and the
recorded 39-command route prefix from the earlier comparison, a 144-field
observer window captured 72 entries and 72 saved-return arrivals. It scanned
52,025,631 instructions, with zero drops, pairing errors or pending returns.
Independent fresh console sessions had equal field count (144), audio frames
(106,148), and all three on/off SHA-256 hashes: RAM
`ffee380b8b7e1e2ef8b583dfb464aa2b89f87ca66234470a286d36a089cba54a`,
video `07c928f9a71f040334415be62548c27b327840ae998a0b81ef3eebbe1ce48e8b`,
audio `60a727fd2d91fecfee18b471b8fde8cc39c6c2d450e00d3ba41db27f69df2a67`.
The console observer was reached and did not perturb this measured output window.

An experimental title-only hook sampled native RAM after this outer guest
function returned, before the host tail. It recorded 1,922 completed returns
after arming, including 68 stage-0 samples through game tick 67. At game tick 1,
console/native level ticks were 1/7 and player positions were
`(84992,47116,9570)`/`(84992,47125,9570)`; camera state and entrance timer
both read `0x80000010` and 44. Both arms showed camera state `0x80000011` at
game tick 46 and zero at tick 54. At tick 55, console remained at state and
target state zero with entrance timer zero, while native read both states
`0x80000010` and timer 44. These are **experimental outer-return readings**;
they cannot establish the nested `CameraUpdate` return or a camera root cause,
and the native instrument was removed after the output controls below failed.
The native run used the authenticated `SCUS_942.28` executable, the same
configured CHD path as the console, Clang 22.1.8, and the then-current dirty
framework tree at `b3fbe300` (`8aecc52-dirty+psxport-b3fbe300-dirty` build
stamp). It exited after 6,494 fields and 3,459 product steps with 20,679,712
Lightrec blocks executed and zero fallback blocks/instructions. This is not a
release-conformance result for the subsequently committed framework tree.

The first native off/on New Game run was invalid as a control: the runs had
already diverged **before arming** at product fields 2640/2641, and their
level-tick-140 endpoints had game ticks 66/67. A stricter six-run diagnostic
started at the first pre-step REPL prompt (labelled frame 1), captured identical
full 2 MiB pre-command RAM SHA-256
`85d254b2a0c9b7b9347e0fbb70790a1e4d5fd7af0ff37aa31e17ce00edd9c1a9`
and identical stage/game/level/camera/player words in every fresh process, then
ran exactly 600 fields with no input. All ended at stage 13, game tick 0,
level tick 602, camera state 0, and player Y 0. Every command in the table
carries the `stage-observe` prefix; the six arms were:

| First-prompt command(s) | Completed returns scanned / target matches | RAM SHA-256 | Frame SHA-256 | WAV SHA-256 |
|---|---:|---|---|---|
| `status` (two independent runs) | 0 / 0 | `947faa000e87badc42b46b2c45f77f3f0161c6099212c367555b8e24cf02e3c8` | `32bbfde9b8d2fe06cf732b76f5169639cadd43fb98a1aec97fd65e1a785d5b7d` | `0f54ec0ba4502d45e7f625eeb727b69a204ff969dafd9c2d7a2cfd7bb61c7791` |
| `on` at `0x8003385C` | 83 / 83 | `1af29528dd2d612d6f5185ee17ca5a8467d104c87212c960219fa83b0c666b0a` | same as `status` | `e4ef40c77d50c4f0800d1d7fec73b1a5131654e66c24e4e9b30434f492264a18` |
| `on FFFFFFFC` | 83 / 0 | same as `on` | same as `status` | same as `on` |
| `on; off` | 0 / 0 | `61a013c242bddb46bc5434dcd935fb32c493e4fd6870b2c6b8b9455f1907ab16` | `e543ec7446dc6a738632a1c75aece7853959358bc2f16417ec57ae3f85b33563` | `85312d08c69bd00b487ec1b1ceac206331377dfac9266054923e829a95cee09e` |
| `off; off` | 0 / 0 | same as `status` | same as `status` | same as `on` |

The two `status` controls repeated exactly, as did reached and unreachable
armed output; this is not a missing-target or uniform-hash instrument. Yet
`on; off` changed all three outputs with **zero return scans during the run**,
and `off; off` changed WAV alone. Thus the first-prompt command sequence can
change output even when the observer is disabled during execution. The armed
versus unarmed hash difference cannot be assigned to the read-only per-return
counter or camera sampling. The armed/unarmed RAM difference was six bytes in
`g_Pad`'s buffered-input slot (`0x800773F6..F7`, `0x80077400..03`); WAV PCM
first differed at file byte 1,283,168. This bounds the observed difference,
not its timing or ownership cause. The temporary native hook is not in the
shipping tree; its bounded raw diagnostics remain gitignored in
`scratch/oracle-comparison/stage_return_*` for review.

## Next discriminator

The title now has a startup-configured (`PSXPORT_DEBUG=stage-observe`), read-only
observer at the completed outer `StageUpdate` return. It starts before any REPL
command and records at most the first 128 stage-0 returns, with scanned,
matched, gameplay, recorded, and omitted denominators at shutdown. A focused
synthetic test exercises reached, unreachable, disabled, and non-gameplay
returns through the shipping sampler. A real-product state-driven New Game run
reached stage 0 and reported 3,017 completed returns, 3,017 matches, 63
stage-0 samples, zero omissions, 20,339,081 Lightrec blocks executed, and zero
fallback blocks/instructions. Its first sampled game tick 1 occurred at level
tick 23, while earlier REPL probes first sampled it at ticks 8 and 19. This
observer fixes the **sample location within one native run**, not the variable
New Game handoff across runs.

Three fresh no-input controls used the identical first-prompt command sequence
and 600 delivered fields, with observation disabled twice and enabled once.
One triplet had identical 2 MiB pre-command RAM, final RAM, frame, and WAV
hashes; the enabled arm positively scanned and matched 83 returns. In another
triplet, the pre-command RAM and final frame matched, but enabled versus
disabled final RAM differed by two bytes at `0x80075950/54`: each `VSync(-1)`
sample read 600 rather than 599. WAV hashes differed too. An earlier triplet
also had different final RAM between **two disabled** arms, despite matching
pre-command RAM and frames. These observations show output is not uniformly
repeatable under the current REPL control and do not isolate observer impact.
The saved bounded controls are in gitignored
`scratch/oracle-comparison/stage_startup_control_*`.

Resolve the VSync/output variance and align the pre-update New Game handoff
before inferring native/full-console camera parity. Require reached and
unreachable observations and identical native RAM, frame, and audio hashes
from identical initial state under a control that reproduces without the
observer. Compare the paired **outer-return** phase before pursuing a nested
`CameraUpdate` return. Do not change camera math or scheduler timing to fit
unmatched endpoints.

## Guarded outer-return discriminator

The authenticated `SCUS_942.28` assembly at `func_800357A4`,
`0x80035C60..0x80035D3C`, grounds the branch from camera target state zero
to `0x80000010`. It requires `g_Spyro+0x194` (`0x80078BEC`) to be zero,
camera mode `0x80075914` bit `0x10` or look mode `0x8007592C` to be nonzero,
and `g_Camera+0xF0` (`0x80076EC0`) to be zero. It then calls
`func_80017AA4` to project Spyro through the current camera. The branch
stays at zero only when the projected point is within 64 pixels of screen
X=256, within 40 pixels of Y=120, and its **unsigned** depth is at most
`0x1400`; otherwise it selects `0x80000010` and stores 45 to the timer at
`0x80075938`. These are the executable's branch conditions, not an inferred
screen position from the player world coordinates. Ghidra's
`build/decomp/800357a4.c` agrees with this branch; its overlapping-global
warning is why the assembly is the address and threshold authority.

The existing native startup observer now reads only those four guard words
and the timer alongside its prior camera/player fields at the **completed
outer StageUpdate return**. Its focused Clang observer test includes reached,
unreachable, disabled, and non-gameplay arms and checks the added read-only
fields. The full-console observer used seven static RAM ranges (312 bytes per
record) over the existing authentic-BIOS/disc route, fields 6438..6582.
Three fresh 144-field console processes exited cleanly. The reached target
scanned 52,025,631 instructions and retained 72 entries plus 72 returns,
with zero drops, pairing errors, or pending returns. The unreachable
`0xFFFFFFFC` target scanned the same denominator and matched/retained zero;
the disabled arm retained nothing. RAM, frame, and audio hashes were equal
in all three arms and reproduced the hashes above. Their raw captures are in
gitignored `scratch/oracle-comparison/stage_return_console_{off,on,unreachable}.json`.

At game tick 54 then 55, console completed returns at fields 6545/6547,
level ticks 107/109, held camera state and target state at zero, timer zero,
player `(84992,47173,9556)`, and guard words
`(camera mode, look mode, player gate, camera block)=(0x52,0,0,0)` in both
samples. One native `PSXPORT_DEBUG=stage-observe` natural-New-Game run from
the first REPL prompt exited cleanly after 6,482 delivered fields and 3,453
product steps, with 20,254,111 executed Lightrec blocks and zero fallback
blocks/instructions. It scanned/matched 3,017 completed StageUpdate returns,
recorded 63 of 63 stage-0 returns, and omitted zero. At game tick 54 then 55,
native level ticks were 110/114; camera state and target state changed from
zero to `0x80000010`, timer from zero to 44, and the four sampled guard words
were also `(0x52,0,0,0)`. Its player was `(84992,47173,9557)` and camera
`(84992,44702,10175)` at both returns. Raw log and parsed records are in
gitignored `scratch/oracle-comparison/stage_guard_native.{log,json}`. This
diagnostic built from Spyro `a32db9b` plus the observer worktree against the
shared psxport dev clone at `8b210329-dirty`; it is not a pinned-framework
conformance run.

The four sampled non-projection guards do not separate the tick-55 result.
The native player Z differs by one from this console run, and the New Game
handoff still varies by level tick, so the endpoints are not fully aligned.

## Branch-time projection discriminator

Authenticated `external/spyro-1/asm/math.s` at `0x80017B20..0x80017B44`
shows that `func_80017AA4` runs RTPS, reads GTE SXY2 and MAC3, sign-extends
each 16-bit screen coordinate, and stores the unchanged 32-bit MAC3 depth to
the caller's three-word local. The caller's exact return PC `0x80035CC8`
precedes its stack loads, so console GPRs `$v0/$at/$v1` there still hold the
actual `(x,y,depth)` written by the function. The native title observer taps
the existing per-Core GTE pre/post boundary only during the outer StageUpdate
call. It filters the RTPS opcode by the current camera matrix, zero GTE
translation, and the player/camera vector, then records SXY2/MAC3 and the
five branch-time guard words; a result is valid only when exactly one RTPS
matches in that call. Lightrec does not supply this callback an exact guest
instruction PC, so the operand match and uniqueness are attribution limits.
The focused Clang synthetic test runs a real RTPS through the shipping GTE
path, verifies a known `(-100,-200,1000)` projection against the same register
ports and signed/unsigned conversions, and rejects wrong vector, wrong
matrix, two-candidate ambiguity, disabled, and unreachable arms. It also
checks that VZ0 compares as a signed 16-bit GTE port rather than a 32-bit
world-coordinate difference.

Three fresh authentic-BIOS/CHD console processes began at field 6438 with
identical full-RAM SHA-256
`d7e1b645aa1b68ebd023f1593423b38d02f26d08b7e2dcfacbd0053086e85a7e`.
Over fields 6438..6582, the exact `0x80035CC8` observer retained 19 hits
from 52,025,631 scanned instructions, with no drop or pairing error. The
unreachable `0xFFFFFFFC` arm scanned the same 52,025,631 instructions and
retained zero; the off arm retained nothing. All three arms had 144 completed
hash fields, 106,148 audio sample frames, and equal full-RAM, frame, and audio
SHA-256 hashes (`ffee380b8b7e1e2ef8b583dfb464aa2b89f87ca66234470a286d36a089cba54a`,
`07c928f9a71f040334415be62548c27b327840ae998a0b81ef3eebbe1ce48e8b`,
`60a727fd2d91fecfee18b471b8fde8cc39c6c2d450e00d3ba41db27f69df2a67`).
The positive arm reached stage 0 and captured console tick 55/level tick 109
at field 6547: projected `(256,120,2546)`, target state zero, guard words
`(camera mode, look mode, player gate, camera block)=(0x52,0,0,0)`, and timer
zero. These are inner-return values before the projection thresholds execute.

One native New Game process built against clean psxport `e747e9d3` exited
normally at field 6482, with 3,453 product steps, 20,081,932 executed
Lightrec blocks, and zero fallback blocks/instructions. It scanned/matched
3,017 completed StageUpdate returns, recorded 63/63 gameplay returns with
zero omission, and scanned 20,603 GTE operations including 374 RTPS; exactly
two camera-vector/matrix candidates were uniquely captured in their calls.
At native game tick 55/level tick 114, the one matching projection was
`(100,120,2546)` with the same branch-time target and four guard words as
console. The subsequent outer return had target/current state `0x80000010`
and timer 44. The source branch rejects X=100 because `abs(100-256)>64`;
console X=256, Y=120 and depth 2546 meet all three bounds. A second native
unique sample at game tick 1 projected `(100,171,1510)`, while the exact
console PC captured `(256,172,1501)` at its tick 1; both X observations
differ by 156, but those earlier 3D states also differ.

The unique native candidate's X=100 would take the observed branch; the
console's exact-PC X=256 stays within its bound. Without an exact native
instruction PC, the operand match cannot prove that candidate is the
function's stack local, and it does **not** identify why X differs. The
native and console handoffs remain unaligned (level ticks 114 versus 109 at
game tick 55), and native player/camera Z are each one higher. The next
narrow discriminator is the GTE projection inputs at this branch, especially
OFX/OFY/H and the camera rotation matrix, compared at the same inner phase.
Do not change camera math or field scheduling from this result alone. Raw
console captures
are gitignored under `scratch/oracle-comparison/stage_projection_console_*`;
the native log/parsed capture are
`scratch/oracle-comparison/stage_guard_native.{log,json}`. The Spyro build
identity was `a32db9b-dirty+psxport-e747e9d3`; this was an observation run,
not a pinned Spyro conformance gate.

## Raw GTE input discriminator

Pinned Spyro `b8efd45`/psxport `e747e9d3` was observed without rebuilding or
changing guest state. A later fresh native New Game run **failed the tick-55
admission before attaching GDB**: at game tick 53, field 6471, its camera
target was already `0x80000011`, so a state-zero tick-55 comparison in that
run would be meaningless. The process exited normally; this is a negative
handoff observation, not an attempt that was retried until it passed.

Two independent, fresh exact-console-PC one-field observations used the same
authentic BIOS/CHD and field-6438 pre-arm full-RAM SHA-256 as the prior
off/on/unreachable controls. At `func_80017AA4`'s RTPS PC `0x80017B20`,
tick 55/field 6547 retained **1 of 346,026** scanned instructions; tick
1/field 6439 retained **1 of 254,066**. Neither dropped a record or reported
a pairing error. Read-only GDB inspection of the core's bound GTE register
file at the corresponding RTPS found, at **both** console ticks, OFX
`0x01000000` (256 in 16.16), OFY `0x00780000` (120), H `0x155` (341),
and translation `(0,0,0)`. At tick 55, camera matrix CR0..4 was
`(0,0x026D1000,0x9B3,0x03E3F07B,0)` and V0 was
`(VXY0=0x026AF659,VZ0=0)`; at tick 1, matrix was
`(0,0x1000,0xA00,0xF000,0)` and V0 was
`(0x016FFA23,0)`. These vector and matrix values also match the exact-PC
GPR and RAM record, grounding the GDB RTPS association rather than relying
on opcode alone.

A single fresh native run armed a read-only GDB breakpoint only at the
shipping title observer's **unique matched RTPS post-op path**. It reached
one candidate at game tick 1/level tick 14 and exited normally after 2,962
matched StageUpdate returns, 9/9 gameplay samples, 5,686 GTE operations,
373 RTPS, one unique camera candidate, 17,835,240 executed Lightrec blocks,
and zero fallback blocks/instructions. Its tick-1 raw GTE inputs were OFX
`0x00640000` (**100**), OFY `0x00780000`, H `0x155`, the **same** camera
matrix and zero translation as console tick 1, and
`(VXY0=0x016FFA10,VZ0=0)`. At the inner phase, both camera positions were
`(84992,45615,9937)`; native player Y was 47135 versus console 47116,
which accounts for the 19-unit packed-vector difference. Native RTPS output
was `(100,171,1520)` versus the exact-PC console function result
`(256,172,1501)`. In this paired phase, screen X equals each side's OFX
integer center, while OFY, H, camera matrix, and translation agree. OFX
therefore identifies the immediate tick-1 X-input discrepancy; the player
position and New Game handoff still differ, and the native candidate lacks
an exact guest PC. The tick-55 native OFX was **not** directly captured by
this run, so its earlier X=100 result cannot be assigned to OFX from this
pair alone.

`Spyro1::BootSequence::initialize` calls the guest `SetGeomOffset` with
`(0x100,0x78)` and `SetGeomScreen` with `0x155`, so native boot initially
publishes the console's 256/120/341 values. A later write or missing state
restore must account for native OFX=100 at the tick-1 projection; the last
**reached** OFX writer and its return-state contract are not identified.
Title-native screen sprite submission and other producers can write GTE
CR24, but a source call site is not evidence that it wrote this value on
this route. The next discriminator is a bounded last-writer trace before
the unique camera RTPS, with the guest and native producer boundaries
distinguished. Do not override OFX to fit this sample. Raw captures are
gitignored under `scratch/oracle-comparison/stage_gte_console_*` and
`stage_gte_native_tick1.json`. The GDB-paused runs were not subjected to a new
on/off output-hash comparison; these snapshots qualify the registers at the
stop, not the subsequent field schedule or rendered/audio output.

## OFX writer source discriminator

The authenticated local `SCUS_942.28` SHA-256
`a533d75cab8afaae6107ec35a02a9a5fe979a92c7c955f9cf1ee50f693a1b998`
loads 103,936 aligned words at `0x80010000`. Decoding the complete loaded
image found four `ctc2` instructions targeting control register 24, at
`0x80022D2C`, `0x8002395C`, `0x800623BC`, and `0x80062620`. The positive
sprite-queue actor write at `0x80022D2C`, queue-exit restore at `0x8002395C`,
and libgte `SetGeomOffset` write at `0x80062620` each matched one decoded
word. Unreachable address `0xFFFFFFFC` was outside the loaded image and
matched zero of the same 103,936-word scan. This is a **static inventory**;
it does not establish which instruction ran on the New Game route, and does
not cover WAD overlay code.

The queue's authenticated assembly writes an actor's X to OFX at
`0x80022D2C` and has an exit arm at `0x80023958..80023964` that writes
`0x01000000` (256) to OFX and `0x00780000` (120) to OFY. The native
`fx_sprite_queue.cpp::setup_screen_gte` likewise writes the actor's X to
OFX, but its containing `emit_screen_queue` returned without restoring the
screen center. Its stage-13 mode-3 text builder has an `x = 100` branch and
stores that X in the actor field later consumed by `setup_screen_gte`.
This formed a concrete restore-contract hypothesis for the native tick-1
OFX=100, separate from guest `SetGeomOffset` HLE.

A first-prompt GDB hardware watchpoint on per-Core CR24 started at
`0x01000000` against the existing pinned Spyro/psxport `e747e9d3` build.
Its overhead prevented it from reaching the tick-1 camera candidate within
the allotted retail slot. The run was terminated before its fixed route
endpoint, without a retry or cap extension; it produced no writer-event
denominator or new parity claim. Its driver truncated the earlier
`stage_gte_native_tick1.log`; the parsed JSON and issue values above remain,
but that earlier raw native log is no longer available.

The replacement trace sampled the native stage-13 mode-3 queue's actual actor-write and exit
boundaries, then the first camera RTPS on the same state-driven route. The bounded shipping run
used the authenticated local `SCUS_942.28` named above and opened the configured USA CHD; the
comparison console provenance above includes the CHD SHA-256 and admitted BIOS SHA-1. It reached
`GS_Playing` at frame 6361 and exited normally after 6362 delivered fields, 17,970,369 executed
Lightrec blocks, and zero fallback blocks/instructions. With the opt-in `stage-observe` sink,
**771/771** native sprite queues exited, **9610** actor OFX writes were observed, **244/9610**
wrote `0x00640000` (100), and unreachable actor sentinel `0xFFFFFFFC` matched **0/9610**.
Completed StageUpdate returns were **2959/2959** matched, with **4/4** gameplay samples and
**1/1** unique camera projection among 373 RTPS operations. The raw read-only run log is
gitignored at `scratch/oracle-comparison/stage_ofx_queue_live.log`.

The last reached stage-13 queue, ordinal 771, contained 21 actor writes. It **entered already at**
CR24 `0x00640000`, CR25 `0x00780000`; the preceding actor left CR24 `0x00740000`, then final
actor `0x801A3B58` wrote it back to `0x00640000`. The native queue exited at the same value. At
the immediately following unique stage-0/game-tick-1 camera RTPS, CR24 remained `0x00640000`,
CR25 was `0x00780000`, H was `0x155`, and projected X was 100. Thus this route proves a reached
native actor write **kept** OFX at 100 and the native queue failed to restore it. It does not prove
how OFX first became 100 before queue 771, nor enumerate all possible intervening CR24 writers;
the guest `SetGeomOffset` HLE is distinct from these title-native producer writes.

The authenticated `r_moby.s` loop branches from `0x800232A8` to `0x80023958` on queue completion.
The latter arm writes CR24=`0x01000000` at `0x8002395C` and CR25=`0x00780000` at `0x80023964`
before subsequent queue-tail work. That control flow and register contract, together with the
reached native missing restore above, grounds the title-local native `emitScreenQueue` exit write.
The production queue now restores both registers on completion regardless of whether a native
actor/primitive variant was refused; it reports that refusal separately. A focused shipping-path
synthetic test reaches the actor transform with OFX=100, checks exit OFX=256 and OFY=120, then
checks a refused transform and an empty queue: zero actor writes in each, the same guest exit values,
and an empty-queue entry of 321 that cannot pass through a saved-entry restore. The combined Clang
gate passed 35/35 tests and checked 162 first-party translation units.

One post-change state-driven retail run reached `GS_Playing` at the same frame 6361 as the prior
run and exited normally after 6362 fields. The queue again entered/exited **771/771** times with
**9610** actor writes, **244/9610** at OFX=100, and **0/9610** sentinel hits. Queue 771's final
actor `0x801A3B58` still wrote OFX=100, but its exit was now `0x01000000`; the unique first
stage-0/game-tick-1 camera RTPS read that value and projected X=256. Player
`(84992,47125,9570)` and camera `(84992,45595,9953)` matched the earlier native tick-1 sample;
Y=171 and depth=1510 also matched it. That pairs the X correction with the reached source write
and guest-grounded exit restore. The New Game handoff still varied (native level tick 5 before,
7 after at game tick 1), and this run did not compare full RAM, frame, or audio output with an
independent console. It therefore proves the local CR24/projection contract, not complete camera or
visual parity. The raw logs are `scratch/oracle-comparison/stage_ofx_queue_live.prev.log` before
and `stage_ofx_queue_live.log` after; both are gitignored.

## Next lifecycle discriminator after the queue restore

A bounded read-only scan of the authenticated `SCUS_942.28` executable above checked the loaded
103,936 aligned words and found all 12 expected instruction words at their exact PCs (12/12).
Unreachable PC `0xFFFFFFFC` was outside that loaded range (0/103,936). The adjacent
`LoadLevelScene` zero stores for `g_LevelTicks` at `0x80013698` and `g_GameTick` at `0x800136A0`
each matched once in the whole loaded image (1/103,936 apiece). In the stage-update body
`0x8003385C..80033C50`, the `g_GameTick` store word appears once in 253 aligned words, at
`0x80033A6C`. In entrance-update `0x8002E000..8002E084`, the stage-zero store word appears once
in 33 aligned words, at `0x8002E070`. These are static instruction and control-flow facts, not
runtime hit counts; the scan does not cover WAD overlays.

The stage-0 dispatch branch at `0x80033890` reaches the game-tick load/add/store at
`0x80033A58/64/6C`. The stage-9 dispatch at `0x80033954` calls entrance update `0x8002E000`,
then jumps to the common tail `0x80033C30`, bypassing that game-tick store even if entrance update
changes the stage during the call. Entrance update tests camera rotation Y `< -512` at
`0x8002E048..4C` or spherical preset `0x8006CA84` at `0x8002E054..64`; either true route reaches
the stage-zero store at `0x8002E070`. The next stage-update call can then increment `g_GameTick`
and reach normal `CameraUpdate` through `0x80033B4C` → `0x80037BD4`.

The first exact-PC console attempt **falsified** the presumed stage-9 entrance
route for this New Game replay. The pinned observer accepts four PC targets,
eight RAM ranges, and 128 queued records per configuration. Four fresh
authentic-BIOS/disc arms replayed the same 38-command prefix to field 5,838;
all four pre-arm full-RAM SHA-256 values were
`6df9aec68f762d2e024b61d9dc57cb22723bcd90629c83cc1a07875cfea7a9c4`.
The disc, BIOS, and Beetle fork identities are the ones admitted above. Each
arm used exact pre-instruction PC snapshots, no guest writes, five bounded
main-RAM ranges, and a 128-record queue.

| Arm | Field range after arming | Scanned instructions | Exact-PC entries | Dropped |
| --- | ---: | ---: | --- | ---: |
| Level-scene reset | 5,838–6,438 | 156,863,584 | `0x80013698/9C/A0/A4`: 1 PC snapshot each; only `98` and `A0` are stores | 0 |
| Presumed entrance plus game tick | 5,838–6,538 | 193,006,836 | `0x8002E070/74`: **0 each**; `0x80033A6C/70`: 50 PC snapshots each; only `6C` is a store | 0 |
| Unreachable sentinel | 5,838–6,538 | 193,006,836 | `0xFFFFFFFC`: **0** | 0 |
| Loader-side stage-zero candidates | 5,838–6,438 | 156,863,584 | `0x80013B4C/50`: 1 PC snapshot each; only `4C` is a store; `0x80016268/6C`: 0 each | 0 |

Every positive record was retained; pairing errors were zero. At field 6,438,
the `0x80013698` store changed level tick 5,326→0 while game tick stayed zero and
stage stayed 13. The `0x80013B4C` store then changed stage 13→0 with both
ticks still zero. At the first `0x80033A6C` store in field 6,439, stage was
zero, level tick one, and game tick changed zero→one. The 50 observed tick
increments continued through field 6,537. The unreachable arm scanned the
same 193,006,836 instructions as the entrance arm and retained no record.
Raw read-only records and denominators are gitignored at
`scratch/oracle-comparison/handoff_console_{reset,handoff,sentinel,loader_stage}.json`.

The decomp's no-level-transition branch in `LoadLevelScene` writes
`g_Gamestate=GS_Playing`; the exact `0x80013B4C` store is now reached on this
route. The stage-9 entrance branch remains a real binary path, but it did not
run in this comparison window. The corrected handoff sequence is therefore
**level-scene reset → loader-side stage-zero store → first stage-zero game-tick
store**. It identifies the console's route, not the native first divergence.
This new window did not repeat the earlier observer off/on RAM, video, and
audio hash control, so it does not independently requalify instrumentation
effects at this boundary.

### Exact-PC native observation boundary

`Core::pcObserver` and `Core::storeWatchCb` cannot attribute an interior translated
store: Lightrec synchronizes `Core::pc` only when its execution segment returns,
and the memory watch callback has no instruction PC. The shared Lightrec selected-
store observer (fork `9a982a6`, per-Core psxport bridge `ff3709e7`) now reports
pre/post state at an exact translated SW PC, retires warm translations on arming,
and fails closed with a typed fault when optimizer provenance is unsupported.
The title's opt-in `PSXPORT_DEBUG=handoff-store` capture targets the four reached
stores `0x80013698`, `0x800136A0`, `0x80013B4C`, and `0x80033A6C`, plus
unreachable `0xFFFFFFFC`. It reports per-target before/after hits over executed
JIT instruction count and fallback count. Capture retains the first eight routine
tick increments, every reset/stage/non-monotonic store, and the next eight stores
after each such transition; the fixed 256-record capacity fails closed if an
interesting event would be lost. It reports hits, recorded, and omitted by class.
The admitted executable's SHA-256 is
`a533d75cab8afaae6107ec35a02a9a5fe979a92c7c955f9cf1ee50f693a1b998`;
its words at `0x8001369C`, `0x800136A4`, and `0x80013B50` are `LUI`, while
`0x80033A70` is `BEQ`. The console observer's hits at those adjacent PCs remain
valid pre-instruction snapshots, but they are not stores and must not be passed
to Lightrec's selected-store API. The title's synthetic shipping-path test
qualifies four reached SW PCs, a zero-hit sentinel, on/off guest-word equality,
and zero interpreter fallback. A second synthetic arm runs 80 routine tick
stores before the reset and verifies the later level/game reset, stage-zero
store, and first post-reset tick remain recorded while 72 routine events are
explicitly omitted. The focused Clang test passed 173 checks: the immediate
arm reached all four stores once over 16 JIT instructions, and the long-prefix
arm reached 84 paired stores over 336 JIT instructions, retained 12, omitted
72 routine events, hit the sentinel zero times, and used zero fallback
instructions in both arms.

### Reached native exact-store handoff

One serialized native run used `tools/drive.py gameplay` with the state-driven New Game route,
no transition cancellation, `PSXPORT_DEBUG=handoff-store`, a fresh empty card/settings path,
headless and silent output, and normal pacing (`PSXPORT_NOPACE=0`). The admitted executable
SHA-256 was `a533d75cab8afaae6107ec35a02a9a5fe979a92c7c955f9cf1ee50f693a1b998`,
CHD SHA-256 was `8fe0a6e735ee399a8251f2173cf61c6e20fa565611b934fa3d90788beab9d6cb`,
the built `spyro_port` SHA-256 was `31c985e57c0a427d7f7f59a8ec3c310ce19918db7e86d55e3585bc24f7ed8518`,
and the built/recorded psxport revision was `ff3709e74b24d21de4f3dcdcd402de8c33d57df7`.
The driver reached `GS_Playing` at its REPL frame 6,381 and exited normally after frame 6,421
(6,421 delivered fields, 3,423 product steps, zero Lightrec faults or fallback instructions).
The owned game and driver processes exited. The complete capture is gitignored at
`scratch/oracle-comparison/handoff_native_store.log`.

| Native SW PC | Pre → post target word | Stage | Level tick | Game tick | Hits (before/after) |
| --- | ---: | ---: | ---: | ---: | ---: |
| `0x80013698` level reset | 6,371 → 0 | 13 → 13 | 6,371 → 0 | 0 → 0 | 1/1 |
| `0x800136A0` game reset | 0 → 0 | 13 → 13 | 0 → 0 | 0 → 0 | 1/1 |
| `0x80013B4C` loader stage zero | 13 → 0 | 13 → 0 | 0 → 0 | 0 → 0 | 1/1 |
| `0x80033A6C` first game tick | 0 → 1 | 0 → 0 | **3 → 3** | 0 → 1 | 23/23 total |

The observer scanned **121,330,865 JIT instructions**, captured 26 paired stores,
retained 19, and omitted seven routine increments; all three transition stores
and all eight post-transition neighborhood stores were retained. The unreachable
sentinel was **0/121,330,865** JIT instructions, and interpreter fallback was zero.
The subsequent native game-tick stores incremented normally; the first eight
post-transition values and their level ticks were 1@3, 2@7, 3@9, 4@11, 5@13,
6@15, 7@17, and 8@19.

The authentic-BIOS console route recorded the same reached SW sequence: reset and
loader stage-zero stores in field 6,438, then the first game-tick store in field
6,439 with **level tick 1** and game tick 0→1. Its pre-reset level tick was
5,326, and the full-RAM pre-arm hash at field 5,838 was
`6df9aec68f762d2e024b61d9dc57cb22723bcd90629c83cc1a07875cfea7a9c4`.
The console and native routes are not field- or full-RAM-aligned at the reset,
so the supported conclusion is narrower: both reach the loader-side stage-zero
path, but native delays its first stage-zero game-tick store until level tick
**3**, versus console tick **1**. The differing pre-reset level ticks and
distinct outer routes do not yet assign that two-tick gap to a scheduler,
loader, or update branch.

The next source-grounded discriminator is the `PadVSync` increment of
`g_LevelTicks`. In the admitted PS-X EXE, `0x80053C6C` loads word
`0x800758C8`, `0x80053C88` adds one, and **`0x80053C90` stores it back**;
the other resident store to that word is the reached reset at `0x80013698`.
The decomp's `PadVSync` increments it at callback entry, and native
`FieldScheduler::deliver` attempts one guest callback dispatch per accepted
field; masked guest IRQ state can defer the actual callback. After the
stage-zero store leaves the word at zero, the first native
game-tick store sees three; the console sees one. This is consistent with
three versus one callback increments in that interval, but current captures
do not count the `0x80053C90` stores there or attribute native field-delivery
sites. The bounded read-only next arm should bracket `0x80013B4C` and the
first `0x80033A6C`, count and retain every `0x80053C90` pre/post store in
that bracket, and record native delivery sites (`hostturn`, native frame tail,
or suppressed-render path) with the unreachable sentinel and JIT/fallback
denominators. A console exact-PC arm must start close enough to the known
handoff to fit the observer ABI's four-target, 128-record queue; an arm from
field 5,838 would overflow from ordinary VSync callbacks. If the callback
counts match the observed level-tick values, compare the field origin and
phase before changing timing or gameplay state; if they do not, identify the
additional writer or observer gap first.

### Post-gate exact-binary rerun

After the combined Clang gate (36/36 CTests) and psxport pin advanced to
`b2510d063c81c594bdaa12c441d5b209ea5cca11`, the same bounded, normally
paced state-driven New Game route ran on `spyro_port` SHA-256
`2388b56451f471ea7e58b27b3242e49f2759e89e7423b3d238435367edc59538`.
It used a fresh empty card, no transition cancellation, `--settle 40`, silent
audio, and the confirmed headless Vulkan presentation sink. The unused
`PSXPORT_VK_HEADLESS` environment knob did not select that sink. The admitted
EXE remained SHA-256 `a533d75cab8afaae6107ec35a02a9a5fe979a92c7c955f9cf1ee50f693a1b998`
and the opened CHD was rehashed as SHA-256
`8fe0a6e735ee399a8251f2173cf61c6e20fa565611b934fa3d90788beab9d6cb`.
The driver reached `GS_Playing` at REPL frame 6,381 and exited zero after its
40-field settle at REPL frame 6,421. Runtime completion reported 6,422 fields,
3,425 product steps, 121,957,868 executed JIT instructions, zero faults, and
zero fallback blocks/instructions. Owned UV/driver/game PIDs
`811183/811186/811198` all exited; the retail slot was released.

The observer scanned **121,957,865 JIT instructions** and paired 27 stores,
retaining 19 and explicitly omitting eight routine increments (transitions
3/3 retained, post-transition neighborhood 8/8 retained, routine 8/16
retained). Exact before/after hits were `0x80013698`: 1/1,
`0x800136A0`: 1/1, `0x80013B4C`: 1/1, `0x80033A6C`: 24/24, and unreachable
`0xFFFFFFFC`: **0/121,957,865**. The reset again changed level tick
6,371→0; stage-zero changed stage 13→0 with both ticks zero. The **first**
game-tick store again changed 0→1 with stage zero and level tick **3**,
preserving the 3-versus-1 console mismatch on the final binary. Later field
cadence was not identical to the preceding native run: its second game-tick
store saw level tick 6, versus 7 before, so this rerun confirms the first
handoff point rather than full subsequent phase alignment. The bounded capture
is gitignored at `scratch/oracle-comparison/handoff_native_store.log`; the
preceding framework run is retained as `handoff_native_store.prev.log`.

### Note (2026-09-13)
## Field-delivery bracket: the 3-versus-1 level-tick gap is now attributed

The bounded read-only arm the preceding section asked for now exists on both sides.

**Native.** `PSXPORT_DEBUG=handoff-store` now also observes the resident PadVSync store
`0x80053C90` and records the delivering field's site. The loader stage-zero store `0x80013B4C` opens a
bracket and the first stage-zero game-tick store `0x80033A6C` closes it, so every `0x80053C90`
between them is retained by class. Three independent normally paced state-driven New Game runs
(across three `spyro_port` builds — before and after the shaded-arm change, then on the frozen tree)
reproduced the same bracket shape; the ordinals move by one between runs because the pre-handoff
store count is not identical, and the final landed run is tabulated here:

| ordinal | SW PC | stage | level tick | game tick | delivery site |
|---|---|---|---|---|---|
| 6376 | `0x80013B4C` loader stage zero | 13 -> 0 | 0 -> 0 | 0 -> 0 | none |
| 6377 | `0x80053C90` PadVSync | 0 | 0 -> 1 | 0 | `hostturn` |
| 6378 | `0x80053C90` PadVSync | 0 | 1 -> 2 | 0 | `render-suppressed` |
| 6379 | `0x80053C90` PadVSync | 0 | 2 -> 3 | 0 | `render-suppressed` |
| 6380 | `0x80033A6C` first game tick | 0 | 3 | 0 -> 1 | none |

The capture reported six target PCs, **121,927,308** scanned JIT instructions, 6,449 paired stores,
22 retained, 6,427 routine increments explicitly omitted (`routine 8/6435`), three transition and
three bracket events with zero omissions, `bracket_opened=1 bracket_closed=1`, zero fallback
blocks/instructions, and unreachable `0xFFFFFFFC` **0/121,927,308**. The two earlier runs agreed on
the bracket with 121,161,184 and 122,045,197 scanned JIT instructions and the same three delivery
sites. Raw log: `scratch/oracle-comparison/handoff_bracket_native.log` (preceded by
`handoff_bracket_native.prev.log`); run report `build/bin/spyro_port` SHA-256
`a734c2405b3ff1cc16b5dc101ef1b1d2e631276312e53f290b29b084662c6aa2`.

**Full console.** Three fresh exact-PC arms replayed the recorded 38-command prefix (field 5,838),
walked to field 6,430, armed the pinned observer on `0x80013698`, `0x80013B4C`, `0x80053C90` and
`0x80033A6C`, and drained `observe_read` after every field so the 128-record queue could not
overflow. All three shared pre-arm full-RAM SHA-256
`12aa437b2121f7e1…` and identical post-window RAM/frame/audio SHA-256, and all three ended at field
6,444 with stage 0, level tick 6 and game tick 3:

| field | SW PC | stage | level tick | game tick |
|---|---|---|---|---|
| 6438 | `0x80013698` level reset | 13 | 5326 -> 0 | 0 |
| 6438 | `0x80013B4C` loader stage zero | 13 -> 0 | 0 | 0 |
| 6439 | `0x80053C90` PadVSync | 0 | 0 -> 1 | 0 |
| 6439 | `0x80033A6C` first game tick | 0 | 1 | 0 -> 1 |

The reached arm matched and retained **19 of 4,458,961** scanned instructions with zero drops and
zero pairing errors; unreachable `0xFFFFFFFC` scanned the same denominator and matched zero; the
disabled arm retained nothing. Raw captures are gitignored at
`scratch/oracle-comparison/handoff_bracket_console_{off,bracket,unreachable}.json`; the driver is
`scratch/oracle-comparison/handoff_bracket_console.py`.

## What the bracket establishes

The "native three, console one" gap at the first stage-zero game-tick store is a **field-delivery
count** difference, not a missing writer or an unattributed guest VSync. The console reaches the
first stage-0 stage update one field after the loader's stage-zero store; the native tree delivers
three fields there — one `hostturn` field and two `render-suppressed` fields — before the next
product step's stage update runs. The steady cadence then agrees on both sides (two level ticks per
game tick from the second store on: native 3 -> 6 -> 8 -> 10, console 1 -> 3 -> 5), so the
divergence is local to the loader -> stage-0 transition rather than a general scheduler rate
difference. (The native second game-tick store saw level tick 6 in the final run and 7 in an earlier
one; the first store was level tick 3 in every run.)

This does **not** yet say which cadence is faithful. It bounds the earlier camera-state comparison:
native and console were already two level ticks apart at the first post-handoff game tick, so the
tick-55 camera-state and projection deltas in the sections above are compared at different phases and
cannot be assigned to camera math. No guest state was written in either arm.

## Owner of one of the two extra native fields

Reading `Spyro1FrameDriver::stepFrame` against the bracket output names the delivered fields. A host
turn (`spyro1::hostTurn`, registered with the framework) can deliver a field while the frame update
is still executing, so the loader's stage-zero store is followed by one field inside the same guest
call. The render-suppressed branch of `stepFrame` then delivers `kFieldsPerLogicFrame` (two) more
fields unconditionally, while the native branch on the other side of the same `if`/`else` checks
`fields_.fieldsThisLogicFrame() < kFieldsPerLogicFrame` before delivering its tail. The two branches
therefore disagree about the module's own one-logic-frame field quota: with a host turn, the
suppressed step delivers three fields where the native branch delivers two.

A temporary diagnostic build (patched, measured, then reverted; never committed) delivered only the
remaining quota in the suppressed branch. Its bracket contained **two** `0x80053C90` stores — one
`hostturn` and one `render-suppressed` — and the first game-tick store moved from level tick 3 to
level tick **2**, with every later sampled game tick also shifted by one (2/5/7/9 instead of 3/6/8/10,
same +3 then +2 pattern). That capture reported six target PCs, 122,126,013 scanned JIT instructions,
6,449 paired stores, 21 retained, 6,428 routine increments omitted, and zero fallback instructions.
Its log is gitignored at `scratch/oracle-comparison/handoff_bracket_quota_experiment.log`.

So **one** of the two extra native fields is a quota violation by the suppressed branch, and the
shipping cadence was left unchanged. The remaining field is the phase of the loader store inside its
product step: the console reaches its next stage update one field after the loader store, while the
native frame loop schedules stage updates on product-step boundaries. Neither branch reproduces the
console's single field, so changing `stepFrame` on this evidence alone would replace one unexplained
offset with another.

Next discriminator: establish where retail's loader store sits inside its own two-field logic
iteration — whether `LoadLevelScene` runs in the same field iteration as the following stage-0
update or in the one before it — before touching the field quota or the frame loop.

## The discriminator is answered: retail has no fixed field quota

`scratch/oracle-comparison/handoff_cadence_console.py` re-ran the same prefix (fields 5838 -> 6444)
with a fifth observed word, `g_StateSwitch` (`0x8007579C`), added to the four RAM ranges. One
capture, 13,032,200 instructions scanned, 48 retained, zero drops, zero pairing errors, target
entries 44 `0x80053C90` / 1 `0x80013B4C` / 3 `0x80033A6C`. Per-field trace around the handoff:

| field | PC | stage | load stage | state switch | level tick | game tick |
|---|---|---|---|---|---|---|
| 6401..6437 | `0x80053C90` PadVSync | 13 | 13 | 0 | 5288..5324 | 0 |
| 6438 | `0x80053C90` PadVSync | 13 | 13 | 0 | 5325 | 0 |
| 6438 | `0x80013B4C` loader stage zero | 13 -> 0 | 13 | 0 | 0 | 0 |
| 6439 | `0x80053C90` PadVSync | 0 | 13 | **1** | 0 | 0 |
| 6439 | `0x80033A6C` first game tick | 0 | -1 | 0 | 1 | 0 -> 1 |
| 6440..6444 | PadVSync / game tick | 0 | -1 | 0 | 1..5 | 1..3 |

So the loader store and the following stage-0 update are **adjacent field iterations** with exactly
one `0x80053C90` between them, and the one field that separates them carries
**`g_StateSwitch == 1`**. `g_LoadStage` is still 13 at that field's PadVSync and is -1 by the game
tick, so the loader update's own case-13 tail (`g_LoadStage = -1`, `src/loaders.c`), the state-switch
consumption, and the `GS_Playing` update all run inside that single field.

`g_StateSwitch` is the main loop's draw gate, and the draw is where retail's two-field rate comes
from. `src/main.c:21` is a free-running `while(1)` that ends with `if (!g_StateSwitch)
GamestateDraw();`, and `GamestateDraw` (`func_8001A050`, `src/gamestates/draw.c:857`) contains:

```c
  D_80075950.pre = VSync(-1);
  while (D_80075950.pre - D_80075950.post < 2) {
    VSync(0);
    D_80075950.pre = VSync(-1);
  }
  D_80075950.post = VSync(-1);
```

That loop is retail's two-field logic iteration, and it only runs when `g_StateSwitch == 0`. While a
state switch is pending the main loop spins without the wait, so how many fields an iteration elapses
is whatever the interval between two VSync interrupts happens to contain — an interrupt phase, not a
rule. `g_DeltaTime` is the compensation: `g_DeltaTime = g_UnprocessedFrames` clamped to `[2, 4]`, so
the guest advances at least two ticks of time per iteration even when no field elapsed
(`src/main.c:21`; `g_UnprocessedFrames` is incremented by `PadVSync`, `src/gamepad.c:508`).

The console's "one field" is therefore the phase of one VSync interrupt inside the loader update,
and the native tree cannot be made to reproduce it by changing a quota: the shipping cadence already
delivers exactly `kFieldsPerLogicFrame` fields per product step, and the temporary one-field
suppressed build landed on level tick 2 rather than 1 for the same reason (its extra field is the
host-turn delivery on the other side of the loader store; see the section above).

**No frame-loop or quota change is warranted, and none was shipped.** The native frame loop's fixed
two-field step is a deterministic model of the *draw-wait* rate, which is the rate the guest is in
for all of gameplay. Its known cost is a permanent offset in the absolute `g_LevelTicks` counter
from the moment of a load (native 2 or 3 where the console reads 1, constant thereafter), which is
the offset the camera checkpoints above were compared across; timers read from `g_LevelTicks` fire
one or two fields early as a result. Modelling retail's state-switch phase instead would mean
choosing how many fields a suppressed iteration consumes, and retail's own answer is "however many
interrupts land", so any such change is a pacing-model decision that needs its own evidence, not a
bracket-count fix.

### Retail's frame bookkeeping is already the native owner of those globals

The frame driver's `FrameState` addresses are the retail globals, not a private host page:
`game/core/guest_gp.h` sets `kGp = 0x80075264`, so `kGp + 0x468` is `g_DeltaTime` (`0x800756CC`),
`kGp + 0x4FC` is `g_UnprocessedFrames` (`0x80075760`), and `kGp + 0x538` is `g_StateSwitch`
(`0x8007579C`). `stepFrame` writes `clamp(elapsedFields(), 2, 4)` into `g_DeltaTime` and zeroes
`g_UnprocessedFrames` once per logic frame — main.c's three lines, in the host that owns the field
clock — and `renderSuppressed()` reads the guest's own `g_StateSwitch`.

A read-only REPL probe (`scratch/oracle-comparison/pad_edge_probe.py`, log
`scratch/oracle-comparison/pad_edge_probe.log`) confirmed the coupling live at Artisans: Cross
pressed for six fields produced `g_Pad.m_Down = 0x00000040` for exactly one field, released for
eight fields produced `g_Pad.m_Released = 0x00000040` for exactly one field, `g_UnprocessedFrames`
read 1 and 2 on alternating fields, and `g_DeltaTime` stayed 2 throughout. `g_Pad.m_Down` /
`m_Released` are `|`-accumulated rather than overwritten whenever `g_UnprocessedFrames != 0`
(`src/gamepad.c:444`), so this is a discriminator that would have shown a stuck edge if the
per-iteration consumption were missing; it did not.

### Note (2026-09-13)
Next discriminator answered: retail has no fixed field quota. Console loader store and first stage-zero game tick are one field apart with g_StateSwitch==1 across that field; func_8001A050 (draw.c:857) waits two fields since the previous draw and main.c:21 skips that draw while a state switch is pending, so a suppressed iteration's field count is interrupt phase. Provisional one-field suppressed build landed on level tick 2, not 1. The native fixed-two-field step is the deterministic time base; changing it changes simulation speed, not just phase. Residual level-tick offset (native 2-3 vs console 1) is parked as a pacing-model convention, not fixed. No code change shipped.
