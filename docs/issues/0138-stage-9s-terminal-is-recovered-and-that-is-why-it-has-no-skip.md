---
id: 138
title: GS_EntranceAnimation's terminal is recovered, and that is why it gets no Start-to-skip arm
status: open
symptom: `TransitionSkip` has three arms and the fourth screen the skip map names, `GS_EntranceAnimation` (stage 9), is absent. The map's stated reason is that its terminal "would have to leave the camera mid-rotation, which is a state question, not a flag". That reason was never checked, and checking it changes both the reason and the shape of the answer.
state_items: S011
tags: transition,skip,input,re,frontend
created: 2026-09-28
---

## Asked

Operator, 2026-09-28: "why don't you wire in the 'Start to Skip's?"

Three of the four named screens are wired. This records the fourth, measured from the admitted image,
and why no arm was added — including the part of the stated reason that measurement **refutes**.

## What stage 9 is

`GS_EntranceAnimation = 9`, the guest's "level entrance camera sweep". Its update is `func_8002E000`
at **`0x8002E000`**, and its whole body is 20 instructions:

```
0x8002E000  addiu $sp, $sp, -0x18
0x8002E004  sw    $ra, 0x10($sp)
0x8002E008  jal   0x8004a200          ; func_8004A200, Spyro's update
0x8002E010  jal   0x80037bd4          ; CameraUpdate
0x8002E018  lui   $a0, 0x8007
0x8002E01C  addiu $a0, $a0, 0x6e1e    ; 0x80076E1E  g_Camera.m_Rotation.y
0x8002E020  lhu   $v0, ($a0)
0x8002E028  andi  $v1, $v0, 0xfff
0x8002E02C  slti  $v0, $v1, 0x801
0x8002E030  bnez  $v0, 0x8002e040
0x8002E034  sh    $v1, ($a0)          ; delay slot
0x8002E038  addiu $v0, $v1, -0x1000
0x8002E03C  sh    $v0, ($a0)          ; normalise 0x801..0xFFF to negative
0x8002E040  lh    $v0, ($a0)          ; signed read back
0x8002E048  slti  $v0, $v0, -0x200
0x8002E04C  bnez  $v0, 0x8002e06c
0x8002E054  lui   $v1, 0x8007
0x8002E058  lw    $v1, 0x6ea8($v1)    ; 0x80076EA8  g_Camera.m_SphericalPreset
0x8002E05C  lui   $v0, 0x8007
0x8002E060  addiu $v0, $v0, -0x357c   ; 0x8006CA84  D_8006CA84
0x8002E064  bne   $v1, $v0, 0x8002e074
0x8002E06c  lui   $at, 0x8007
0x8002E070  sw    $zero, 0x57d8($at)  ; g_Gamestate = 0 = GS_Playing      <-- the terminal
0x8002E074  lw    $ra, 0x10($sp)
0x8002E078  addiu $sp, $sp, 0x18
0x8002E07c  jr    $ra
```

`SCUS_942.28`, `scratch/assets/spyro1/SCUS_942.28`, 417,792 B. Text mapped from the PS-X EXE header
itself — `pc0 = 0x8005B8E0`, `t_addr = 0x80010000`, `t_size = 0x65800`, text at file offset `0x800` —
and cross-checked by `tools/probe_guest_disasm.py --verify-only` against the decompiled listing, whose
`--selftest` also separates little-endian from big-endian before it is trusted.

## The recovery, by more than one argument

1. **The store's address operand names the global the port already owns.** `0x8002E070`'s base is
   built by `lui $at, 0x8007` at `0x8002E06C` and its displacement is `0x57d8`, so the target is
   `0x800757D8` — which `game/core/guest_globals.h:23` independently names `kGamestate`, and which
   `tools/writers.py 0x800757D8` reports 24 immediate-form stores to.
2. **The value is `$zero`, and 0 is the play state.** The same `tools/writers.py` scan resolves each
   store's stored constant by backward propagation: `0x8002E070` is one of 23 resolvable sites and
   stores 0. The gamestate enum's 0 is `GS_Playing`, and the dispatch arm for the *play* state calls
   the same two routines stage 9 calls — `jal 0x8004A200` at `0x80033AD8` and `jal 0x80037BD4` at
   `0x80033B4C` — which is what a hand-off to play looks like and is not what a hand-off to any other
   state looks like.
3. **It is the last store before the epilogue, and the only one in the function.** No other global is
   written between the gate and the return, so the terminal transition *is* this store. That is the
   same argument the level-transition tally's arm rests on
   (`docs/findings/start-skip-map.md`, "Clearing that one flag is therefore the screen's entire
   terminal transition").
4. **It is reached only through the camera gate.** Both paths into `0x8002E06C` are the
   rotation test and the preset compare; nothing else branches there.
5. **`func_8002E000` has exactly one caller**, `jal 0x8002E000` at `0x8003395C`
   (`tools/xrefs.py 0x8002E000`), which is the gamestate dispatch switch. So there is no second route
   into this screen's update that the decode above missed.

## Why there is still no arm

Three measured facts, each of which independently removes a candidate.

### 1. The terminal cannot be DISPATCHED

The store is `0x10` bytes before the function's epilogue, and the epilogue's first instruction is
`lw $ra, 0x10($sp)`. The framework detects a dispatched guest call by *return address*: see
`psxport/runtime/cpu/native_dispatch.cpp` — `dispatchGuest` passes `core.r[31]` to
`executeFunction`, and `lightrec_executor.cpp`'s `blockBoundary` ends the call when
`guestPc == *boundary.returnAddress`. Entering at `0x8002E06C` therefore runs the guest's own store
and then overwrites the return address with a word off the current stack, and `jr $ra` at
`0x8002E07C` leaves through it. A mid-function entry is not expressible with
`dispatchGuestToReturn0`, and that is read off the mechanism, not assumed.

The only well-formed guest unit in stage 9 is `func_8002E000` itself. Dispatching it re-runs
`func_8004A200` and `CameraUpdate` once and re-tests the same camera gate, so it either does nothing
or performs the terminal — it cannot make the terminal happen early. An arm built on it would count
cancellations that did not cancel, which is the failure this whole area keeps paying for.

### 2. The gate is the camera, and the two ways to move it are both forbidden

`g_Camera.m_Rotation.y` (`0x80076E1E`) and `g_Camera.m_SphericalPreset` (`0x80076EA8`) are the only
two inputs. Making either true early is a camera write or a fast-forward. The rules that forbid it are
the header's own ("performs exactly the terminal write the screen's own guest owner performs … leaves
every other global as the natural route leaves it") and claim C179, which is about advancing a clock —
and a camera sweep is a clock in everything but name.

### 3. The guest has no acceleration route *in the resident text*, unlike stage 14

Stage 14 is skipped **by the guest** while Start or Cross is *held*, which is why no native arm exists
there and the map says so. Stage 9's own update has no such path, and the first half of that is
complete: `func_8002E000` is 20 instructions with two `jal`s and reads no pad word at all. The only
route from this screen into `GS_Playing` is the store at `0x8002E070`, and it is reached only from
the camera gate, so the guest cannot end the screen earlier by any route but the camera.

The second half is a count, and it is scoped to the resident image. The whole main text is 103,936
aligned words; `andi rt, rs, 0x840` — the Start/Cross mask this map establishes for the title — occurs
at exactly **two** sites:

| site | what it is |
|---|---|
| `0x8002E988` | `lw $v0, 0x7378($v0)` = `[0x80077378]`, the newly-pressed word, inside the pause-menu update, where Start is confirm |
| `0x80033354` | the cutscene update's own held-Start acceleration, i.e. the stage-14 skip |

**This is not the same claim as "there is no Start test in the game", and the difference matters.** The
title/attract Start handling this map quotes — `0x8007AC48`, `0x8007B88C` — is at an address above
`0x80075800`, so it is not in the resident text at all: it is in the overlay `OV_5B800`, which loads
at `0x8007AA38`. The WAD overlays are not provisioned in this worktree (only `SCUS_942.28` and
`SYSTEM.CNF` are), `tools/provision_title.py` extracts the boot executable and nothing else, and there
is no maintained member extractor, so **the overlays were not scanned**. See "Not determined".

## WHAT THE MEASUREMENT REFUTES — the map's reason is wrong, and the real reason is sharper

`docs/findings/start-skip-map.md` said: *"A cancellation would have to leave the camera mid-rotation,
which is a state question, not a flag."* **The camera is not left mid-rotation, because gamestate 9
does not own the camera and ending it does not stop the camera.** Measured:

- `g_Camera.m_Rotation.y` is a **derived** value, rewritten every frame by `func_800342F8`
  (`0x800342F8`: `addu` of the simulation words, `andi $v0, $v0, 0xfff`, `sh $v0, 0x6e1e($at)`), which
  `func_80037A20` calls at `0x80037B48`. It is not accumulated and not frozen.
- The camera's owner is `CameraUpdate` at `0x80037BD4`, and the **GS_Playing arm calls the same
  routine** — `jal 0x80037BD4` at `0x80033B4C`. Stage 9's own call is at `0x8002E010`. The camera
  update is therefore continuous across the terminal.
- What selects the camera's sub-update is `g_Camera.m_State` (`0x80076E28`), compared inside
  `CameraUpdate` at `0x80037CA4`, and **stage 9 never writes it** — the only stores to it in the
  function's reach are `0x80037784` (`= 0x8000000E`, from `func_80037714`, a *set-up* routine) and
  `0x80035F10`.

So ending stage 9 early does not strand the camera: the same owner keeps converging it on the same
trajectory, and the rotation at the moment of the switch is a **timing** difference, not a state
corruption. That is why the map's reason does not hold, and it is a real correction — but it does not
produce an arm, because the arm would then be a single native `g_Gamestate` write, which the brief for
this work and the header's rule both exclude, and because the decision to accept a timing difference
in when the player regains control is a product call, not an RE call.

**If the operator wants that arm**, it is one line in `TransitionSkip::observe` — `mem_w32(kGamestate,
0)` on a Start edge in stage 9 — and the measurement above is what justifies it. It is deliberately
not written here.

## Reachability, measured

`g_Gamestate = 9` is written from **exactly one** instruction in the resident text.
`tools/writers.py 0x800757D8 --value 9` reports one site, `0x800162C8`, and a full scan of all 24
immediate-form stores (`0x80057208` is the one register-valued store; `addiu $s1, $zero, 1` at
`0x80057148` dominates it, so it is a `= 1`). `0x800162C8` sits in `LoadLevel`'s level-transition
entrance block, two instructions after `sw $v0(=10), 0x80088AD4`.

So stage 9 needs a level transition — a portal exit or a flight level — which is behind the same
portal blocker that already leaves `GS_LevelTransition` unexercised. This scan cannot see a store
through a fully computed pointer, and it does not cover overlay images, which reuse these addresses;
both limits are stated by the tool.

## The harness gap, closed

`docs/findings/start-skip-map.md` recorded that `tools/drive.py` applies `--hold`, `--tap` and
`--after` only AFTER arrival, so no driven run could press anything during boot, the logos or the
intro. That is now `tools/pre_arrival_press.py` and `drive.py --press-while GAMESTATE:BUTTON[:FRAMES]`,
gated by `pre_arrival_press_selftest`. It is condition-driven rather than a frame count, because the
boot/attract sequence is timing dependent — field 400 is a different screen on every run.

The rule is **one edge per condition, ever**, and that was measured rather than chosen. The first
version was "one edge per contiguous run of samples in the named state". A live run with
`--press-while 0:start` then fired twice — at field 20 on the boot logo, and again at field 6180, the
moment the guest became `GS_Playing` — and the guest opened `GS_PauseMenu`. The run's own census said
`since GS_Playing: playing=13, 2=13`. That is the hazard the map already named for synthetic pulses,
reproduced by a driver instead of by a player.

### Live evidence

Two runs, same settings file, same disc, one product instance at a time.

| run | presses | arrival | port's own log | census |
|---|---|---|---|---|
| baseline, no press | 0 | `GS_Playing` at field **6380** | `Start/Cross ends …` ×0 | 651 samples; `never reached: level_transition, dragon, entrance_animation, credits` |
| `--press-while 0:start` | 1, at field **20** | `GS_Playing` at field **6180** | `Start/Cross ends Spyro 1's first presentation hold` ×1 | 631 samples; `since GS_Playing: playing=13` |
| `--press-while 9:start --press-while 1:start` | 0 | `GS_Playing` at field 6360 | — | 649 samples; both conditions report `fired 0 time(s) in 0 sample(s) -- scanned, never matched`, and the census independently names both states unreached |

200 fields earlier, with the port's own boot owner reporting the hold it ended, and no pause menu.
The third run is the two-instrument negative: the press report and the census agree that stage 9 and
stage 1 never occur on the drivable route, each with a sample count. Both result frames were captured
and looked at (684×240, 93.3% non-black, ~3,186 distinct colours — real frames, not empty buffers).

## Bugs found and NOT fixed

- **`tools/writers.py` and `tools/xrefs.py` pointed at `scratch/bin/spyro/`, a layout this repository
  does not produce.** Both died with `FileNotFoundError` before scanning a word, which reads exactly
  like "nothing writes this". Fixed here: the defaults now name the provisioned
  `scratch/assets/spyro1/`, both take `--exe`/`--ovdir`/`--img`, and a missing or non-PS-X-EXE image
  is a refusal with exit 2 instead of a traceback. `writers.py` also now reports how many images it
  scanned, because a corpus that silently scanned only `MAIN` is a different and weaker answer.
  **Not** fixed: the same stale path remains in `tools/depth_cov.py`, `tools/field_layers.py` and
  `tools/shot.py`, which are run-artifact tools and were not on this path.
- `writers.py`'s docstring advertised an `--img` flag the code never had.
- The pre-arrival press rule shipped wrong on its first live run and was corrected here; recorded
  because the first rule was plausible and the failure was only visible by running the product.

## Not determined

- **Whether anything in a WAD overlay accelerates the entrance sweep.** The overlay images were not
  scanned, and they are where this title's other Start handling lives (`OV_5B800` at `0x8007AA38`
  holds the title/attract Start tests the skip map quotes). Establishing it needs an overlay
  extractor, which this repository does not have — `tools/provision_title.py` extracts the boot
  executable only, and `tools/wad_index.py` enumerates and scores members without writing them out.
  This is the one place a guest-owned skip for stage 9 could still be hiding, and it is the
  unblocking action if the operator wants the question closed rather than bounded.
- Whether `g_Camera.m_SphericalPreset == &D_8006CA84` is reachable at all. No code in the resident
  image materialises `0x8006CA84` except the two sites that compare it (`0x800161A8`, `0x8002E05C`),
  and no big-endian word equal to it appears in the image. The pointer can also arrive from level
  data in a WAD image, which was not searched, so the arm of the gate that is not the rotation is
  **unmeasured**, not dead.
- How long stage 9 actually lasts, and what `g_Camera.m_Rotation.y` is on entry. It is a portal-exit
  screen and no driven run reaches one.
- Whether the guest's `sw $zero` idiom at `0x8002E070` has a cheaper address form that would make the
  terminal dispatchable. It does not: the store is a full `lui`/`sw` pair with no short form, and the
  only addressable units are the function entry and the two calls it makes.
