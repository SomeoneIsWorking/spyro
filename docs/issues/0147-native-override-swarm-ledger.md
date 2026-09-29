---
id: 0147
title: Native override swarm ledger — which Spyro 1 functions are owned, which are unexercised
status: open
symptom: Spyro 1 still runs most guest code through Lightrec. Owning leaf functions natively goes
  through bounded worker jobs, one function each. Each job is accepted only by
  tools/native_override_gate.py, which ends with psxport's override differential on the gameplay route.
tags: native,override,swarm,differential
created: 2026-09-29
updated: 2026-09-29
---

## The gate

`tools/native_override_gate.py <name>` runs inside the job worktree. Its docstring has the full
contract. It fails an unchanged tree and any change outside `game/`, `titles/`, `tests/` or
`CMakeLists.txt`. It builds with Clang and requires format, clang-tidy and the source policy to pass,
plus CTest without the `slow` console-oracle label. Last, it runs a headless `drive.py gameplay` with
`PSXPORT_OVERRIDE_DIFF=<name>`. **Zero sampled calls is a failure**, so a function the route never
calls cannot land.

Proven both ways on the same route: `update_flame_tail_lock` matched 22 of 22 sampled calls, and a
planted `+2` on its counter path mismatched 22 of 22 (first difference: v0 1 vs 2).

## Owned (rounds 1, 2 and 4)

| guest | name | module | seen | sampled | match |
|---|---|---|---|---|---|
| `0x80049F3C` | `update_flame_tail_lock` | `native_player_animation` | 440 | 22 | 22 |
| `0x800342F8` | `camera_rotation_from_sphere` | `native_camera` | 54 | 16 | 16 |
| `0x80056C84` | `positional_stereo_volume` | `native_sound_position` | 127 | 17 | 17 |
| `0x8003CB24` | `advance_body_animation` | `native_player_animation` | 1116 | 33 | 33 |
| `0x80035F58` | `camera_shoulder_rotation_input` | `native_camera` | 220 | 19 | 19 |
| `0x800562A4` | `stop_moby_sounds` | `native_sound_position` | 36 | 16 | 16 |
| `0x8003CBB8` | `advance_body_animation_with_transitions` | `native_player_animation` | 91 | 17 | 17 |
| `0x80037F90` | `tick_moby_timer` | `native_moby_helpers` | 817 | 28 | 28 |
| `0x8003A720` | `reset_moby_defaults` | `native_moby_helpers` | 21 | 16 | 16 |
| `0x80049880` | `smooth_head_look` | `native_player_animation` | 440 | 22 | 22 |

`positional_stereo_volume`'s mono branch is never taken on the route, because mono audio is off. It
was checked against the bytes at `0x80056D94..0x80056DB4` instead: v0 is the `srl` of the
sign-extended sum, v1 the clamped left channel, and both stores are made.

`camera_shoulder_rotation_input`'s L2 and R2 branches are not taken on the route, which holds only
RIGHT. They were checked against the bytes at `0x80035F90..0x80035FA8`: R2 stores -0x400, L2 stores
0x400, and with neither held v0 exits as 0x400 and nothing is stored.

## Measured but unexercised

| guest | decomp | why it is not owned |
|---|---|---|
| `0x80038D54` | `moby_helpers.c` point-to-plane distance | 0 calls on the gameplay route in three runs, so there is nothing to compare |
| `0x800495D8` | `pete.c` head-animation stepper | 0 calls on the route in 3 gate runs (the body stepper `0x8003CB24` is called 1,116 times on the same route) |
| `0x8003D92C` | `pete.c` approach target speed | 0 calls on the route in 3 gate runs |

A function here needs a route that calls it before it can be owned. It is not an implementation
problem. A worker job on such a function can never pass the gate, and round 3 spent two
40-minute worker timeouts learning that. Candidates are now screened before dispatch with a reach
census: a scratch-only build installs a pass-through override (it calls the original) at every
candidate, and the same gameplay route runs with the differential armed, so each one's "seen" count is
its reach. The census is only trusted when it shows both answers. Of the ten remaining pete.c,
moby_helpers.c, special_surfaces.c, init.c and draw.c leaves, it measured 4 reached (`0x80037F90` 817,
`0x80049880` 440, `0x8003CBB8` 91, `0x8003A720` 21) and 6 at 0: `0x80049DFC`, `0x80057380`,
`0x8002C85C`, `0x80018880`, `0x8003B7C0`, `0x800381BC`. A first census run read 0 for all ten
because the probe build had failed and the run used the previous binary. That is why a census must
show at least one nonzero.

## Route corpus (2026-09-29)

One gameplay route was too narrow. It both chose what to own and gated what was owned, so any
function it never called was neither a candidate nor gated. `tools/reach_corpus.py` now runs every
route it names with two instruments armed: psxport's function-reach recorder (`PSXPORT_REACH_REPORT`,
which records every pc the dynarec dispatches, keyed by code image) and the override differential for
every registered override. The denominator is the 547 statically called functions in `SCUS_942.28`
(its `jal` targets). Functions reached only through pointers are missed by that denominator, not by
the recorder.

| route | reached / 547 |
|---|---|
| artisans-walk | 302 |
| pause-menu | 296 |
| gem-seek | 315 |
| portal-level | 323 |
| skip-transitions | 321 |
| attract-demo | 308 (the last flush before the timeout kill; crashed before issue 0148's fix) |
| **union of all six** | **352 (64.4%)** |

WAD overlays: 2 images and up to 398 distinct entry pcs per gameplay route, and 5 images with 2,360 on the attract demo. There is no function list for them
yet, so they are counted without a denominator.

Differential across the corpus:
- 29 overrides were gated, with 0 mismatches.
- `dllink` (0x800168DC) was reached on no route.
- The three CD overrides (`cd_loader`, `cd_retry_step`, `cd_stream_read`) cannot be shadowed:
  arming any one of them alone aborts boot at libetc VSync 0x8005DBC4, which the product keeps fatal,
  because their original bodies poll it. The tool excludes them by name and prints why.

MISSING routes, which no headless route reaches yet: a flight level, a boss, death and continue, and
the save screen after a level.
