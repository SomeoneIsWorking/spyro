# Start-to-skip map

This is the evidence boundary for making logos, loading screens, and scripted sequences skippable.
It deliberately does not equate "Start is down" with "jump to the next state": Start is also the
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
- the legitimate transition writes stage sub-state 2 and sub-sub-state 5 at
  `0x8007B8F0..0x8007B8F8`; it then reaches sub-state 3 through the guest's memory-card completion
  chain. That chain is now functional (issue 0027 resolution), so no PC state poke is justified.

Repeated synthetic Start pulses are not a shipping skip mechanism: once the sequence hands off to
gameplay, another pulse opens the pause screen. A shipping implementation must consume one host edge
inside a positively identified skippable state and release it before the next state reads input.

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

- `GS_EntranceAnimation = 9` (`func_8002E000`): terminates by writing `g_Gamestate = GS_Playing`
  once the camera y-rotation drops below `-0x200` or the spherical preset reaches `D_8006CA84`. A
  cancellation would have to leave the camera mid-rotation, which is a state question, not a flag.
- `GS_ExitLevel = 10` (`func_8002E084`): terminates through its own counter chain into
  `func_8002C664` (`0x8002C664`), which is a complete recovered route — a scoped original call to it
  is the shape a cancellation should take, once the state can be reached.
- `GS_Dragon = 8`: blocked earlier than input; it has no native producer at all (issue 0103).
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
