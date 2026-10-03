# 0137 — the product is NOT one load step behind: the barrier sampled INSIDE one `LoadLevel` call, and no override is correct here

`docs/project-state.md` records the `demo_level_load` divergence and, in the same paragraph, the
reading this issue was opened to test:

> The open question, named rather than guessed … `docs/issues/0135` refuted the framework suspect.
> **So this is a real one-step difference in the loader, not a sampling artifact of the barrier.**

**That is refuted too, by measurement on both cores.** The product's loader is not behind. At the
barrier the two cores are on the **same guest instruction with the same register values**, and the
`1` against `2` is which side of one `sw` each core's field boundary landed on. The loader walks
`1 -> 2 -> 3` inside a single call on both cores.

There is consequently **no native override to write**, and writing one would be a tap: it would
reimplement ~600 instructions of CD-paced loading, risk the level load that currently works
(`demo_playing` and `gameplay[0]` match on all 13 decisive ranges), and make a number stop
differing without changing what the loader does. The fix belongs to the ROUTE's decisive set, and
this issue names it exactly. That change is not mine to make — see "What I did not do".

## 1. The load state machine, read out of the image

`g_LoadStage` is `0x80075864` (`game/core/guest_globals.h:38`), so `%hi` = `0x8007` and
`%lo` = **+0x5864**. It is positive: `0x80075864 - 0x80070000`. Two wrong guesses were available and
both produce a *scanner that finds nothing* — `0x8584` addresses `0x80078584`, and reading `LUI`'s
register out of bits 25..21 reads the field MIPS requires to be zero (a `LUI` names its register in
bits 20..16, a `lw`/`sw` names its base in bits 25..21). `tools/probe_load_step.py --selftest` fails
if any of these stops being true of `SCUS_942.28`, so none of the rest of this file can go stale
silently.

`LoadLevel` is **`0x80015370`**. `uv run --frozen python tools/probe_guest_disasm.py --address …`:

```
0x80015370  0780023C  lui   $v0, 0x8007
0x80015374  6458428C  lw    $v0, 0x5864($v0)   ; g_LoadStage, read before the frame is set up
0x80015398  02004228  slti  $v0, $v0, 2         ; g_LoadStage < 2 ?
0x8001539C  14004014  bnez  $v0, 0x800153F0     ; so stage 1 SKIPS the CD gate entirely
0x800153A4  F958000C  jal   0x800163E4          ; CDLoadTime()
0x800153AC  0780023C  lui   $v0, 0x8007
0x800153B0  B86B4224  addiu $v0, $v0, 0x6BB8   ; = 0x80076BB8 = g_CdState + 0x28
0x800153B4  0000428C  lw    $v0, ($v0)         ;   the CD gate's first test
0x800153BC  FF034014  bnez  $v0, 0x800163BC     ;   -> return, stage UNCHANGED
0x800153C4  F68E010C  jal   0x80063BD8          ; CdSync(1, 0)
0x800153CC  02000324  addiu $v1, $zero, 2      ; CdlComplete
0x800153D0  FA034314  bne   $v0, $v1, 0x800163BC;   -> return, stage UNCHANGED
0x800153D8  0780023C  lui   $v0, 0x8007
0x800153DC  B474428C  lw    $v0, 0x74B4($v0)   ; = 0x800774B4 = g_CdMusic + 0x04
0x800153E4  40004230  andi  $v0, $v0, 0x40
0x800153E8  F4034010  beqz  $v0, 0x800163BC     ;   -> return, stage UNCHANGED
0x800153F4  6458638C  lw    $v1, 0x5864($v1)   ; g_LoadStage, re-read for the dispatch
0x800153FC  0E00622C  sltiu $v0, $v1, 0xE      ; cases 0..13
0x80015410  880A228C  lw    $v0, 0xA88($at)    ; jump table at 0x80010A88
0x80015418  08004000  jr    $v0
```

The dispatch table's 14 entries are stored in the image's **instruction** byte order, so each must be
read little-endian: index 1 -> `0x800155B4`, index 2 -> `0x800155E4`. (Read big-endian, case 1 comes
out as `0xB4550180`, which is not code.)

**Case 1 and case 2 are one straight line, and that is the whole finding:**

```
0x800155B4  CA5A010C  jal   0x80056B28          ; case 1: KillSoundsAndMusic(0)
0x800155C0  6458428C  lw    $v0, 0x5864($v0)   ; g_LoadStage
0x800155CC  408A20AC  sw    $zero, -0x75C0($at); g_Cyclorama.m_SectorCount = 0
0x800155D4  246E23AC  sw    $v1, 0x6E24($at)  ; g_Camera.m_OcclusionGroup = -1
0x800155D8  01004224  addiu $v0, $v0, 1
0x800155E0  645822AC  sw    $v0, 0x5864($at)  ; g_LoadStage = 2      <-- THE FIRST WRITE
0x800155E4  0780023C  lui   $v0, 0x8007       ; case 2 starts here. NOT a branch.
0x800155F4  1C69010C  jal   0x8005A470        ; SetOverlayPointers()
0x80015664  0F000012  beqz  $s0, 0x800156A4   ; s0 = pArg
0x8001569C  A659000C  jal   0x80016698        ; CDLoadAsync(overlay)
0x800156A8  6458428C  lw    $v0, 0x5864($v0) ; g_LoadStage
0x800156AC  ED580008  j     0x800163B4       ; -> the shared tail, increment in the delay slot
0x800156B0  01004224  addiu $v0, $v0, 1
0x800163B4  0780013C  lui   $at, 0x8007
0x800163B8  645822AC  sw    $v0, 0x5864($at); g_LoadStage = 3      <-- THE SECOND WRITE
0x800163BC  7800BF8F  lw    $ra, 0x78($sp)  ; the "blocked" exit: SKIPS the store
```

Two addresses in the CD gate are named only as far as they can be named. `g_CdState` is at
`0x80076B90` and `g_CdMusic` at `0x800774B0` (their own `.space` comments: "Total size from
80076B90 to 80076BC0" and "from 800774B0 to 800776D8"), so the two reads above are
`g_CdState + 0x28` and `g_CdMusic + 0x04`. The decomp calls those `m_IsReading` and `m_Flags`, but
**that attribution is not confirmed**: `include/cd.h`'s `CdState` puts `m_IsReading` at `0x14 +
sizeof(CdlFILE)`, and PsyQ's `CdlFILE` is 69 bytes, which would put it outside `g_CdState`'s 0x30
altogether — so the header's offsets do not fit the `.space` and the field names are unverified. The
gate's *behaviour* does not depend on the names: three tests, each returning without touching the
stage, and the stage-1 path skipping all three. What the three tests are, exactly, is not
established here.

Three byte facts, each checked separately by `--selftest`, and together they are the explanation:
1. `0x800155E4` is `lui`, **not a control transfer** — case 1 falls straight through into case 2.
2. Case 2's body `[0x800155E4, 0x800156B0)` contains **no store to `g_LoadStage` at all**, so 2 is not
   disturbed between the two writes.
3. Both writes store `g_LoadStage + 1` (`lw` then `addiu $v0,$v0,1` then `sw`), so one call entered at
   stage 1 writes **2 at `0x800155E0`** and then **3 at `0x800163B8`**, and returns.

**Therefore `g_LoadStage == 2` is not a resting value. It exists only INSIDE one `LoadLevel` call**,
in a window thousands of instructions wide (it contains `SetOverlayPointers` and a `CDLoadAsync`).
A reading of 1 is the same window before `0x800155E0`; a reading of 2 is the same window after it.

The caller is the title overlay's flyby (`external/spyro-1/src/gamestates/update.c:614`,
`GamestateCutsceneTransition`), which for `TSD_DemoLevel` writes stage 1 in `TSS_Setup` and then runs

```c
} else if (g_TitlescreenState.m_State == TSS_Loading) {
    while (g_LoadStage < 6) { LoadLevel(1); CDMusicUpdate(); }
    g_TitlescreenState.m_State = TSS_Active;
```

— a **blocking spin loop inside one main-loop iteration**, and `m_State` is only set to
`TSS_Active` *after* it. So `title.mode=3, title.state=1` is precisely the declared state of "inside
that loop", and it is the state both cores are in. That is also why the route's `load_stage` reading
is a per-field sample of a value that is mid-transition by construction.

## 2. The measurement: both cores, same instruction, same registers

`tools/probe_load_step.py` (new, `--selftest` 20 checks: 6 read out of the image, 14 on the
classifier) drives both cores with the ROUTE's own `advance` and the ROUTE's own
`_demo_owns_title` predicate, so its samples are the comparator's samples. It arms the reference's
read-only PC observer on `LoadLevel`'s entry, on `0x800155E0`, on the shared tail `0x800163B8`, and
on **`0x80015420` as a negative control** — case 0, which this route provably cannot enter because
`TSS_Setup` writes stage 1 for `TSD_DemoLevel`. It arms the product's own translated-store observer
(`PSXPORT_STORE_OBSERVE`) on the two store PCs.

```
[loadstep] native  PARKED at advance step 3549 (field 3549): load_stage=1 gamestate=13 title=(3,1,5) tick=1009 level=11 level_ticks=3550
[loadstep] console PARKED at advance step 4766 (field 4766): load_stage=2 gamestate=13 title=(3,1,5) tick=1009 level=11 level_ticks=3654
[loadstep] declared state at the barrier -- equal: ['gamestate','title_mode','title_state','title_sub_state','title_tick','title_sub_tick','game_tick','level_id']; UNEQUAL: none
```

**The reference, at its park, is standing ON the instruction that writes 2:**

```
[loadstep] observer drain at the console's own park: scanned=1339899003 matched=2 retained=2 dropped=0 pairing_errors=0 incomplete
[loadstep]   target 0x80015370: entries=1     <- LoadLevel was entered once
[loadstep]   target 0x800155E0: entries=1     <- case 1's store of 2 was reached once
[loadstep]   target 0x800163B8: entries=0     <- the tail that would overwrite 2 with 3 has NOT run
[loadstep]   target 0x80015420: entries=0     <- NEGATIVE CONTROL: case 0 never entered
[loadstep]   field=4766 pc=0x800155E0 insn=0xAC225864 load_stage=1 title=(3,0) v0=0x00000002 t0=0x0000007F t1=0x8007623C t9=0x00002000 ra=0x800155BC
```

The observer is `before-opcode-after-fetch-and-cycle-bookkeeping`, so its RAM range still reads
`load_stage=1` while `$v0` already holds `2` — the store is one instruction from retiring. The
comparator's read happens after the field step returns, i.e. after it retired, and reads `2`. **The
only instruction that can turn that 1 into that 2 is `0x800155E0`, and the reference is observed on
it.** That is a proof, not an inference.

**The product executed the same store, with the same registers:**

```
guest_pc=0x800155E0 phase=before cycle=1120 a0=0x00000000 t0=0x0000007F t1=0x8007623C gpr[29]=0x801FFF20 gpr[31]=0x800155BC seen=1
guest_pc=0x800155E0 phase=after  cycle=1122 a0=0x00000000 t0=0x0000007F t1=0x8007623C gpr[29]=0x801FFF20 gpr[31]=0x800155BC seen=2
```

`ra=0x800155BC`, `t0=0x7F`, `t1=0x8007623C` — identical to the reference's record. Both cores run
byte-identical code and produce the identical `1 -> 2 -> 3` transition. `0x800163B8` fired **20**
times on the product, with the same ten `ra` values twice, i.e. two complete `0 -> 13` climbs.

**The per-field series is the shape of it** (`f` = fields after each core's own park):

```
   f  core     stage gstate mode state sub tick  stk lticks level
   0  native       1     13    3     1   5 1009    0   3550    11
   0  console      2     13    3     1   5 1009    0   3654    11
   1  native       6     13    3     2   5    1    0   3551    11
   1  console      2     13    3     1   5 1009    0   3655    11
   4  console      3     13    3     1   5 1009    0   3658    11
```

The product's field boundary lands **before** `0x800155E0`; the reference's lands **on** it, and
then sits at stage 2 for four more field boundaries because its `CDLoadAsync` overlay read has not
completed and the guard at `0x800153BC`/`0x800153D0`/`0x800153E8` keeps returning. Both cores climb
`0 -> 13`, reach level 11, and the next checkpoint matches on all 13 decisive ranges.

**This is why `docs/project-state.md`'s framing needs inverting: the product is not behind. The
console is the core that got a sample from later inside the call.**

### Controls, and the instrument shown able to report the other answer

* **Negative, console side, with a denominator:** `0x80015420` reports `entries=0` against
  `scanned=1339899003`. The observer is live (two other targets fired in the same drain), so this is
  a measured absence.
* **Positive, product side:** 4 and 40 callback lines on the two store PCs, named per PC, with the
  registers above.
* **A surface that CANNOT report a miss is labelled as such.** `LOADLEVEL_ENTRY` and `CASE0_ENTRY`
  are `jal`, not stores, so `PSXPORT_STORE_OBSERVE` returns 0 lines for them **by shape**; the tool
  prints that as "NOT a measurement" rather than as a zero. This is the `0x800700F4` mistake from
  `docs/issues/0133` and `psxport/docs/findings/diagnostics-that-cannot-lie.md`, and it is worth
  naming because the same command line contains both kinds of target.
* **The classifier can return the other verdict.** `--selftest` requires stage 1 against 3, and
  stage 2 at a different `m_State`, both to come out `loader-difference`. A classifier that can only
  ever say "same iteration" would pass this run and be worthless.
* **The route's own predicate gated the verdict.** The tool refuses if either core never satisfies
  `_demo_owns_title`, and refuses if the provisioned image is absent rather than reporting a vacuous
  pass.

### The cadence explanation is separately dead, and this supersedes it

`docs/issues/0133` recorded that "a one-field shutter inside a blocking `while (g_LoadStage < 6)`
loop can and does move that counter by a stage" and that the two cores "agree on 0 of 900 lockstep
fields". Both are true and neither is the cause. The field-boundary count inside the loop differs
(the product crosses it in one field, the reference in five+) because the reference's CD read takes
real time and the product's does not, and that difference is what decides which side of `0x800155E0`
a given field boundary sees. The stage index is not the divergence; the sample point is.

## 3. What the fix is, and whose it is

`load_stage` is a **step index**. Its value is stable only *between* `LoadLevel` calls, and
`demo_level_load`'s park is a *field* boundary that can and does land inside one — the route's own
`docs` say so, and `tools/probe_load_stage.py` argues for exactly this at length. The correct change
is to the route's decisive set, not to the loader:

* **`tools/oracle_spyro1.py:63`** declares `DeclaredRange("load_stage", G_LOAD_STAGE, 4, True)`. That
  single `True` is what makes the mid-load sample fail the route. It should not be decisive at a
  checkpoint whose park is inside a load; it remains meaningful where the load is finished and the
  value is resting (`demo_playing`, every `gameplay[n]` checkpoint), and there it already matches.
* The narrow, honest form is per-checkpoint, not a blanket relaxation: a `DeclaredRange` carries one
  decisiveness flag for the whole route, so expressing "decisive except at the mid-load barrier"
  needs the range's decisiveness to be a property of the checkpoint. That is a comparator change and
  it changes what the gate ASSERTS, which is why it is not mine to make.

**What would prove that change:** `tools/oracle_compare.py --policy demo` exits 1 on
`player.position` at `gameplay[1]` tick 702 and no longer reports `load_stage`, with
`demo_playing` and `gameplay[0]` still MATCH, and `--policy artisans` still 15/15 with 0 decisive
divergences. **What would refute it:** a run in which `load_stage` still reads 1 against 2 at
`demo_playing` or at a `gameplay[n]` checkpoint, because that would be a resting value differing and
would be a real loader defect after all.

I did not make this change. It edits the gate rather than the product, the ownership I was given for
this task is the probe, one issue, tests and native overrides, and "make the symptom stop appearing"
is exactly the move that has to be separable from "explain the symptom".

## 4. A second measurement from the same runs, which belongs to issue 0133

**The demo route's recorded tick-702 `player.position` divergence is presentation-dependent, and the
comparator's own contract says it must not be.** `tools/oracle_compare.py --help` says
`presentation must not change guest RAM`. Six runs of the same product binary, differing only in
`PSXPORT_SETTINGS`:

| settings file | product `player.position` @ tick 702 | console |
|---|---|---|
| `aspect=1, fps60=1` (only) | `b3d70200fd4a02004d520000` | `eed70200924c02004d520000` |
| + `ires/face_order/ssao/light/shadows = 0` | `b3d70200fd4a02004d520000` | identical |
| + `light_dir/light_ambient/light_diffuse` | `b3d70200fd4a02004d520000` | identical |
| + `ssao_strength/radius/bias/range/shadow_strength` | `b3d70200fd4a02004d520000` | identical |
| **`aspect=3, fps60=1` (only)** | **`0adc0200e94c0200ff500000`** | identical |
| `tools/shipping_settings.ini` as it stands | `0adc0200e94c0200ff500000` | identical |

The console is byte-identical in all six, so the product is the variable, and the variable is
`aspect`. `aspect=3` is `ASPECT_AUTO`, which the removed header comment on that same file already
warned resolves to the SINK's aspect — 512 in a headless run against 684 at `aspect=1`. **Changing
the render width changes `g_Spyro.m_Position` 500+ game ticks later.** That is a producer reaching
guest state, or a widened draw area feeding something the guest reads, and it is a defect under
`AGENTS.md`'s "Native render producers consume pre-GTE game state" whichever of the two it turns out
to be.

Two consequences the operator needs:

* **The value in `docs/issues/0133` (`b3d70200…`) is the 684-wide one.** Any later work that A/Bs
  against it must pin `aspect=1`, and `--product-env PSXPORT_ASPECT=1` does **not** do that — the
  ini file wins, which is how the first A/B I ran came back with the wrong answer. Pass
  `--product-env PSXPORT_SETTINGS=<file>`.
* **`tools/shipping_settings.ini` is in flight and was changed at 19:59 today to `aspect=3`.** It
  silently moved a recorded oracle divergence. That file is not mine and I did not touch it.

The `load_stage` finding is **independent** of all of this: `load_stage` reads `01` against `02` in
all six runs, at `aspect=1` and `aspect=3` alike.

## 5. Gates

| run | result |
|---|---|
| `tools/probe_load_step.py --selftest` | 20/20 checks (6 image facts, 14 classifier) |
| `tools/oracle_compare.py --policy demo` | **exit 1** — `demo_level_load` `load_stage` 1 vs 2 (unchanged), `gameplay[1]` tick 702 `player.position` diverges |
| `tools/oracle_compare.py --policy artisans` | **exit 0** — 15/15 checkpoints MATCH, 0 decisive divergences (the regression gate, still green) |
| `ctest --test-dir build` | **90/91**. The one failure is `spyro_psxport_pin_live`, and **it was already red before this change**: at 20:44, before I reconfigured anything, it reported `framework … configured 006eb917, current 9da9e96c`; after my reconfigure (below) the same test reports `you built against 9da9e96c … but this repo records 006eb917`. Either way the cause is the same and none of it is this issue: `psxport` HEAD moved twice while this work ran and `psxport.pin` still records `006eb917`. The fix is the operator's `reconfigure -> build -> gate -> --bump`, and bumping the pin is landing, so I did not do it. This change compiles nothing — `git status` shows `tools/probe_load_step.py`, this issue, and one `add_test` line in `CMakeLists.txt` as mine. |
| `probe_load_step_selftest` (new, registered in `CMakeLists.txt`) | Passed, 0.25 s |

**A shared-build side effect I caused, stated rather than left to be discovered.** Registering the
test needed `cmake -S . -B build`, which rewrote `build/psxport_resolved.txt` from `006eb917` to
`9da9e96c` while another agent was working in this tree. That is why the pin test's message changed
between my two runs. The other agent's next build will be configured against `9da9e96c`; the two
commits since `006eb917` are an analysis commit and the `PSXPORT_SETTINGS` commit, so the risk is
low — but it is a shared directory and I moved it without asking. |

**Provenance of every number above, because the tree was moving while this was measured.** All runs
used `build/bin/spyro_port` as built at **17:15**, against the receipt
`commit = 7f537273db66b4ba576d0651d66545354f52e0de`. By 20:33 another agent had reconfigured
`build/` (receipt `006eb917`) and by 20:43 rebuilt the binary (26,521,368 bytes against 26,501,312
at 17:15), with `game/render/frame/frame_renderer.h`, `game/render/frame/frame_renderer.cpp` and `CMakeLists.txt` modified
and four new `game/render/pause_menu_*` files in the tree. None of that is this issue's, and none of
it is boot-path code, but **the load-stepper numbers here describe the 17:15 binary and should be
re-taken on a quiescent tree before anything is concluded about the current one.** The A/B in
section 4 has the same provenance, and its mechanism is now named by `psxport` `006eb917`
("Running the game rewrote a tracked settings file and silently turned widescreen off, because
`PSXPORT_SETTINGS` is both the config input and the save target") — which is exactly how
`tools/shipping_settings.ini` acquired `aspect=3` underneath this investigation, and it belongs to
that commit's owner rather than to this issue.


## 6. What is NOT established here

* **Why** the product's CD read completes in one field where the reference's takes five is not
  established. It is a CD-timing difference in the framework's domain, it is not the load stepper,
  and it is not what the route reports. It is the reason the two field boundaries land on opposite
  sides of `0x800155E0`, and that is a reason, not a cause of the load stage.
* **Whether the tick-702 divergence is one defect or two.** With `aspect` pinned it is stable and
  reproducible; whether anything presentation-independent remains under it is open, and 0133's
  per-tick word-level work is unaffected by this issue except that its baseline must say `aspect=1`.
* **`g_LoadStage` after the load** is still unexplained. The load path is bounded by 10
  (`update.c:241`, `camera.c:491`) and a live run shows 9 and 13; nothing here says what those are.
* **No override was written, deliberately.** `LoadLevel` is ~600 instructions of CD-paced loading
  that currently produces a correct level entry on both cores. Replacing a correct loader to change
  a number a barrier sampled mid-call is the definition of a tap, and `psxport/AGENTS.md` and
  `AGENTS.md` both forbid one.
