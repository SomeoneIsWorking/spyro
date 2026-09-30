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

## Round b1r (2026-09-30): 32 landed, 5 held back by the corpus

Swarm b1r's 20 accepted jobs added 37 overrides. Every one was re-gated on EVERY call on its own
route after 4286922 fixed the gate (from 3c071ae until then the gate crashed before its differential
step, so b1r had been accepted without one). 36 of 37 matched every call; `mvmva_camera_matrix` was
unsampled there only because its callers were shadowed at the same time, and the corpus sampled it
301/301.

The six-route corpus then caught five that pass on their own route and mismatch on others, all
first seen on attract-demo. They are NOT landed; their code is kept for a fix round:

| guest | name | mismatching / sampled | routes |
|---|---|---|---|
| `0x800499C0` | `update_flame_burst` | 15 / 236 | attract-demo |
| `0x80054988` | `update_hud_collectables` | 20 / 164 | attract-demo |
| `0x8002A6FC` | `propagate_environment_light` | 29 / 165 | attract-demo |
| `0x8004E3C8` | `moby_collision_walk` | 11 / 271 | attract-demo, skip-transitions |
| `0x80055A78` | `assign_active_sound_slot` | 7 / 137 | attract-demo, portal-level, skip-transitions |

**A single-route gate is not enough to accept an override**: each of these passed every call on the
route its job named. The corpus over all six routes is the acceptance bar for a landing batch.

With the 32 landed, the corpus reports 65 owned overrides and 0 mismatches (union 353/547 jal targets).

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

## Round b2 (2026-09-30): 5 landed, 1 held back by the corpus

Six further jobs (camera-artisans-walk-1, 42cc4-gem-seek-0, initialization-artisans-walk-0/1, psyq-libspu-8005cb7c,
psyq-libspu-8005cc58) were re-gated across the six-route corpus. Five match every sampled call:

| guest | name | sampled / match |
|---|---|---|
| `0x80033F08` | `camera_spherical_follow` | 183 / 183 |
| `0x80052568` | `release_moby` | 42 / 42 |
| `0x8001277C` | `initialise_artisans_level` | 13 / 13 |
| `0x8005CB7C` | `spu_set_transfer_mode` | 6 / 6 |
| `0x8005CC58` | `spu_set_common_attr` | 96 / 96 |

`reset_game_progress_for_new_game` (`0x80012604`) is NOT landed: no route calls the installed override
(0 sampled on all six; the only entry into the address is the scoped guest call
`initialise_artisans_level` makes), so there is nothing to compare. It needs a new-game route.

## The four scenes the corpus was missing (2026-09-30)

The four MISSING routes named above are now routes. They are in `tools/route_scenes.py` and the
corpus runs them like any other route, so what they add is a number over the same 547-function
denominator and, more to the point, they stop deciding ownership by accident.

**WHY THE SCENES ARE SEPARATE FROM THE ROUTE TABLE.** A route is a command; a scene is a state
with a proof. `tools/route_scenes.py` owns the four states, the guest words each asserts on, and the
input plan that reaches them; `tools/repl_walk.py` owns the press/run/release stepper the fixed
routes already used (it moved out of `drive.py` so `route_scenes` does not have to import the
driver that imports it); `tools/drive.py` gained `--scene` and `--scene-proof`; and
`tools/reach_corpus.py` judges the result. A scene route writes a proof FILE and only after it
reached its target, so the corpus can require one and fail by name — the absence of a file is the
failure signal, and a proof left by an earlier corpus run is refused rather than counted (that
refusal is in the corpus selftest, on four inputs: absent, present, another scene's, unreadable).

**NO RAM WRITES.** Every transition is `press`/`tap` plus `run`, and every decision is made from a
word the product wrote. There is no level-id write, no position write, no health write, and no
`mem_w` anywhere in the four scenes; the pad bit names are the port REPL's own
(`external/psxport/runtime/psx/repl.cpp` `repl_btn`) and the bit values are the PSX digital pad's.

### What each scene reads, and where the address came from

| scene | what it proves | guest words | derivation |
|---|---|---|---|
| `flight-level` | a flight level is running | `g_IsFlightLevel` `0x80075690` != 0, `g_LevelId` `0x8007596C` == 15, `g_LevelIndex` `0x80075964` == 5 | `include/common.h` / `loaders.c:1398`; `g_IsFlightLevel` is the flag `draw.c:534` and `update.c:177` branch on to drop the walk HUD, so it is what makes a flight level a flight level rather than a level with a new id |
| `boss-level` | a boss level loaded | `g_LevelId` == 14, `g_LevelIndex` == 4, `g_Gamestate` == 0 | `g_LevelId`/`g_Homeworld`/`g_LevelIndex` from `loaders.c:1398-1400`; 14 is Toasty (`strings.c` `g_LevelNames` index 4) |
| `death-respawn` | the guest killed Spyro and put him back | `g_Gamestate` `0x800757D8` == 4, `g_SpyroLifeCount` `0x8007582C` decremented | `func_8004A200` `0x8004A4F4 slti $v0,$v0,0x400` / `0x8004A4F8 bnez $v0,.L8004A4D8` on `g_Spyro + 0x8` (m_Position.z) → `0x8004A4D8 jal func_8002C85C`, which `init.c:206` decrements `g_SpyroLifeCount` and sets `GS_Respawn` or `GS_GameOver` |
| `save-fairy` | the guest issued the memory-card write | `g_Gamestate` == 11, `g_FairyCutscene.m_MenuDialoguePage` `0x80078D0C` 0 → 4 → 3 | `func_800314B4` dispatches on `m_MenuDialoguePage` (`0x80031E08 lw $v1,0x8($s1)`, `$s1 = g_FairyCutscene + 4`) through `jtbl_80010E08`; page 4 is the ONLY predecessor of `0x800321F4 jal SaveCreate` and `0x80032230 jal MemCardWriteFile` |

Two of those derivations are corrections of a wrong first reading, and both are worth stating
because the wrong reading is the plausible one:

- **The in-game save is the FAIRY, not the BALLOONIST.** `MemCardWriteFile` has exactly two callers
  in the whole image (`grep -rn MemCardWriteFile external/spyro-1/asm`): the title screen, and
  `func_800314B4`, the `GS_Fairy` update. `include/overlays/balloonist.inc.h` — the balloonist's
  entire eight-state ride across the homeworld, `D_800777E8` — contains no card call at all. The
  balloonist is a taxi.
- **The fairy's menu takes SQUARE, not CROSS.** Page 0 is `andi $v0,$v1,0x4000` at `0x80031E40`,
  which on the PSX digital pad is SQUARE (`repl_btn` maps `square` to `0x8000` and the guest's
  immediate is the shifted form of the same layout: UP `0x10`, DOWN `0x40`, SQUARE `0x8000`,
  TRIANGLE `0x1000`). Reading `0x4000` as "the 0x4000 bit" rather than as the PSX face layout
  would press the wrong button and then report a menu that did not respond.

### The two Moby classes the scenes walk to

Neither is named in `moby.h`; both are read off the compare that selects them in the level-10
overlay's own dispatcher, `external/spyro-1/asm/nonmatchings/overlays/level_10/func_level_10_8007D9C8.s`:

| class | what it is | instruction | trigger |
|---|---|---|---|
| 110 (`0x6E`) | the fairy — the save | `0x8007DB54 addiu $v0,$zero,0x6E` ; `0x8007DB58 beq $v1,$v0` → the block ending `0x80080A1C jal InitFairyCutscene` | `0x80080788 slti $v0,$v0,0x400` after `OctDistance` against `g_Spyro`, plus a Z band and `g_Spyro.m_health > 0` (`0x8007D7E8 blez`) |
| 187 (`0xBB`) | the balloonist — the ride | `0x8007DBAC addiu $v0,$zero,0xBB` ; `0x8007DBB0 beq $v1,$v0` | `0x80082850 slti $v0,$v0,0x780` AND a head-facing test at `0x80082888` — so it needs Spyro to be LOOKING at it, which is why the walk uses `arrived=0` and a predicate rather than a radius |

`g_Spyro + 0x80` is `m_health`, corroborated twice from bytes rather than assumed: `func_8004A4B0`
compares it against `0x65` and `0x7D` to decide death, and the fairy's own trigger refuses to open
unless it is positive (`0x8007D7E8 blez $v0`). The death route reads it as corroboration and never
as the death condition; the death condition is the Z plane at `0x400`.

### The scenes refuse, and the refusals are the selftest

`route_scenes_selftest` is registered in CTest and **every one of its cases is a refusal**, because
the failure these routes most need to not have is reporting a scene they never reached:

- a homeworld with no portal to level 15 → names the level ids it DOES offer;
- the same for level 14;
- a level with no Moby of class 110 → names the class and the instruction that selects it;
- a bounded island where 7,200 fields of walking in all eight compass sectors never moved the guest
  out of `GS_Playing` → names the sectors walked, the fields spent, the lowest Z reached and the
  death plane, and says the route needs a level with a fall in it rather than more walking;
- an unknown scene name → names the four that exist.

`reach_corpus_selftest` gained the proof reader's four answers and a check that every scene has a
route and a place to write its proof.

### MEASURED 2026-09-30 — THE CORPUS RUN, THE 547 DENOMINATOR, AND WHAT ACTUALLY GREW

The full ten-route corpus ran on `build/bin/spyro_port` (716/716 targets, clang). **Both unions,
read from the same reports by the same summarizer**, the "before" by moving the four scene reports
aside so only the six original routes are counted:

| | union over 547 |
|---|---|
| six original routes (**before**) | **352/547 (64.4%)** |
| ten routes, four scenes included (**after**) | **367/547 (67.1%)** |
| growth | **+15 functions** |

**AND THE +15 IS THE FINDING, NOT A SUCCESS.** Not one of the four scenes reached its target, so the
growth cannot be credited to reaching a flight level, a boss, a death, or a save. It is what the
four refusing routes happened to execute on the way to their refusals — chiefly `death-respawn`,
which swept eight compass sectors and reached **332/547, the most of any route in the corpus**, and
which is exactly the route that had no level to fall off. **A wider union is not a wider set of
states**, and quoting 67.1% as progress on the four states would be reading a number about a
different subject.

Per-route totals over the same denominator, and what each route alone contributes:

| route | functions | unique to this run | state actually reached |
|---|---|---|---|
| attract-demo | 308/547 | **+21** (of six) / +18 (of ten) | boot, logos, title, attract flyby, self-playing demo |
| artisans-walk | 302/547 | +0 | the hub, walking |
| pause-menu | 296/547 | +2 | pause menu open |
| gem-seek | 315/547 | +0 | walked to and collected a gem |
| portal-level | 323/547 | +0 | **left level 10** (see below) |
| skip-transitions | 321/547 | +0 | level flyby and tally |
| flight-level | 321/547 | **NO PROOF** | none |
| boss-level | 318/547 | **NO PROOF** | none |
| death-respawn | **332/547** | **NO PROOF** | none |
| save-fairy | 325/547 | **NO PROOF** | none |

`owned overrides: 33`. The summarizer reports `NO PROOF FILE: the scene did not reach its target`
for each scene rather than a function count, which is the behaviour it was built for: a route that
did not arrive contributes reach data but is never credited with having reached its state.

**`portal-level` IS THE ONE ROUTE THAT GOT INTO A LEVEL, AND IT SAYS SO.** Its own output:

    seek: portal to level 14 at (134462, 83968, 5347)
    seek: left level 10 after 576 field(s), 556 from the nearest portal
    gamestates ... playing=126, level_transition=3, ... 3 image(s)

`g_LevelId` was 10 (Artisans) and stopped being 10, `level_transition` was sampled 3 times, and the
run touched **3 images** — an overlay loaded. **So a level entry IS reachable, and Toasty's portal
is the NEAREST one from the spawn.** That refutes the "the walk cannot climb to any portal" reading
of the four refusals and makes the real question much narrower: **`boss-level` stalls at 2,228 from
the very portal `portal-level` reaches at 556, and it stalls at exactly 2,228 on two independent
runs**, so it is deterministic and not spawn variance. The difference between the two walks is
narrow and nameable — `boss-level` passes only the level-14 portal with stop predicate
`g_LevelId == 14` and budget 9,000, where `portal-level` passes **every** portal with stop
`g_LevelId != 10` and the seeker's default 6,000 — and that is the next thing to run down, because
it decides whether the boss and flight scenes need new traversal at all or only a fixed target set.

### MEASURED 2026-09-30 — ALL FOUR SCENES REFUSE, AND THREE OF THE FOUR SHARE ONE CAUSE

**0 of 4 scenes reached their target.** Every refusal is by name and carries the guest words it
read; none wrote a proof file, so `reach_corpus.py` reports them missing rather than covered. One
run each, product build `build/bin/spyro_port` (716/716 targets), driven through the REPL with no
guest RAM write:

| scene | what it had to reach | closest approach | what the guest actually did |
|---|---|---|---|
| `flight-level` | portal to level 15 (Sunny Flight) | **5,279** | the portal EXISTS and its `m_LevelId`/`m_Center` read correctly; the walk closed from 7,995 to 4,528 and then oscillated in a 4,500-5,300 band for the rest of its 9,000 fields |
| `boss-level` | portal to level 14 (Toasty) | **2,228** | same shape, closer in |
| `death-respawn` | `m_Position.z < 0x400` | lowest Z **6,399** | eight compass sectors and 7,200 fields never left `GS_Playing=0`; **the hub is bounded**, so there is no fall to die in |
| `save-fairy` | a class-110 fairy | **740** | four fairies, all abandoned; the nearest is 740 away and the cutscene still never opened |

**WHAT IS RULED OUT BY THESE NUMBERS, so nobody re-derives it.** The addresses are right:
`g_LevelNames` in `external/spyro-1/src/strings.c:109` lists Artisans as `HOME, STONE HILL, DARK
HOLLOW, TOWN SQUARE, TOASTY, SUNNY FLIGHT`, and `loaders.c:1396` computes
`g_Homeworld = g_LevelId/10 - 1`, so Artisans is `g_LevelId` 10-19 and **14 and 15 are correct**,
giving `g_LevelIndex` 4 and 5. The `Portal` offsets are correct too: `include/cyclorama.h` puts
`m_LevelId` at 0x1C and `m_Center` at 0x20, which is what `portal_targets` reads. So none of these
is a wrong-address story, and none of them is a missing-content story: the portal the flight route
walked toward was a real portal for the right level.

**THE CAUSE, and it is ONE cause for three of the four.** `Walk` (`tools/spyro1_steering.py:208`)
steers on bearing with `ARRIVED = 500`, `PROGRESS = 200`, `STALL_STEPS = 15`, detours
`(0, +60, -60, +120, -120)` and a single `cross` hop after 3 stalled decisions. Its own comment
already says portals sit on raised platforms. **It has no way to climb one**: a hop is one jump, and
Artisans' portals and fairies are up on ledges the walk reaches the base of and circles. The
`arrived=0` these scenes pass is correct — being next to a portal is not entering it — which means
the last few thousand units are exactly the part that has to be climbed, and that is the part no
decision in `Walk` can make.

**`save-fairy`'s 740 is the sharpest evidence and it is NOT the same fault.** 740 is *inside* the
0x400 = 1,024 trigger radius derived from the fairy proximity test, and the cutscene still did not
open. So either the trigger's `OctDistance` is a 2D XZ distance while the walker's 740 is a 3D
view-space distance — the fairy is close in three axes and far in the two the guest tests — or the
trigger has a second condition. **This one is a measurement to finish, not a traversal to add**, and
it is the cheapest of the four to resolve.

**`death-respawn` is a different cause and cannot be fixed by walking harder**: the hub has a floor
(lowest Z 6,399 against a plane at 1,024), so a death route must run INSIDE a level, which means it
needs a portal first, which means it inherits the same climbing problem.

**SO ALL FOUR REDUCE TO ONE PREREQUISITE: get into a level.** There is no route into a level yet.
The existing corpus route `portal-level` uses `--seek-portal`, which is the same `Seeker` with the
same `arrived=0` and the same stop predicate on `g_LevelId` (`tools/drive.py:641`), so it faces the
same wall and is worth re-measuring on its own: if it also refuses, the corpus has never entered a
level and every "level" number in it is really a homeworld number.

**WHAT IS REQUIRED, precisely.** Not pad replay and not a framework change: **flight traversal in
the walk policy.** Spyro reaches an Artisans portal by flying, and the honest route is
charge-and-glide — hold a bearing, hold `cross` to charge, release into a jump, keep the stick held
to glide — repeated until the gap closes. That is ordinary player input through the same REPL
`press`/`tap`/`run`/`release` the stepper already owns, so it needs a new DECISION in `Walk` (a
`fly` mode beside `hop`, and a stall rule that escalates to flight before abandoning a target), not
a new capability anywhere else. Until that exists, the correct report for all four states is
`missing`, and the useful next step is the `save-fairy` 740 case, which may not need flight at all.

### NOT YET MEASURED — the product binary could not be built

**Nothing in this section is a run.** The four routes above are code and derivation; the numbers
they were supposed to produce (each route's proof and field count, the corpus union before and
after, and the per-route additions over the 547 denominator) are not recorded here because the
product could not be built, and a coverage claim with no run behind it is the exact artefact issue
0147 exists to distrust. What IS recorded below is the blocker, measured.

`heavy.py --kind build` could not be admitted for the whole session. The machine's admission queue
is head-of-line blocked by a **zombie**, and it is a defect in `shared/re-harness`, outside this
worktree, so it is reported here and not patched here.

**MEASURED, 2026-09-30.** With 16,268,524 kB total and roughly 6,000 MiB `MemAvailable`:

| observation | value |
|---|---|
| build slots | 2, both held by wrappers that had already passed `MachineSlots` |
| those wrappers' children | none — neither was compiling anything |
| oldest `WaitingTicket` | pid 2929289, `reserve_mib` 1280 |
| that pid's state | `Z` — `[python3] <defunct>`, a zombie |
| its parent | 2882117, `swarmkit.reaper` under `swarm.py --workers 8`, alive, holding three unreaped children |
| `MemAvailable − outstanding − 1024 MiB floor` | 6,555 MiB — every other request would have fitted |
| builds actually running on the machine | 0 |

**THE CAUSE, in the harness.** `ReservationLedger.try_acquire` refuses any admitter with
`queue_ahead(ticket) > 0`, and `live_tickets()` keeps a ticket whose `process_alive(owner_pid)` is
true. `swarmkit.procs._alive` implements `process_alive` as `os.kill(pid, 0)`, which **succeeds for
a zombie**: a terminated process whose parent has not reaped it still has a `/proc/<pid>` entry and
still accepts signal 0. A zombie cannot make progress, so it never clears its own ticket, and the
ticket at the head of the FIFO refuses every later admitter for as long as the head is un-grantable
— here, forever, because nothing is consuming memory. Both build slots are taken by units already
past the slot gate, so no new request can even try. `live_entries`/`_entry_alive` use the same
predicate, so a zombie's *reservation* would pin headroom as well.

**THE REQUIRED CHANGE, in `shared/re-harness/tools/swarmkit/procs.py`.** `_alive` must not report a
zombie as alive. The module already has `_stat_fields`, so the smallest correct form is:

```python
def _alive(sender: Callable[[int, int], None], target: int) -> bool:
    # os.kill(pid, 0) succeeds for a ZOMBIE: a process that has terminated but whose
    # parent has not reaped it still has a /proc entry and still accepts signal 0. The
    # admission ledger asks this question to decide whether a waiting ticket still
    # constrains the queue, so a crashed admitter's ticket would never clear and would
    # head-of-line block every later request on the machine. A zombie cannot run, so it
    # is not alive. Kernel threads also report state Z and DO run, so this must read the
    # state and the PF_KTHREAD flag together, not the state alone.
    fields = _stat_fields(target)
    if fields is not None and fields[0] == "Z" and not _is_kernel_thread(fields):
        return False
    try:
        sender(target, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    except OSError as error:
        return error.errno != errno.ESRCH
    return True
```

`swarmkit/reaper.py` is the coarser half and is worth fixing with it: the reaper is a **subreaper**
whose whole job is to take down a command's subtree, and it is not reaping. Three of its children
were zombies at once, so every worker the swarm kills becomes a permanent entry. It needs a
`waitpid(-1, ...)` drain, or a `SIGCHLD` handler that reaps, so that a unit it killed does not
outlive it.

**SECOND, SMALLER, AND WORTH FIXING IN THE SAME PLACE.** `heavy.py`'s `on_wait` prints
`reserved {held} MiB < {floor} MiB`, where `held` is `outstanding`. The rule it is reporting is
`available − outstanding − mine < floor`, and the printed line does not contain `available`, `mine`
or the subtraction: at the moment of the deadlock it read `MemAvailable 6150 MiB - reserved 2465 MiB
< 2048 MiB`, in which 2465 is not less than 2048 and the sentence is a false report about a
condition that is not the one being tested. An operator reading that line cannot tell a queue that
is waiting on memory from a queue that is waiting on a slot, and that is the whole diagnostic value
of the line. It should print the actual predicate, and it should say once, by name, that the head
of the queue is un-grantable rather than repeating one line per poll.

**WHAT THE OPERATOR CAN DO TO UNBLOCK WITHOUT A HARNESS CHANGE.** Reaping the zombie clears its
ticket: `kill -CHLD 2882117` if that process installs a handler, or simply letting the swarm reap it.
Killing the two slot-holding wrappers (whichever `ps -eo pid,etimes,args` shows holding
`$HOME/repo/scratch/locks/heavy-build/slot-*.lock` with no children) frees the slots, but on
their own they will re-queue behind the same ticket. Reducing a request's `--mem-mib` does NOT help
while it is queued behind the zombie: the refusal is by queue position, not by size.

