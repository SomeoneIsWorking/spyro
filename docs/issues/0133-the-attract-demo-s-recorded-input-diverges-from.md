---
id: 133
title: The attract demo's recorded input diverges from the console at g_GameTick 556: player position offsets (+9,+51,-2) growing to (+59,+405,0), with the camera following
status: investigating
symptom: oracle compare on the no-input attract route holds 553 per-iteration comparisons then DIVERGE on player.position at tick 556; the level entry itself MATCHES every decisive range
tags: oracle,attract-demo,physics,divergence,level-entry
created: 2026-09-26
updated: 2026-09-26
---

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

## Open

The cause is not yet established. The next measurement is a per-iteration census of Spyro's physics
block and the camera to establish which moves first. No fix is claimed and no tolerance is proposed:
this is a decisive declared range, so it either matches or it is a defect.

## Route result

The route exits 1. It is not a clean pass, and it is not tuned to be one: the level entry matches, 553
iterations match, and one real divergence 555 ticks in is reported rather than hidden. The second level
entry (the demo's return to the title and the next demo) is still uncompared, because the route stops
at the first decisive divergence.
