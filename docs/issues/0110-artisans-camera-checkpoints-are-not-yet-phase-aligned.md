---
id: 110
title: Artisans native and full-console camera checkpoints are not yet phase aligned
status: investigating
symptom: At the same Artisans level tick and player position, native and console game ticks and camera states differ before movement input
tags: oracle,camera,gameplay,timing,input
state_items: S011
created: 2026-09-12
updated: 2026-09-12
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

This makes the **level-scene reset, first stage-9-to-0 store, then first stage-0 game-tick store**
the narrow handoff sequence to compare at exact guest PCs in native and console. Record each hit
count and the pre/post `g_Gamestate`, `g_LevelTicks`, `g_GameTick`, camera rotation Y,
spherical-preset pointer, camera state/target, and delivered-field count through the first game
tick. The positive control is reaching the reset pair, transition store, and tick store in order;
an unreachable-PC sentinel must report zero against the same scanned denominator. Stop at that
first tick rather than running to the later tick-55 camera-state restart. The source says
`LoadLevelScene` resets
both ticks, `PadVSync` advances level ticks by physical field, and the stage-0 arm advances game
tick once per update. The earlier native first-game-tick level ticks 5/7 versus console 1 and the
147/150 game-tick values at level tick 300 are consistent with different handoff timing, but do
not prove which transition predicate or field boundary first differs. No new retail process was
launched for this static discriminator.

### Exact-PC observation boundary and bounded console plan

The pinned console observer can take pre-instruction snapshots at exact guest PCs,
including selected RAM ranges, but its ABI accepts only **4 PC targets**, **8 RAM
ranges**, and **128 queued records** per configuration. The proposed read-only
comparison therefore needs two predetermined console arms from the same saved
pre-handoff state, each stopping at the first stage-0 game-tick store:

- Reset arm: `0x80013698` (before level-tick zero store), `0x8001369C`
  (after it), `0x800136A0` (before game-tick zero store), and `0x800136A4`
  (after it).
- Handoff arm: `0x8002E070`/`0x8002E074` (before/after the stage-9-to-0
  store), then `0x80033A6C`/`0x80033A70` (before/after the first stage-0
  game-tick store).

Set `follow_return=false`, drain between fields to avoid the 128-record cap,
and report per-target entries, scanned, retained, dropped, and the reached
field denominator, including **zero** if a target was never reached. Sample
`g_Gamestate`, `g_LevelTicks`, `g_GameTick`, camera rotation Y, spherical
preset pointer, camera state, target state, and delivered-field counter as the
eight bounded RAM ranges. A third identically bounded sentinel arm targets
unreachable `0xFFFFFFFC`; it must report zero hits with a nonzero scanned
denominator. These are planned controls, not observed runtime results. No
console arm has been run for this handoff comparison.

The matching **native exact-PC arm cannot yet be implemented title-locally**
without changing execution semantics. `Core::pcObserver` exists, but the
production Lightrec path never calls `pc_observer_at`; it executes a translated
segment and synchronizes `Core::pc` only on return. `Core::storeWatchCb` receives
address/value/width, not the translated instruction PC, so pairing that callback
with `Core::pc` would falsely attribute an interior store to the segment exit.
The title's StageUpdate call observer similarly sees an outer call boundary,
not the nested reset or store instruction. A shared Lightrec debug seam would
have to report selected guest instruction PCs and pre/post state at the
translated execution boundary while leaving unarmed execution unchanged. Its
synthetic qualification needs a reached translated-store positive, an
unreachable-PC zero-hit negative with scanned denominator, unchanged guest
output with observation on/off, and zero interpreter substitution. Until such
a seam is reviewed and implemented in `psxport`, the console-only arms cannot
yield a paired lifecycle verdict.
