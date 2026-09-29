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

## Owned (round 1)

| guest | name | module | seen | sampled | match |
|---|---|---|---|---|---|
| `0x80049F3C` | `update_flame_tail_lock` | `native_player_animation` | 440 | 22 | 22 |
| `0x800342F8` | `camera_rotation_from_sphere` | `native_camera` | 54 | 16 | 16 |
| `0x80056C84` | `positional_stereo_volume` | `native_sound_position` | 127 | 17 | 17 |

`positional_stereo_volume`'s mono branch is never taken on the route, because mono audio is off. It
was checked against the bytes at `0x80056D94..0x80056DB4` instead: v0 is the `srl` of the
sign-extended sum, v1 the clamped left channel, and both stores are made.

## Measured but unexercised

| guest | decomp | why it is not owned |
|---|---|---|
| `0x80038D54` | `moby_helpers.c` point-to-plane distance | 0 calls on the gameplay route in three runs, so there is nothing to compare |

A function here needs a route that calls it before it can be owned. It is not an implementation
problem.
