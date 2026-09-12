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

Resolve native observation before inferring gameplay parity. Arm an observer
through an immutable startup configuration boundary before REPL command timing,
or build an independent Lightrec guest-PC/return trace with synchronized
architectural state; the dormant `Core::pcObserver` is not wired into the
shipping executor. Require reached and unreachable controls and identical
native RAM, frame, and audio hashes from identical pre-arm state. Then compare
a truly matched pre-update New Game handoff, before the first stage-0 game
tick. Do not change camera math or scheduler timing to fit unmatched endpoints.
