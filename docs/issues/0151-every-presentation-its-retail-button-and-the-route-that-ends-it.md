---
id: 151
title: every Spyro 1 presentation, the button retail accepts on it, and the route that ends it
status: open
symptom: the skip work named four screens (docs/findings/start-skip-map.md) and the operator asked for all of them. The rest had never been inventoried, one of them turned out to have a retail skip nobody had recorded (the game-over spiral, held Start), a second had a terminal nobody had recovered, and the three arms the port does have had never been observed live — every one of them sits behind a portal or a pause menu, which no driver could reach
state_items: S011
tags: transition,skip,input,frontend,re
created: 2026-09-30
---

## Asked

Operator: make every Spyro 1 skippable presentation accept Start and Cross through a complete
cancellation route. The existing map covers four screens. This records all sixteen gamestates, what
retail itself does on each, and where the port stands — including the three findings that only
appeared once the other twelve were read.

## How each row's "retail button" column was established

Two different questions, answered two different ways, and conflating them is how "Start skips the
cutscene" gets written down when the game actually skips it with Circle:

* **Does the guest read a pad word on this screen?** A scan of the screen's own update function for
  `andi $v0, $v0, <mask>` over a value loaded from `g_Pad` (`g_Pad` is `m_Down` at +0, `m_Released`
  at +4, `m_Held` at +8 — `game/core/guest_globals.h`). Every such site is listed with its address.
  A screen with **no** such site has no button of its own, which is a different statement from "the
  button does nothing": it means the screen is not reading the pad at all.
* **What does that mask mean?** `external/spyro-1/include/gamepad.h`: `PAD_CROSS = 1<<6 = 0x40`,
  `PAD_CIRCLE = 0x20`, `PAD_SELECT = 0x100`, `PAD_START = 1<<11 = 0x800`, `PAD_UP/DOWN = 0x1000/0x4000`.
  So `0x840` is **Start or Cross** and `0xA000` is **Start or Circle** — the two masks the map
  already quotes, and both are this header, not a guess.

Instruction words are quoted as `tools/probe_guest_disasm.py` prints them (little-endian word, image
mapped from the PS-X EXE header: `file_offset = 0x800 + (addr - 0x80010000)`, cross-checked against
62,183 listing instructions) and as `external/spyro-1/asm/nonmatchings/...` prints them. Addresses
for globals come from `tools/re_globals.py`, which pairs each `lui` with its low half numerically.

## The inventory

Sixteen gamestates, in the order the main loop dispatches them. "Port" is what the port does on a
Start or Cross edge today; "route" is the thing the press has to reach.

| # | presentation | retail button | where the guest reads it | natural terminal | route | port status before | port status after |
|---|---|---|---|---|---|---|---|
| 0 | boot logos (SCEA / Universal) and their two VBlank holds | **none** — there is no pad read in the boot function | `0x800127C0` reads no pad word; the two holds are `now - stamp >= 210` | the hold expiry, then the fade-out | enhancement: a press takes the same cleanup (`0x80016914`) and fade the expiry takes | **done**, `titles/spyro1/core/spyro1_boot_sequence.cpp` `Phase::HoldFirst` / `HoldSecond`, both on `presentationSkipPressed()` (Start **or** Cross) | unchanged; **the Cross leg fired live** this session (`[boot-native] Start/Cross ends Spyro 1's first presentation hold`), the Start leg is queued |
| 0 | boot's 3→10 loader | **none**, and must stay none | — | `g_LoadStage` reaching 10 | *not a presentation*: required I/O | no skip, deliberately | unchanged |
| 13 | title screen fly-in + "PRESS START" platform | **Start** (and Cross in the menu arms) | overlay `OV_5B800` at `0x8007AA38`: `0x8007AC48` (held) and `0x8007B88C` (edge), the latter behind a `0xA000` Circle-or-Start test at `0x8007B894` | the overlay's own sub-state chain to `TSM_Menu` | guest-owned; the driver answers it (`title_prompts.py`) | guest-owned, no native arm, and none is correct | unchanged |
| 13 | attract flyby card, `TSD_DemoLevel` (what boot reaches after the idle timeout) | **none on the card** | the card's own update reads no pad word; the *demo playback* it enters does | `0x8003314C` (`g_DemoMode=1`), `0x80033154` (`g_DemoFadeTimer=0`), `0x80033158 jal 0x8004AC24`, `0x80033160 jal 0x80015370` | would have to reproduce the two demo stores | deliberately absent, and the reason is measured: widening the predicate to `TSD_DemoLevel` would load the *demo's* level as if it were a new game (issue 0129) | unchanged |
| 0 | attract demo playback (`g_DemoMode` set, inside `GS_Playing`) | **ANY button**, after 16 game ticks | `func_800334D4` at **`0x800334D4`**, called once from the play dispatch at `0x80033A78`; `PadDemoUpdate()` then a test on the recorded pad buffer | the `g_DemoFadeTimer == 16` block inside the same function: kills audio, clears, reloads the title overlay synchronously, `func_8002D170()`, `g_StateSwitch = 1` | guest-owned and complete | guest-owned; no native arm, and none is correct | unchanged; **not reached** by any run this session — the census names `demo` as never reached |
| 13 | new-game flyby, `TSD_Cutscene` — the 384-tick card before the intro | **none** | the card's own update reads no pad word | `0x8003304C..0x800330E8`: inline `RECT` on the guest stack, `0x80033070 jal 0x8005F8F8` (ClearImage), `0x80033078 jal 0x8005F764` (DrawSync), `0x80033098 jal 0x8005B6F8` (AllocateBuffers(1)), a blocking `0x80014564` (LoadCutscene) + `0x8002BBE0` (CDMusicUpdate) loop to `g_LoadStage == 10`, `0x800330D0 jal 0x8002D338` (StartCutscenePlayback), `0x800330D8` (`g_StateSwitch = 1`) | **no dispatchable route** — see "the one that cannot be dispatched" below | absent | unchanged; the reason is now measured rather than asserted |
| 14 | **intro cutscene** ("In the World of Dragons", `g_CutsceneIdx == 1`) | **Start or Cross, HELD** | `0x80033348 lui $v0,0x8007` / `0x8003334C lw $v0,0x7380($v0)` = `g_Pad.m_Held`, then **`0x80033354 andi $v0,$v0,0x840`**; `0x80033374 slti $v0,$v1,0xf1` (tick ≥ 241), `0x8003338C addiu $v0,$v0,-0x20` (before the last 32), `0x80033398 sra $v0,$v1,1` + `0x8003339C addiu $v0,$v0,0x10` → `g_CutsceneLayout->m_Duration = (tick>>1)+16` | `0x800333C4 jal 0x8002D440` = `EndCutscenePlayback` | guest-owned, and it is an *acceleration toward* the natural terminal, not a jump | **unmeasured** — no driver could press during it, and `--skip-transitions` pressed only Start as a 4-field edge, which lands before the 241-tick gate and changes nothing | `--skip-transitions` now holds the button for 400 fields, and `--skip-button cross` chooses which; **the held Cross fired live** (`press: cross while in GS_Cutscene at frame 2480`, arrival at 3520); the Start leg and the baseline it is differenced against are queued |
| 13 | "THE ADVENTURE BEGINS…" flyby, `TSD_Level` | none in retail (port enhancement) | — | `0x80033158 jal 0x8004AC24` + `0x80033160 jal 0x80015370`, gated on `m_Tick >= 384` **and** `g_LoadStage == 13` | port arm `CutsceneTransitionFlyby` | done; live-measured once, 760 fields earlier | unchanged |
| 1 | level-transition gem tally (portal in and out) | none in retail (port enhancement) | — | `0x8002DA74`: once `g_LevelTransTicks > 416` it clears `g_LevelTransHudActive` (`0x800756B0`) and does nothing else | port arm `LevelTransitionTally` | done; **never observed live** — both routes into stage 1 sit behind a portal | reachable now: `--press-after 1:start` / `1:cross`; **not run** — the leg is queued behind the host blocker below |
| 9 | level entrance camera sweep | **none**, and there is no guest acceleration route | `func_8002E000` is 33 instructions with two `jal`s and reads no pad word; the whole main image has exactly two `0x840` sites (the pause menu's confirm at `0x8002E988` and the cutscene's own skip at `0x80033354`) and the WAD overlays add none that reach this screen (issue 0141, claim C229, 36 code entries / 501,760 words) | `0x8002E070 sw $zero, 0x57d8($at)` = `g_Gamestate = GS_Playing`, reached only from the camera gate | none that is not a scene-pointer write | absent, and issue 0138's measurement says ending it early does not strand the camera — the blockers are that the store is not dispatchable and that the only other lever is a camera write | unchanged |
| 10 | return-home glide (pause-menu Quit in a sub-level) | none in retail (port enhancement) | `func_8002E084` reads no pad word | on its second counter wrap: `g_Down824 = 0`, `g_Down828++ == 2` → `jal 0x8002C664` | port arm `ReturnHomeSequence`, dispatching `0x8002C664` | done; **never observed live** | reachable now via `--press-after 10:…` on a pause-quit route; **not run** — queued; and it has a **predicted** state divergence, from the guest's own counter chain rather than from a diff — see below |
| 5 | **game-over spiral** | **Start, HELD**, once `g_GameOverTicks >= 0x169` and `g_LoadStage >= 0xB` | `func_8002EDF0`: `0x8002F32C lui $v0,0x8007` / `0x8002F330 lw $v0,0x7380($v0)` = `g_Pad.m_Held`, **`0x8002F338 andi $v0,$v0,0x800`**, then `0x8002F344 jal func_8003FDC8`, `0x8002F350` `D_80075940 = 2`, `0x8002F35C g_GameOverTicks = 0` | the same block, reached by the tick counter instead of the button | guest-owned and complete | **not in the skip map at all** — nobody had read this function for a pad test | recorded here; guest-owned, so no native arm; drivable with `--press-after 5:start:…` once a run can die |
| 4 | respawn spiral | none | same function, but its arm at `0x8002EE74` tests `g_Gamestate == 4` and calls `0x8002C8A4` + `0x800144C8`; the held-Start site is on the game-over arm only | `0x8002C8A4` | none | absent | unchanged; measured as "reads no button on this arm" |
| 8 | dragon rescue cutscene | **Cross**, as *continue*, not as skip | `func_8002F3E4`: `0x80030CC8` / `0x80030CCC` = `g_Pad.m_Down`, **`0x80030CD4 andi $v0,$v0,0x40`**, then it advances `g_DragonCutscene`'s own script fields | the cutscene's own state machine | guest-owned; there is no skip | absent, and absent correctly | unchanged |
| 11 | fairy save | **Cross** (three tests) and **Down** (menu navigation) | `func_800314B4`: 6 `g_Pad.m_Down` reads; `0x80031E40 andi $v0,$v1,0x4000` (Down) and `0x80031F14` / `0x8003207C` / `0x80032360` all `andi $v0,$v0,0x40` (Cross) | the fairy's own script | guest-owned | absent | unchanged |
| 12 | balloonist flight | **none — this one reads no pad word at all** | 0 of `func_800324D8`'s 338 instructions form an access to `g_Pad` (`re_globals.py` over the image) | `.L800329CC`: `lui $v0,%hi(D_8007574C)` / `lw $v0,(%lo)` / **`jalr $v0`** — a jump through a level-supplied function pointer, read off the image at `0x800329D0`/`0x800329D8` | none: the terminal is not an address in the resident image | absent | unchanged; the reason is now measured (0 of 338 instructions touch the pad; the terminal is a computed `jalr`) |
| 7 | flight-level results | not read | overlay function, outside the resident text | overlay | not established | absent | unchanged |
| 15 | credits | not read | overlay `func_credits_8007AA50`, outside the resident text | `func_800333DC` at `0x800333DC` (stage > 98) | not established | absent | unchanged |
| 2 / 3 | pause menu / inventory | Start **and** Cross are menu commands | `func_8002E12C`, `func_8002EB2C` | — | not skips | `press_conditions.PostArrivalPressConditions` refuses both by name | unchanged |

### Denominators behind the "reads no pad word" column

Counted from the ADMITTED IMAGE, not from the decompiled listing, with `tools/re_globals.py`, which
pairs each `lui` with its low half numerically and so cannot be fooled by a listing that desyncs on
data. "g_Pad reads" is the number of formed accesses to `0x80077378` / `0x8007737C` / `0x80077380`;
"mask tests" is the number of `andi` instructions on the loaded word.

| function | gamestate | instructions | g_Pad reads | mask tests |
|---|---|---|---|---|
| `func_8002F3E4` | 8 dragon | 2,109 | 1 (`m_Down` @ `0x80030CCC`) | `0x40` Cross |
| `func_800314B4` | 11 fairy | 1,044 | 6 (`m_Down`) | `0x4000` Down @ `0x80031E40`, `0x40` Cross @ `0x80031F14`, `0x8003207C`, `0x80032360` |
| `func_800324D8` | 12 balloonist | 338 | **0** | **0** |
| `func_8002EDF0` | 4/5 respawn, game over | 373 | 1 (`m_Held` @ `0x8002F330`) | `0x800` Start @ `0x8002F338` |
| `func_8002E000` | 9 entrance | 33 | 0 | 0 |
| `func_8002E084` | 10 exit level | 42 | 0 | 0 |
| `func_8002DF9C` | 1 level transition | 25 | 0 | 0 |
| `GamestateCutsceneUpdate` `0x800331AC` | 14 cutscene | 140 | 1 (`m_Held` @ `0x8003334C`) | `0x840` Start-or-Cross @ `0x80033354` |

The balloonist's own terminal is in the same table's evidence: `re_globals.py` over its 338
instructions reports `0x8007574C lw@0x800329D0` and no `g_Pad` access at all, and `0x800329D8` is the
`jalr $v0` that leaves the state. So "no button" and "no resident address to call" are both read off
the image rather than inferred from the decomp's silence.

## Finding 1 — the game-over spiral is skipped by retail on held Start, and no document said so

`func_8002EDF0` serves `GS_Respawn` and `GS_GameOver`. After the load reaches stage 11 and
`g_GameOverTicks` passes 361, the game-over arm tests the **held** pad word against `0x800` and, if
Start is down, calls `func_8003FDC8(0)`, sets `D_80075940 = 2` and zeroes `g_GameOverTicks`.

This is a complete guest-owned route of exactly the shape the port's own arms use, and the skip map
— which claims to be "the evidence boundary for making logos, loading screens, and scripted sequences
skippable" — does not mention `GS_GameOver` at all. It was missed because the map's method was a scan
for `0x840` (Start **or** Cross) plus a quoted list of overlay addresses, and this site is `0x800`
(Start alone) in the **main** image: a census that only looks for the two-button mask cannot see the
one-button skip. The general rule is the same one this workspace keeps re-learning in the other
direction — the mask you scan for decides what you find, and a clean result means "not this mask".

Consequence for the port: **no native arm.** The guest owns it, and a second mechanism would fight the
guest's own. What the port owes is that the held Start reaches the guest, and a driver route to
observe it. `press_conditions` accepts `5:start:<frames>` today (`--press-after 5:start:400`); no
drivable route reaches a death yet, so the census reports `game_over` as never reached rather than
pretending otherwise.

## Finding 2 — the return-home arm leaves one word different, and it has never run

The glide's own counter chain is `D_800758B8` (the 32-frame wrap counter `0x8007568C` is
`D_8007568C`), and on the **second** wrap it calls `0x8002C664`. `func_8002C618` (the pause-menu Quit
that enters the glide) zeroes both `D_8007568C` (`0x8007568C`) and `D_800758B8` (`0x800758B8`) —
measured with `tools/re_globals.py` on `0x8002C618`, which reports exactly three stores, at
`0x8002C630`, `0x8002C638` and `0x8002C640`.

So the natural route leaves `D_800758B8 == 2`, and the port's arm — which dispatches `0x8002C664` on
the press, at whatever point of the chain the press lands — leaves it at 0 or 1. **This is a
divergence in the post-skip state, found before the arm was ever run rather than by running it**, and
it is reported here with the RAM comparison below rather than smoothed over.

It is bounded and cosmetic, and the bound is measured rather than assumed: `D_800758B8` is the pause
menu's frame counter, read by `func_8001A40C` (the gamestate 2/3 draw) to rotate the menu text and
gated at `== 0` for its one-time setup, and by `func_8001C694` (the gamestate 10 draw) to pick one of
three full-screen cases. Nothing else writes it during the glide — the decomp has 26 references and
every writer is the pause-menu draw, the pause-menu enter/exit, or the glide's own chain.

**It cannot be fixed inside a skip.** Making the word match would mean writing it, and this is exactly
the phase write the rules forbid; and the alternative — waiting for the guest's own wrap — is not a
skip, it is the screen finishing. So the arm stays, and the divergence is stated.

## Finding 3 — the card before the intro cannot be dispatched, and now there is a measurement for it

`TSD_Cutscene`'s terminal is a straight-line block at `0x8003304C..0x800330E8` that ends with
`0x800330E4 j 0x80033190`, the **epilogue of the function it lives in**. Dispatching into the middle
of it runs the block and then reloads `$ra` from the guest stack, which is the same clobber issue
0138 measured for stage 9's terminal. On top of that the block needs an argument that has no address:
`ClearImage` is handed a `RECT` built on the guest stack at `$sp+0x20`, and this game has no
dispatchable "clear the screen" wrapper — all 12 `ClearImage` call sites in the decomp build the RECT
inline.

So the screen stays uncancellable, and now for a stated reason rather than an assertion. What would
unblock it, precisely: either a framework entry point that can run a mid-function guest block with a
synthesised frame (psxport, outside this repository), or a guest-owned full-screen clear this game
does not have. Both are named so the next person does not re-derive them.

## The harness gap this session closed

`--press-while` stopped applying the moment `reach_gameplay()` returned, so **every screen between
two levels was unreachable by any driver**: the tally, the entrance sweep and the glide are entered by
a portal walk or a pause menu, not by a frame count. Those three arms therefore had unit tests and no
observation at all, which is indistinguishable from not working.

`tools/press_conditions.py` (renamed from `pre_arrival_press.py`, which is what it stopped being) now
owns both phases: `PressConditions` for everything up to `GS_Playing`, applied only from
`Navigator._advance()`, and `PostArrivalPressConditions` for everything after it, applied from
`Port.run()`'s own sampler so it is live while the run walks a level. The post-arrival set **refuses
`GS_Playing`, `GS_PauseMenu` and `GS_InventoryMenu` by name**, because the pre-arrival set's guarantee
was structural and this one is not: a spec naming `GS_Playing` would fire at the first gameplay sample
of the run and open the pause menu, which is the exact live failure the module's docstring records.

`tools/ram_compare.py` is new and is the instrument acceptance needs: it reads two `drive.py
--dumpram` captures and compares a **named** field list — the fields that define a hand-off, plus the
clocks, reported separately because a skipped run is *expected* to read a different clock. It exits 1
when a hand-off field differs, which is the finding, and it refuses a missing or short capture rather
than reading one as zeros.

## Finding 4 — the flyby arm accepts Cross and did NOT accept Start in the one leg that tried it

Leg 4 pressed Cross on the "THE ADVENTURE BEGINS" card and the product logged
`[transition] level flyby cancelled (1)`. Leg 5 pressed **Start** on the same card at the same place
in the route (driver: `cancel level_flyby: start at frame 2920`, against leg 4's Cross at the same
frame) and the product logged **nothing**: no `[transition]` line, and the census reads
`title_screen=296` — the card ran its full 384 ticks, which is why leg 5 arrives at 3680 exactly like
the cutscene-only legs and not at 2960.

**The two buttons are supposed to be interchangeable here, and the owner says so.**
`FieldOwner::presentationSkipPressed()` is `game_.pad.pressedButton(kPadStart | kPadCross)` — one
edge, either button — and `TransitionSkip::observe` latches `flybyPressHeld_` from that single value.
So the difference cannot be the mask. **What I have not established is which of the two it is**, and
saying "the arm has a Start bug" would be exactly the attractive wrong lead this workspace keeps
paying for:

* **A dead tap, or the wrong candidate.** The Start press also reaches the guest's own reader. On the
  title screen's fly-in the guest reads Start for the "PRESS START" platform, and a Start edge while
  the card is up may be consumed by that reader one field before `observe()` samples it, so
  `presentationSkipPressed()` is false on the only field the arm looks at. The arm latches only what
  it saw on a field where the CARD was already up.
* **Timing.** The driver presses on a 10-field sample grid, so a Start edge can land up to 10 fields
  from the point the Cross edge landed, and the card's own load gate (`g_LoadStage == 13`) may have
  opened and closed in between. Leg 4's Cross happened to land inside the window; leg 5's Start did
  not.

What would settle it, named so it is not re-derived: **one run with `--press-while 13:start` and one
with `--press-while 13:cross`, same frame budget, comparing the product's own skip-map counters**
(which the field owner already keeps: `skipMapStartEdges_`, `skipMapFields_`) rather than the arrival
field. If the Start edge is counted and the arm still does not fire, the tap is not reaching the
owner and the bug is in delivery; if it is not counted, the guest consumed it. Until that run exists
the honest statement is the measurement above, not a diagnosis.

**Meanwhile the Start leg of this screen is UNPROVEN, and no document may say otherwise** — including
the boot hold, where Start *is* measured (leg 6, 180 fields, `[boot-native] Start/Cross ends Spyro 1's
first presentation hold`), and the intro cutscene, where Start is measured (leg 3, the same 2,680
fields as Cross). Those are three different screens and three different owners; a button that works on
two of them is evidence about those two.

## Two harness defects the legs exposed, both fixed

**A prompt with no memory re-fires after its screen is gone.** `load_route_prompt()` is consulted
once per observation step and derives its answer from the guest's CURRENT screen struct. After the
flyby arm cancels the card, the title struct can still read like the card, so the same prompt fires
again — and with **Start** that is the pause button. Leg 5's first attempt died exactly there:
`drive.py REFUSED: left the load route into unexpected gamestate 2`, with the census showing
`pause_menu=2`. With Cross the same re-fire does nothing at all, which is worse: it reads as a pass.
Every one-press-of-one-presentation prompt now carries a `target`, and the Navigator records the
(target, button) pairs it has delivered. A prompt that is a **navigation** carries `repeatable=True`,
because the save picker's Left must repeat until the guest's own option word reads NEW GAME — the
first version of this fix keyed on the screen alone and stalled the picker instead, which cost three
runs and is the reason both halves are pinned in the selftest.

**`--skip-transitions` cannot be measured with Start until that is fixed**, so the Start leg of the
three cancellations is a *harness* result today and a *port* result only for the cutscene (which the
guest owns, and which leg 3 measures at the same 2,680 fields as Cross).

## Live measurements

Every leg below ran to completion on ONE product build, this worktree's
`build/bin/spyro_port` at `origin/main` 93b49f1, headless and silent, one instance at a time, with
`tools/shipping_settings.ini` and `--settle 0` so the RAM capture lands at the hand-off rather than
120 fields after it. `gate`: **112 of 112 CTest tests passed** on that build. The driver's own
census is the denominator for every claim about what a leg reached.

| # | route | arrival | census (10-field samples) | what it proves |
|---|---|---|---|---|
| 1 | baseline, no press | **6360** | 637 samples: `title_screen=296, cutscene=295, playing=46` | the unskipped route, and the guest state a skip must reproduce. 6360 matches the figure already recorded in `docs/project-state.md`, so the baseline is not a regression |
| 2 | `--press-while 14:cross:400` | **3680** | 368: `title_screen=296, cutscene=28, playing=44`; `press: cross while in GS_Cutscene at frame 2640` | the intro cutscene's own route on a HELD Cross: **2,680 fields** |
| 3 | `--press-while 14:start:400` | **3680** | 368: `title_screen=296, cutscene=28, playing=44`; `press: start while in GS_Cutscene at frame 2640` | the same screen, the other button: **the same 2,680 fields**, from a different button |
| 4 | `--skip-transitions --skip-button cross` | **2960** | 297: `title_screen=223, cutscene=28, playing=46` | cutscene **and** the flyby card on Cross: **3,400 fields** total; product log `[transition] level flyby cancelled (1); guest 0x8004AC24 then 0x80015370` |
| 5 | `--skip-transitions --skip-button start` | **3680** | 369: `title_screen=296, cutscene=28, playing=45` | the cutscene on Start (**2,680**, as leg 3) and **the flyby arm did NOT fire**: no `[transition]` line in the product log and the title screen is a full 296 samples. See Finding 4 |
| 6 | `--press-while 0:start` | **6180** | —; product log `[boot-native] Start/Cross ends Spyro 1's first presentation hold` | the boot presentation hold on Start: **180 fields**. The Cross leg of the same hold fired in an earlier run and is logged in `scratch/logs/smoke-cross.log` |

**Frames skipped, attributed the only way this evidence allows.** Leg 2 minus leg 1 is the cutscene
and leg 4 minus leg 2 is everything the `--skip-transitions` leg adds, and the two agree with the
census: `cutscene` samples fall 295 -> 28 (**2,670 fields**) and `title_screen` falls 296 -> 223
(**730 fields**), which is the flyby card. The residual 6360 - 2960 = 3,400 against 2,670 + 730 =
3,400 is the same number two ways, so the attribution is not a guess.

**What is NOT measured, and is not claimed.** The census of every leg names the same thirteen
gamestates it never reached — the tally, the entrance sweep, the glide, the game-over spiral, the
fairy, the balloonist and the rest — because all six legs are TITLE ROUTES that stop at arrival. So
the three arms that sit between two levels (tally, glide) and the two that need a level loaded first
have unit tests and **still no observation**. Their routes now exist in the driver
(`--press-after 1:cross`, `--gate-teleport 0:0 --seek-portal --quit-home --press-after 10:cross`),
but no leg above used them, and nothing in this section should be read as covering them.

### RAM comparison at the hand-off

`tools/ram_compare.py` over a **named 33-field list** (25 hand-off fields and 8 clock/pad fields),
reading two 2 MiB `--dumpram` captures. It exits 1 on a hand-off difference, and refuses a missing
or short capture.

| pair | equal | differing | what differed |
|---|---|---|---|
| baseline vs leg 4 (Cross) | **31 of 33** | 2 | only `g_Spyro.m_Position.y` and `.z` — both classified `[clock/timing]` |
| baseline vs leg 5 (Start) | 28 of 33 | 5 | the two positions plus `g_Fade` (12 vs 22), `g_GameTick` (10 vs 5), `g_LevelTicks` (19 vs 9), all `[clock/timing]` |
| leg 4 vs leg 5 | 28 of 33 | 5 | the same five |

**Every hand-off field agrees in all three comparisons.** That includes `g_Gamestate` (0),
`g_LoadStage`, `g_LevelId`/`g_NextLevelId`/`g_PortalLevelId`, `g_HasLevelTransition`,
`g_LevelTransTicks`, `g_LevelTransHudActive`, `g_StateSwitch`, `D_800758B8`, `D_8007568C`, all of
`g_Spyro`'s position/state/health/animation, both camera fields, the three title-screen fields,
`g_CutsceneIdx`, `g_CutsceneLayout`, `g_DemoMode`, `g_DemoFadeTimer` and `g_LevelMobys`. The
differences that remain are exactly what a skip is expected to move: the clocks read fewer ticks at
the same field count, because the screens that would have consumed them were cancelled.

`D_800758B8` is in the list precisely because claim C011 predicts it on the return-home leg; this
route never enters the glide, so it reads 0 in all three captures and says nothing about that
prediction. The falsifier stays open.

## Host incidents during this session, both resolved outside this repository

**An unreaped zombie held the machine-wide admission queue for 28 minutes.** A `heavy.py` belonging to
another session's swarm (pid 2929289) took an admission ticket at 15:56:44 and died without being
reaped; its parent, that swarm's `swarmkit.reaper`, stayed alive, so the zombie persisted.
`ReservationLedger.live_tickets()` keeps a ticket whose owner is "alive", and `process_alive` is
`os.kill(pid, 0)`, which **succeeds for a zombie**. `_queue_ahead()` then counts every live ticket
older than yours, so one dead waiter deferred every admission on the machine while 8.9 GiB was free.
Fixed upstream in re-harness (d9b9372); the change is `process_alive` -> the already-existing
`process_running` in `swarmkit/reservations.py`, and the operator applied it.

**`vendor/beetle-psx` was emptied, which broke every spyro worktree's reconfigure.** The shared
`~/repo/psx/psxport` checkout lost `vendor/beetle-psx` and its module store, so
`add_subdirectory(${PSXPORT_DIR}/vendor/beetle-psx/deps/libchdr)` failed and `cmake --build` could not
even re-run CMake. Restored with the framework's own prescribed command from that checkout:

    git -C ~/repo/psx/psxport submodule update --init -- vendor/beetle-psx

and `python3 ~/repo/psx/psxport/scripts/sync_submodules.py` now reports **3 of 3 submodules at their
recorded gitlinks**, with `vendor/beetle-psx` at `5791d27a` — the exact revision psxport records. Two
attempts failed first with `invalid index-pack output` because another agent's clone was writing the
same module directory at the same time; the third, once it was quiet, succeeded.

## What is still missing

- `GS_FlightResults` (7) and `GS_Credits` (15) are overlay-owned and were not read; the map does not
  claim otherwise and neither does this.
- No drivable route reaches a death, so the game-over spiral's held-Start route is recovered from
  bytes and unobserved — it is a guest route, so it needs a driver that can die, not a port change.
- `GS_EntranceAnimation` (9) and the `TSD_Cutscene` card remain without a cancellation, for the two
  measured reasons above.
- **The three arms that sit between two levels — the level-transition tally, the entrance sweep and
  the return-home glide — are still unit-tested and unobserved.** Every leg in this issue is a title
  route that stops at arrival, and each leg's census names them as never reached. Their driver routes
  now exist (`--press-after`, `--seek-portal`, `--quit-home`); the legs that use them have not been run.
- **The flyby arm's Start half** is unproven for the reason and with the discriminator spelled out in
  Finding 4.
- **Claim C011's RAM prediction** (`D_800758B8` on the return-home leg) is still a prediction: this
  route never enters the glide, so the word reads 0 in every capture taken. `ram_compare.py` already
  carries the field, and the leg that exercises it is the quit-home route above.

## Harness note for whoever runs this next

Every leg in this section is one command of the form

    ~/repo/shared/re-harness/tools/heavy.py -- python3 tools/drive.py gameplay \
        --settings tools/shipping_settings.ini --settle 0 <the flags in the table>

with no `--kind`, no `--lock-dir` and no `--mem-mib`: heavy.py's flags were removed (re-harness
13817db) and passing any of them exits 2. Output goes to a file under `scratch/tasks/`, one product
instance at a time, headless and silent. The RAM comparison is a plain tool, not a run:

    python3 tools/ram_compare.py scratch/ram/base.bin scratch/ram/skip-cross.bin

The gate that has to be green before any of it counts is `ctest --test-dir build` plus its count read
in the same command (a directory with no `CMakeCache.txt` reports zero tests and returns 0, which is
how a gate can pass without running). On this build it is **112 of 112**.
