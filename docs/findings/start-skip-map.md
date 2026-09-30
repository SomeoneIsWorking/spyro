# Start-to-skip map

**THE FULL INVENTORY IS `docs/issues/0151`.** This file remains the evidence boundary for the
mechanism — what a cancellation is allowed to do, and which guest route each screen's press reaches.
Issue 0151 carries all sixteen gamestates, what retail's own bytes say each one accepts, and where the
port stands per screen. Read that one for "is this screen skippable"; read this one for "what does the
press do when it lands".

**A METHOD CORRECTION that came out of building that inventory, because it invalidates a scan-shaped
claim in this file.** The census this document relies on — "the Start/Cross mask this map establishes
for the title", `andi rt, rs, 0x840` — finds **Start or Cross** and nothing else. The game-over spiral
is skipped by retail on held **Start alone**: `func_8002EDF0` at `0x8002F32C`/`0x8002F330` loads
`g_Pad.m_Held` and `0x8002F338 andi $v0, $v0, 0x800` is the whole test, after which it calls
`0x8002F344 jal func_8003FDC8`, sets `D_80075940 = 2` and zeroes `g_GameOverTicks`. A census that looks
for one mask reports "no skip" for a screen the game skips with a button. **The mask you scan for
decides what you find**, in both directions — this is the same lesson as the dead-tap rows in the
workspace map, arriving from the other side.

This deliberately does not equate "Start is down" with "jump to the next state": Start is also the
gameplay pause button, and several screens perform required I/O while their artwork is displayed.

## Boot logos and loading

The resident boot function `0x800127C0` owns the entire sequence. A recovered runtime trace
establishes the following order:

1. eight fade-in iterations for the first uploaded logo;
2. three synchronous data loads, then a VBlank hold until `now - stamp >= 210`;
3. eight fade-out iterations;
4. eight fade-in iterations for the second uploaded logo;
5. sets loading phase `[0x80075864] = 3` and calls `0x80014564` until the phase reaches 10;
6. another VBlank hold until `now - stamp >= 210`;
7. eight fade-out iterations, then the display/frame-loop setup.

There is no pad read or Start test in `0x800127C0`. Boot-logo skipping is therefore a PC
enhancement, not a dormant guest branch. The safe semantic boundary is narrower than "leave boot":
the 3→10 loop is required loading work and may not be bypassed.

`titles/spyro1/core/spyro1_boot_sequence.cpp` now owns the enhancement at the only two
presentation-only holds. After `loadAssets()` has completed, a Start or Cross press takes the same
first-hold cleanup (`0x80016914`) and fade-out route that natural expiry takes. After the 3→10 loader
has completed, a Start or Cross press takes the existing second-logo fade-out route. The title-owned
`FieldScheduler` supplies the final effective pad edge; `BootSequence` alone decides whether it is a
valid skip, and it does not suppress that input for later game states. Neither route advances the
VBlank clock, bypasses callbacks, writes a guest timer/phase/scene, or skips loading.

Both input shapes were exercised in a real 320-present headless run on 2026-08-31: `FFF7` (Start)
and `BFFF` (Cross) each logged both hold transitions and `native boot reached the gameplay frame
boundary` with no frame-contract failure.

`PSXPORT_DEBUG=skipmap` measures this live. The observer wraps `0x800127C0`, so `region=boot` is
derived from the function's actual dynamic lifetime rather than a guessed frame range. Each Start
edge and every boot-phase/stage-state change is uncapped; every 600 fields it prints the denominator
including `start_edges=0` when it scanned input and found none.

The field scheduler retains only the exact boot lifetime for `skipmap` diagnostics. It observes and
reports Start edges without changing the VBlank clock or boot flow.

## Title / attract sequence

The title/attract overlay has a real Start path and should keep using it. Claim C110 and issue 0027
establish the two input shapes in the resident `OV_5B800` image:

- `[0x80077380]` is held input; `0x8007AC48` tests Start while the title timer is armed.
- `[0x80077378]` is newly-pressed input; `0x8007B88C` tests Start/X in the sub-state-1 arm.

**CORRECTED 2026-09-28 by `docs/issues/0141` / claim C229, which read the overlay bytes.** The
second address is the `lw` of the EDGE word, and that word is loaded ONCE and then tested twice: the
mask consumed first is `0xA000` at `0x8007B894` (`beqz` to `0x8007B8BC`), and only then the `0x840`
at `0x8007B8BC`. The sub-state-1 arm therefore tests Circle-or-Start *before* it tests
Start-or-Cross, and `0x840` is its second gate rather than its only one. `0xA000` occurs at 10 sites
across the WAD overlays, so a census that looks only for `0x840` cannot see it. The arm's meaning, and
the conclusion drawn from it, are unchanged.
- the legitimate transition writes stage sub-state 2 and sub-sub-state 5 at
  `0x8007B8F0..0x8007B8F8`; it then reaches sub-state 3 through the guest's memory-card completion
  chain. That chain is now functional (issue 0027 resolution), so no PC state poke is justified.

Repeated synthetic Start pulses are not a shipping skip mechanism: once the sequence hands off to
gameplay, another pulse opens the pause screen. A shipping implementation must consume one host edge
inside a positively identified skippable state and release it before the next state reads input.

## The card before the intro (`TSD_Cutscene`), recovered and not cancellable

This is the 384-tick card a NEW GAME flies before the intro cutscene, reached from
`overlays/titlescreen.c:479` (`m_Mode = TSM_Loading`, `m_State = 5`, `m_DemoType = 0`). Its terminal is
recovered from the image, and it is the one transition whose terminal **cannot** be dispatched:

```
0x8003300C  lw    $v0, 0x5864($v0)      ; g_LoadStage < 7 -> jal 0x80014564 (LoadCutscene)
0x80033024  lw    $v0, -0x7280($v0)    ; m_Tick (0x80078D80) < 0x180 (384) -> not yet
0x80033038  addiu $v0, $zero, 7 ; bne g_LoadStage, 7 -> not yet
0x8003304C  ...   $sp+0x20 = {0, 0, 0x200, 0x1E0}      the RECT, built on the GUEST STACK
0x80033070  jal   0x8005F8F8           ; ClearImage(&rc, 0, 0, 0)
0x80033078  jal   0x8005F764           ; DrawSync(0)
0x80033098  jal   0x8005B6F8           ; AllocateBuffers(1)
0x800330A8  jal   0x80014564           ; LoadCutscene  } while (g_LoadStage < 10) {
0x800330B0  jal   0x8002BBE0           ; CDMusicUpdate }
0x800330D0  jal   0x8002D338           ; StartCutscenePlayback
0x800330D8  sw    $v0 -> 0x8007579C     ; g_StateSwitch = 1
0x800330E4  j     0x80033190           ; THE FUNCTION'S OWN EPILOGUE
```

Two independent reasons, both structural rather than "the decomp does not say":

1. The block ends by branching to the enclosing function's epilogue, so entering it mid-function runs
   the block and then reloads `$ra` from the guest stack — the same clobber `docs/issues/0138`
   measured for stage 9's terminal. No prefix of this block is dispatchable.
2. `ClearImage` is handed a `RECT` that lives on the guest stack at `$sp+0x20`, and this game has no
   dispatchable full-screen clear: all twelve `ClearImage` call sites in `external/spyro-1` build the
   RECT inline, so there is no guest function to call and no address to pass.

The load loop is required I/O and is not the obstacle — running it is the point. What would unblock
this screen is named in `docs/issues/0151` so it is not re-derived: a framework entry point that can
run a mid-function guest block with a synthesised frame, or a guest-owned full-screen clear.

## Level-transition tally

The level-transition owner is `func_8002DF9C` (`GS_LevelTransition = 1`). Its guest HUD/tally
`func_8002DA74` advances `g_LevelTransTicks` at `0x800756AC` and, once that counter passes `416`,
clears `g_LevelTransHudActive` at `0x800756B0` **and does nothing else**: the branch that retires a
gem sprite is inside the `<= 416` arm, so gems still in flight at expiry keep their live markers,
and the counter itself is only ever re-zeroed when the next transition begins (`func_8002C664` in
`gamestates/init.c`, and the type-6 surface in `special_surfaces.c`). Clearing that one flag is
therefore the screen's entire terminal transition, not a shortcut past part of it.

The screen is also not a loading screen: `func_8002DF9C` calls `LoadLevel(1)` while the load is
below stage 11 **or** the flag is clear, so a finished load is *held* by the animation. Cancelling
releases that hold and leaves every load phase to run.

`titles/spyro1/core/spyro1_transition_skip.cpp` owns the enhancement. On a Start or Cross edge while
the stage selector reads 1 and the flag is still set, it performs that same single write. It does
not touch `g_LevelTransTicks`, the gem array, the load stage, or the gamestate, and it does not
re-run the tally update, so no simulation is fast-forwarded and no I/O is bypassed. The former host
shortcut, which wrote the timer as well as the flag, stays removed.

`classify()` is pure and covered by `tests/test_transition_skip.cpp`, including the two negatives
that matter: Start during play is the pause button and Start in the pause menu is confirm, and
neither reads as a cancellation.

NOT YET LIVE-VERIFIED. `GS_LevelTransition` is only entered by portal traversal and by
`func_8002C664` (return home), and `func_8002C664` is reachable from the pause menu only in a
sub-level, not in a homeworld. Both routes therefore sit behind the portal blocker below, so
`tools/drive.py --skip-transitions` reaches gameplay without the state ever occurring: a driven run
with the flag on and one with it off both reach `GS_Playing` at frame 6361 and log no cancellation.
The mechanism is proven by unit test only until a portal can be traversed.

## The level flyby ("THE ADVENTURE BEGINS...")

The card the player sees on the way into a level is `GamestateCutsceneTransition`
(`gamestates/update.c`), reached from `GS_TitleScreen = 13` when `g_TitlescreenState.m_Mode` is
`TSM_Demo`; `TitlescreenUpdate` owns every other mode of that same gamestate, so the mode is part of
identifying the screen rather than a refinement of it. The card itself is the `TSS_Active` arm, which
flies Spyro along a five-segment scripted path while `m_Tick` counts to 384.

Its terminal transition depends on `m_DemoType`, and only one of the three is recovered:

- **`TSD_Level = 1`** is the card the user sees — `draw.c:func_8001E6B8` draws "THE ADVENTURE
  BEGINS..." on this path, or "THE ADVENTURE CONTINUES..." when `g_VisitedFlags[0]` is set. Its
  terminal is `func_8004AC24(1); LoadLevel(1); return;` and **nothing else**.
- `TSD_DemoLevel = 2` runs the same two calls but first writes `g_DemoMode` and `g_DemoFadeTimer`,
  neither of whose guest addresses is recovered here.
- `TSD_Cutscene = 0` ends somewhere else entirely: `ClearImage`, `AllocateBuffers(1)`, a load loop to
  stage 10, then `StartCutscenePlayback()` and `g_StateSwitch = 1`.

So only `TSD_Level` is cancellable, and the other two are deliberately absent rather than
approximated.

### The two guest addresses, recovered numerically

The decomp names `func_8004AC24` in its own symbol but gives `LoadLevel` no address. It is
**`0x80015370`**, established from the shipping `SCUS_942.28` three independent ways:

1. Of `func_8002DF9C`'s three non-tally `jal` targets it is the only one that both reads **and**
   writes `g_LoadStage` (`0x80075864`) — twenty accesses, three of them `sw`. `0x80037BD4` reads it
   once and never writes, so it is a consumer; `0x8004A7EC` never touches it.
2. Scanning the whole text for `jal 0x80015370` gives eight call sites, one of which is
   `0x8002DFE8`, inside `func_8002DF9C` (`0x8002DF9C..0x8002DFF8`) — the call this document's
   level-transition section already described in prose, arrived at from the opposite direction.
3. It appears in the terminal pair itself: `0x80033158` is `jal 0x8004AC24` and `0x80033160`, eight
   bytes later, is `jal 0x80015370`. Four of the eight `LoadLevel` sites sit within `0x400` bytes of
   both a reference to `m_Tick` (`0x80078D80`) and an immediate `384`, which locates
   `GamestateCutsceneTransition`'s body; only this one is preceded by the Spyro reset.

### The load gate is the guest's, and the press is held rather than obeyed early

The terminal has **two** conditions: `m_Tick >= 384`, which is presentation, and `g_LoadStage == 13`,
which is I/O. `spyro1_transition_skip.cpp` honours the second and cancels only the first. A Start or
Cross edge arriving while the level is still streaming is **held** — `flybyPressHeld_`, cleared the
moment the card is no longer up — and classified on a later frame once the load gate opens. Nothing
writes `m_Tick`: claim C179 already established that advancing a clock is not a valid skip route, and
this is the same move.

The cancellation itself is two scoped dispatches of the guest's own calls, `0x8004AC24(1)` then
`0x80015370(1)`, on the `func_8002C664` precedent — no global is hand-copied.

`tests/test_transition_skip.cpp` covers the load gate, the two unrecovered demo paths, and the
titlescreen menu (the same gamestate, where Start is the menu's own confirm). Both new discriminators
were checked by removing the thing they test and watching them fail, not by reading them.

## Transition screens still without a recovered cancellation

Each of these has a named natural terminal writer but no exercised route, so none is installed:

- `GS_EntranceAnimation = 9` (`func_8002E000` at **`0x8002E000`**): its terminal is one guest
  store, `0x8002E070: sw $zero, 0x57d8($at)` = `g_Gamestate = GS_Playing`, and nothing else. It is
  reached only through the camera gate, so a cancellation has to move the camera — but the reason
  this section used to give ("it would have to leave the camera mid-rotation, which is a state
  question, not a flag") is **wrong**: `g_Camera.m_Rotation.y` is a derived value rewritten every
  frame by `func_800342F8`, and the camera's owner is `CameraUpdate` `0x80037BD4`, which the
  GS_Playing arm calls at `0x80033B4C` exactly as stage 9 calls it at `0x8002E010`. Ending the
  screen does not strand the camera. The real reasons there is no arm are that the terminal cannot be
  dispatched (it sits `0x10` bytes before an epilogue whose `lw $ra` clobbers the dispatcher's return
  address) and that, in the resident text, there is no guest acceleration route for it (the main
  image's only two Start/Cross-mask sites are the pause menu's confirm and stage 14's own skip).
  **The WAD overlays have now been scanned too** — `docs/issues/0141`, claim C229, over 36 code
  entries and 501,760 words — and the answer there is the same: 44 `andi …,0x840` sites (22 of them
  on a pad word, all in the title overlay's menu or in a level's own card), 15 overlay sites storing
  stage 9's terminal value of which **none** is pad-reachable, 51 overlay writes to the camera
  rotation it is gated on of which **none** is pad-reachable, and 50 overlay reads of `g_Gamestate`
  whose consumers compare it against 7 or 8 and **never** 9. So "no guest acceleration route" is now
  a measurement over the whole overlay corpus rather than a scope limit. Recovered, measured and
  written up in `docs/issues/0138`, extended in `docs/issues/0141`.
- `GS_ExitLevel = 10` (`func_8002E084`, 42 instructions, no pad read): terminates through its own
  counter chain into `func_8002C664` (`0x8002C664`), which is a complete recovered route — a scoped
  original call to it is the shape a cancellation should take, and the port takes it. **Its one
  divergence is derived from the guest's own counter chain, not from a run** (the run that would show
  it is queued — see `docs/issues/0151`): the chain zeroes `D_8007568C` and increments `D_800758B8`
  twice before the call, so the natural route leaves `D_800758B8 == 2` and a cancellation fired at an
  arbitrary point of the chain leaves it at 0 or 1. `D_800758B8` is the pause menu's frame counter
  (read by `func_8001A40C`, the gamestate 2/3 draw) and the gamestate 10 draw's case selector
  (`func_8001C694`); nothing else writes it during the glide. See `docs/issues/0151`.
- `GS_Dragon = 8`: blocked earlier than input; it has no native producer at all (issue 0103). Its own
  update does read the pad — `0x80030CD4 andi $v0, $v0, 0x40` on `g_Pad.m_Down`, i.e. **Cross** — but
  that advances the cutscene's own dialogue script, it is not a skip.
- `GS_GameOver = 5`: **retail skips this one itself, on held Start**, and no document here said so
  until `docs/issues/0151`. `func_8002EDF0` at `0x8002F338` tests `g_Pad.m_Held & 0x800` once
  `g_GameOverTicks >= 0x169` and `g_LoadStage >= 0xB`, then calls `0x8002F344 jal func_8003FDC8`, sets
  `D_80075940 = 2` and zeroes the tick counter. Guest-owned, so no native arm; the port's obligation
  is only that the held button arrives.
- `GS_Fairy = 11`: reads `g_Pad.m_Down` at four sites — `0x4000` (Down) once and `0x40` (Cross)
  three times — all of them the fairy's own script and menu navigation.
- `GS_Balloonist = 12`: **reads no pad word at all** — 0 of `func_800324D8`'s 338 instructions form
  `g_Pad`, and its terminal is `.L800329CC`'s `jalr $v0` through the level-supplied pointer
  `D_8007574C`, which is not an address in the resident image. No route, and no way to invent one.
- `GS_Cutscene = 14`: CORRECTED 2026-09-14 — this state is NOT without a cancellation. The intro
  cutscene (`g_CutsceneIdx == 1`, "In the World of Dragons") is skipped BY THE GUEST while Start or
  Cross is HELD: `gamestates/update.c:GamestateCutsceneUpdate` shortens the layout's duration once
  `m_CurrentTick >= 241` and before the final 32 ticks (`m_Duration = (tick >> 1) + 16`), so the
  cutscene still reaches its own terminal condition and calls the guest's `EndCutscenePlayback()`.
  That is the same accelerate-toward-natural-terminal shape used by recorded playback, and it is a
  complete route, so no native skip may be added here: a second mechanism would duplicate a live
  guest one. What is missing is measurement, not implementation (below).

## Measured boot timeline, and the harness gap that hid this

Sampled every 30 fields from boot (screenshots in `scratch/screenshots/boot/`, game evidence only):

- fields 46/226/406 — the two boot logos, gamestate 0 (`SONY COMPUTER ENTERTAINMENT AMERICA`,
  then `UNIVERSAL INTERACTIVE`);
- fields 586-946 — the Insomniac-logo panorama over the Artisans homeworld, gamestate 13;
- field 1666 onward — the title screen (`SPYRO THE DRAGON`, `PRESS START`), gamestate 13.

`gamestate == 14` (GS_Cutscene) was NOT observed anywhere in that window, so on this route the intro
cutscene state is not reached before the title screen — which means "which screen does the user mean
by IN THE WORLD OF DRAGONS" is still an open question, not an assumption to build on.

The reason this went unmeasured is a harness property, now established by measurement rather than
assumption: `tools/drive.py` applies `--hold`, `--tap` and `--after` only AFTER arrival in GS_Playing,
so no driven run can press anything during boot, the logos, or the intro. A REPL `press` issued
directly from boot DOES reach the guest before arrival — holding Start from boot changed the run
(a no-input run aborts in the attract demo, issue 0113, while the same run with Start held survives
the full 7800 fields), and the boot-skip routes already shipped rely on that same early input.

Any future skip verification for a pre-gameplay state therefore needs a pre-arrival input path; the
drive's arrival-gated route cannot express it, and that is a harness gap rather than a title defect.

**CLOSED 2026-09-28 for everything up to `GS_Playing`.** `tools/press_conditions.py` and
`drive.py --press-while GAMESTATE:BUTTON[:FRAMES]`, gated by `press_conditions_selftest`. It is
condition-driven, not a frame count, because the boot/attract sequence is timing dependent and field
400 is a different screen on every run. One edge per spec, never a second: gamestate 0 is both the
boot logo and the arrival state, and the first version of the rule (one edge per contiguous run) fired
a second Start at the `GS_Playing` hand-off in a live run and the guest opened `GS_PauseMenu`.

**CLOSED 2026-09-30 for everything AFTER it, which is the mirror image of the same gap.**
`--press-while` is applied only from `Navigator._advance()`, and `reach_gameplay()` returns before it
can run again — so the three screens that exist *between two levels* (the tally, the entrance sweep,
the return-home glide) could not be pressed by any driver at all, which is why all three arms had unit
tests and no observation. `PressConditions` was renamed to `press_conditions.py` and now owns two
phases: `PressConditions` (pre-arrival, unchanged) and `PostArrivalPressConditions`
(`--press-after GAMESTATE:BUTTON[:FRAMES]`), applied from `Port.run()`'s own sampler so it is live
while the run walks a level. The post-arrival set refuses `GS_Playing`, `GS_PauseMenu` and
`GS_InventoryMenu` **by name at parse time**, because the pre-arrival set's guarantee was structural
and this one is not. Measured legs, same disc and settings, one product
instance at a time:

| run | presses | arrival | port's own log | census |
|---|---|---|---|---|
| no press | 0 | field 6380 | `Start/Cross ends …` ×0 | 651 samples; never reached `level_transition`, `dragon`, `entrance_animation`, `credits` |
| `--press-while 0:start` | 1 at field 20 | field **6180** | `Start/Cross ends Spyro 1's first presentation hold` ×1 | 631 samples; `since GS_Playing: playing=13` |
| `--press-while 9:start --press-while 1:start` | 0 | field 6360 | — | 649 samples; both conditions `fired 0 time(s) in 0 sample(s) -- scanned, never matched`, and the census independently names both states unreached |

PARTLY CLOSED 2026-09-19. `--skip-transitions` now also presses Start on the flyby card during the
approach, alongside the level-transition tally, so the navigator does issue pre-arrival edges on the
two screens it can positively identify. The general gap remains: `--hold`, `--tap` and `--after` are
still arrival-gated, so boot, the logos and the intro still need a REPL `press` issued directly.

The portal traversal remains a separate blocker: the type-6 collision surface writes the transition
globals, and a visible portal still reaches the unowned `0x80050BD0` mask/near-family/painter path.
No Start shortcut writes those transition globals or substitutes for that renderer.

## Still unclassified

Stage mode 14 is now classified: it is recorded/demo playback, and Start handling is already guest
owned. `0x800331AC` advances the playback cursor each frame. When state `[0x8007566C] == 1`, it tests
held input `[0x80077380] & 0x840` (Start or Cross). Once the cursor is at least 241 and before the
last 32 samples, that input rewrites the cursor to `cursor / 2 + 16`, accelerating toward the same
natural terminal condition. It does not jump state. When `cursor >= sample_count * 2`, the function
calls the natural completion writer `0x8002D440`; that body performs the cleanup and writes stage
mode 13 plus the appropriate stage-state handoff. A native Start transition here would duplicate and
potentially conflict with a mechanism the game already has, so none is installed.

Live replay `scratch/logs/skipmap-replay-play.log` reaches mode 14 at field 1912 and leaves it through
the normal path at field 2183 while receiving Start edges. It then traverses another phase-driven
load and reaches gameplay modes 0/2. This also demonstrates why a global Start-to-next-state rule is
invalid: the same replay continues generating Start edges in modes 0/2, where Start is gameplay UI.

The observed loading surface is stage 13/sub 3 with `[0x80075864]` progressing through 0/1/3/4/5/6/7
and later 8/9/10/11/12/13. Those are actual streaming/load phases (`0x80032B08` / `0x80014564`), not
a presentation timer: the run cannot bypass them without skipping required I/O. No independently
owned "loading overlay finished displaying" transition has yet been observed, so no loading skip is
installed. The next safe target requires a run that distinguishes load completion from a subsequent
presentation-only hold and traces that hold's natural writer.
