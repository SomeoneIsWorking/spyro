---
id: 0170
title: Two of the four scene routes reach their scene; the two portal routes stall on a ledge the
  recorded route crosses and the walker cannot climb
status: open
symptom: tools/route_scenes.py drives four scenes. save-fairy and death-respawn reach theirs and prove
  it from guest words. boss-level and flight-level walk most of the recorded route and then stall a
  few thousand units short of the portal, with the route data -- not the walker and not the guest --
  naming the failure.
tags: routes,repl,card,fairy
created: 2026-10-01
updated: 2026-10-01
---

## What passes, with the words that prove it

**save-fairy** reaches the in-game save. The proof is the guest's own page machine, sampled at the
card machine's own resolution: `g_Gamestate` 11 (GS_Fairy), `g_FairyCutscene.m_MenuDialoguePage`
walking **0 -> 2 -> 7**, with the card-stage word (the same `+0x08` word, see below) reaching 4.
Page 2 is the only jtbl_80010E08 entry whose handler reaches `SaveCreate` (0x800321F4) and
`MemCardWriteFile` (0x80032230); page 7 is the page the guest captions **"GAME SAVED"** (the string
at 0x80010CC4, laid out for page 7 by `game/render/fairy_menu_recipe.cpp` `case 7`). The card image
the run left behind carries the `BASCUS-94228SPYRO` file, so the write is corroborated outside the
guest too.

**The route's constants were inverted before this.** The previous revision read page 7 as "the card
refusing" and page 4 as the write, and refused the run that had in fact saved. The page meanings are
in the guest's own caption table, and they are the opposite of that: 3/4/5/6 are the failures ("NO
MEMORY CARD", "NO SAVE FILE", "SAVE ERROR", "SAVE FAILED") and 7 is the success. A refusal that
asserts a page without reading the caption it maps to will call a successful save a failure.

**`+0x08` is two things, and reading it as one is how the previous run reported a selection of 4.**
The guest proves the double use: the page-0 handler increments it and wraps at 3 as
`m_MenuSelectedOption` (0x80031E74 / 0x80031E80), and the page-2 handler loads it as the card
machine's base and stores 1..4 into it as the stage (0x800320C8 and 0x800320F8 / 0x80032168 /
0x80032240). Measured on one run: option 0 when CROSS was pressed, 4 twenty fields later, save
successful. So the option may only be read while the page is 0, and a route that presses DOWN to
"correct" a selection of 4 presses a button the guest's current page does not read.

**The card machine finishes inside one 60-field step.** CROSS at field 103, page 2 at 103, stage 4
by field 129, page 7 at 129. A route that sampled the page only at the end of a 60-field step walked
straight over page 2 and could not prove the write at all, which is why it read as "never reached
the write page". `FAIRY_WATCH_FIELDS` is now 2 and the whole sequence is recorded as the proof.

**death-respawn** reaches the guest's own drowning. `g_Gamestate` 0 -> **4 (GS_Respawn)** while
walking at the flight portal, body state **0x1D**, health 0xAA at the moment of the death, lowest Z
5674 against a death plane of 0x400, and `g_SpyroLifeCount` **4 -> 3**, back at GS_Playing.

## The card is an input, and the fairy scene wrote to the one every run shares

`save-fairy` writes a memory card. On the shared `scratch/saves/card.mcr` that left
`BASCUS-94228SPYRO` in slot 1, and the **next** run booted into the picker's "this slot has a save"
screen -- `TSM_Loading` state 2 -- which `tools/title_prompts.py` deliberately never answers because
answering it destroys the operator's card. That run refused with "the save picker never committed a
slot within 12000 frames", which reads like a driver bug and is the card the previous run left.

So a `--scene` run now opens its own card (`tools/drive.py`'s `SCENE_CARD`), deleted before launch
by the framework's `blank_card_environment`, which is issue 0169's rule applied to the one route
that writes.

**A blank card then exposed a second defect: a mistimed press was permanent.** Every press the
front end and the save picker take is gated on the screen's own `m_SubTick >= 8`
(`titlescreen.c:625`, `:930`, `:944`), so a four-field edge that lands before the gate opens changes
nothing -- and the driver's one-press-per-target rule then never pressed again. Measured on a blank
card: one Cross at field 1760 into `TS_SubState_CreateSaveConfirm` left the guest asking the same
question for the rest of a 12000-field budget; making the press repeatable carried it through to
GS_Playing at field 6380. The one picker screen whose repeat would be *wrong* -- `TSM_Loading`
state 2, whose Cross cancels back to the slot picker -- still has no prompt at all, so no repeat can
reach it. `title_prompts.py`'s selftest pins both directions.

## What does not pass, and why

**boss-level** and **flight-level** both walk most of the way and stall. Nothing here writes a guest
word: the routes press pad edges and read `g_LevelId`.

*boss-level* follows `tools/routes/spyro1_level10.json`'s `portal-14` to **waypoint 66 of 74** and then
stalls at 258 view units of waypoint 67. Measured on the guest's own position words: Spyro is at
`(128933, 80464, z 5090)` oscillating inside a 100-unit patch while waypoint 67 is recorded at
`(129225, 81975, z 7042)` -- a terrace **1952 units above** the ground he is standing on. The route
data crosses a ledge there; the walker is not failing, it is refusing to walk into a wall. Marking the
leg as a climb (hop on every decision, the flag the follower already honours) does not help, because
Spyro's hop gains a few hundred units and the terrace is two thousand.

*flight-level* has **no recorded route at all**: `Route.find` returns None for `portal-15`, so the
scene falls back to the straight-line seeker, which stalls at 5570 view units from the portal to
level 15 at `(52656, 43245, 8865)`. The same portal is where `death-respawn` drowns, which says the
direct line crosses the west pond.

### The mesh-derived route does not fix it, and the reason is a real defect in the planner

`tools/routes/*.json` is recorded from the level's render mesh with an edge-clearance margin, and
regenerating `portal-14` from the same mesh with a proper A* walk (`plan.py`, z-steps bounded at 450,
"edgy" cells forbidden) produced 68 waypoints of which the walker reached **68 of 68** -- the mesh
route's own last leg was the only thing it could not do. The remaining gap to the portal is then about
2900 world units and 800 units of climb, and appending five waypoints sampled from the mesh's
*lowest* floor in a 300-unit radius reached **70 of 73** before stalling at waypoint 71 on a
same-height step, i.e. on an obstruction rather than a climb.

The planner's own clearance is not doing what it claims. The dilation that was supposed to widen the
forbidden "edgy" set by one cell was written as

```python
sh = np.zeros_like(edgy)
sh[max(0,dy):H+min(0,dy), max(0,dx):W+min(0,dx)] = edgy[:, ...] if False else False
```

-- every shift assigns the literal `False`, so `forb` is all-zero and the "forbidden margin" is
nothing. A path planned with no margin is a path along cell edges, which is exactly the class of
route that walks into geometry the walker cannot pass. Replacing it with a real 3x3 binary dilation
changes the result measurably: reachable cells 157470 -> 117509 and `P13` moves from "reachable at
radius 3000" to unreachable, i.e. the margin was doing nothing before.

### What would unblock both

1. A walkable-surface source that matches the guest's **collision**, not its render mesh. The
   level-10 collision triangles are already decoded elsewhere in the tool tree; the route mesh is the
   render one, and the difference between them is what waypoint 67 is.
2. Failing that, the route's tail needs recording from what Spyro actually walks, the way
   `scratch/explore/session.py`'s trace does, rather than sampled from the mesh's lowest floor: the
   70-of-73 run above stalled on a same-height step, which is a mesh sample that is not where
   Spyro's feet are.
3. `flight-level` additionally needs a `portal-15` route around the west pond; `best_near` reaches no
   closer than 6000 units to that portal under any climb bound tried (450, and the "unreachable
   within 3000" default), so the approach is not on the walkable graph the mesh gives.

## What is NOT the answer

`external/spyro-1/src/cheats.c` carries a guest **level warp** cheat (Cheat 1:
R1, R2, L1, L2, R1, L1, R2, L2, then a level digit) that would load any level through the guest's
own loader with pad input alone, and `func_8002EB2C` dispatches it whenever `g_LevelCheatActive` is
set. `g_LevelCheatActive` is BSS and **nothing in the image ever sets it** -- the only write is the
clear at cheats.c:246 -- so the warp is dead code in retail. Enabling it would fabricate guest state
this build does not have, in order to reach a scene, which is the one thing a scene route exists not
to do. It is named here so it is not re-derived as an idea.
