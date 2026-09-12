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
toward the New Game handoff, before the measured movement interval.
Neither an end-of-field image diff nor a camera-position delta can be attributed to
camera math until the first earlier lifecycle/tick divergence is found.

## Next discriminator

Observe the title-to-Artisans handoff on both arms at `g_Gamestate`, `g_LevelTicks`,
`g_GameTick`, camera state, and a completed `CameraUpdate` return. Locate the **first**
transition where game tick or camera state differs; then classify its owner as input
delivery, title frame scheduling, or guest camera execution using an equivalent native
read-only guest-PC checkpoint. A camera correction is justified only after that first
divergence and the input phase are identified. The shared native PC observer seam must be
implemented and qualified before treating a matching native PC sample as evidence.
