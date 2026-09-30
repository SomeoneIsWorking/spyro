---
id: 155
title: Spyro 1 loading census - every CD read issuer, what waits on it, and which waits are loading-only
status: open
symptom: S033 (G005 loading removal) said "no load operation has been censused or classified". Without the census the first implementation step would have been a guess about which of Spyro 1's waits are I/O, which are authored presentation, and which the port has already removed
state_items: S033
tags: loading,cd,wad,census,re,transition,logo
created: 2026-09-30
---

## What this is, and what it is not

A static census. Nothing was launched. Every address below was read from `SCUS_942.28` (SHA-1
`84e3728ab947...`, matches `titles/spyro1/executable.json`) mapped the PS-X EXE way
(`file[0x800]` -> `t_addr 0x80010000`, `t_size 0x65800`, so 103,936 text words), or from the WAD
overlays inside `WAD.WAD` (the 110,260,224-byte archive at `scratch/wad_census/`, SHA-256 prefix
`7ba8961c3626bcec`, LBA 37 on the disc per `0x80012518`). Disassembly is
`external/psxport/tools/disasm.py` over a 2 MiB RAM image built that way (Capstone, 0 unknown words
in every range below). The WAD archive was **not** re-authenticated against the disc for this
issue; it is the file `tools/overlay_image.py` already uses.

Evidence tags used throughout:

* **[B]** read from instruction words or image bytes in this session; the word or address is quoted.
* **[D]** read from the decompilation (`external/spyro-1/src`) only, then cross-checked against the
  image by call-site count or by an address in [B].
* **[S]** read from this repository's own sources.
* **[I]** inferred. Named as such, with what would settle it.
* **[0151]** taken from `docs/issues/0151` (the presentation/cancellation inventory, uncommitted in
  the `skips` worktree when this was written). Where this issue re-read the bytes it says so.

## Headline

1. **Spyro 1 has no loading screen.** Every retail wait on the CD is either a tight spin with no
   draw and no VSync (frozen last frame) or a cooperative loader called once per field behind an
   authored card. There is no "LOADING" presentation object to remove.
2. **Storage latency is already gone at the CD primitive level.** The port natively overrides
   `0x80016500` (blocking), `0x80016698` (async) and `0x800163E4` (the retry/timeout step) in
   `game/core/cd_queue.cpp`. The blocking form copies the whole payload into guest RAM before it
   returns and never enters the spin loops; the async form copies at issue and delivers the guest's
   own completion callback on the next `0x800163E4` call. [B]+[S]
3. **What is left of G005 for Spyro 1 is four things**, none of them "remove the load":
   (a) the two 210-field boot logo holds; (b) proving the override payload and terminal state
   against retail per operation (nothing records this today); (c) measuring the residual latency the
   override path still has (per-stage cadence of the cooperative loaders, and the CD-music readiness
   gate `[0x800774B4] & 0x40`); (d) the cancellation gaps 0151 already names.
4. **The scan is closed:** 31 CD read issuer sites in total, all enumerated below; no indirect
   reference to the loaders exists anywhere in the executable or the 36 code overlays.

## 1. The CD primitives (all [B])

The executable links stock Sony libcd (C004) and Spyro wraps it in five functions. Identities are
established by their own bodies plus `external/spyro-1/src/cd.c`, whose shape they match
instruction for instruction.

| address | role | bytes that establish it |
|---|---|---|
| `0x80016500` | `CDLoadSync(sector, buf, size, offset, maxTime)` - blocking | signature: `0x80016504..0x80016520` save `$a0..$a3` to `$s4,$s5,$s3,$s1`; `0x8001653C lw $s6,0x48($sp)` = the 5th (stack) argument; issue sequence `0x800165A8 jal 0x80063C48` (CdControl; `$a0=0xE` = CdlSetmode from the delay slot `0x80016580`, mode byte `0x80` stored at `0x80016548`), `0x800165E0 jal 0x80064094` (CdIntToPos), `0x800165F0 jal 0x80063C48` with `$a0=2` (CdlSetloc), `0x8001662C jal 0x8006606C` (CdRead, `$a2=0x80`) |
| `0x80016698` | `CDLoadAsync` - same issue sequence, **no wait** | identical sequence from `0x800166D8` (CdControl) to its `jal 0x8006606C` at `0x80016758`; ends without a loop |
| `0x800163E4` | `CDLoadTime()` - read-time watchdog, retry step | `lw [0x800756E0]` (max time) `beqz` returns at `0x800163F4`; `slt` against `[0x8007588C]` at `0x80016408`; the timeout arm re-inits the CD (`0x80016414 jal 0x8006397C`), registers `0x80016490` (`0x8001641C lui $a0,0x8001` / `addiu $a0,$a0,0x6490`, `jal 0x8006623C` = CdReadCallback), sets `[0x800774B4]=0x40` (`0x8001642C/0x80016434`), spins `CdSync(1,0)==2` (`0x8001644C..0x80016454`) and reissues the read |
| `0x80016490` | `CDReadDone(intr, result)` - the read-complete callback | `0x80016498 andi $a0,$a0,0xFF`; `bne $a0,2`; on 2: `sw $zero` to `[0x80076BB8]` (`0x800164A8`), `[0x8007588C]` (`0x800164B0`), `[0x800756E0]` (`0x800164B8`) |
| `0x80076BB8` | the read-pending gate (`g_CdState.m_IsReading`) | closed census of stores to it (lui-paired, 103,936 words scanned, 3 matched): `0x800164A8` (callback clears), `0x80016628` (sync issuer sets 1), `0x80016754` (async issuer sets 1). Its loads go through a materialised pointer (`addiu $s2,$s2,0x6BB8` at `0x8001652C`), which a lui-paired scan cannot see, so the **read** side of the gate is not censused here |
| `0x80076B90` | `g_CdState.m_WadSector` | `0x80012518 addiu $v1,$v1,0x6B90` / `0x8001251C addiu $v0,$zero,37` / `0x80012524 sw $v0,($v1)` in `0x8001250C` (`WadInitialize`) |

Two loops in `0x80016500` are the whole of the retail blocking wait, and neither presents anything:

* **Loop A (wait for the previous read), `0x8001654C..0x8001659C`:** `lw $v0,($s2)` of the gate,
  `bnez` into `0x80016584 jal 0x800163E4`, `0x8001658C jal 0x8005637C` (SoundsUpdate),
  `0x80016594 jal 0x8002BBE0` (CDMusicUpdate), `0x8001659C j 0x8001654C`. No VSync, no draw.
* **Loop B (wait for this read), `0x8001663C..0x80016664`:** gate load, `bnez` into
  `0x8001665C jal 0x800163E4`, else `0x8001664C jal 0x80063BD8` (CdSync) `beq $v0,2` out,
  `0x80016664 j 0x8001663C`. No VSync, no draw, no sound update.

The CD-music state machine is XA streaming, not Redbook: `0x8002BBE0` (CDMusicUpdate) issues
CdControl with `$a0 = 0x1B` (CdlReadS) at `0x8002BCEC/0x8002BCF4` and `0x8002BE58/0x8002BE60`, and
`$a0 = 0x0D` (CdlSetfilter) at `0x8002BE30`, consistent with C003. The bit `0x40` of `[0x800774B4]`
(`g_CdMusic.m_Flags`) is tested by every cooperative loader before it will advance a stage
(`0x800153DC..0x800153E8` in LoadLevel, `0x8007ADB4..0x8007ADC0` in the title overlay), so **a data
load waits for the music command queue as well as for the read**.

### What the port does with these (all [S])

`game/core/cd_queue.cpp`: `cd_loader` (override of `0x80016500`) calls `ArchiveTransfer::read`,
which stages the whole range, SHA-256s it, writes it through the canonical memory writer (so
translated-block invalidation applies) and activates a WAD image identity, then
`publishTransferState` writes the gate as 0 and returns `2` - the guest never enters Loop A or B.
`cd_stream_read` (override of `0x80016698`) does the same copy but leaves the gate at 1 and sets a
completion latch; `cd_retry_step` (override of `0x800163E4`) runs the original, and when the latch
is set dispatches `0x80016490(2)`, i.e. the guest's own completion callback. A short or unavailable
range is refused with a typed fault before any RAM write (issue 0091, `tests/test_archive_transfer.cpp`).

## 2. The issuer census

**Method and denominators.** Every `jal` to a target was found over the 103,936 main-image words,
and over the 501,760 words of the 36 WAD entries that score as code (the same denominator claim C229
used). Indirect reach was then closed three ways over the same words: raw 32-bit data words equal to
the target (pointer tables), `lui`+`addiu`/`ori` materialisations, and `jalr`. **Matched K = 0 for
every loader and for every public entry named below**, in both corpora. The instrument was shown a
positive: the same materialisation scan on `0x80016490` (the CD read callback) finds exactly
`0x800124A4` and `0x8001641C`, both `lui`+`addiu` registrations. Nothing feeds a zero here except
the absence of such code; the scan is over the whole corpus, not a candidate list.

Function attribution uses the Ghidra function inventory (`scratch/decomp/spyro1/inventory.json`,
673 functions) and each site's argument registers were traced with a lui/addiu tracker, then read
by eye.

### 2.1 Blocking issuers: 11 sites of `jal 0x80016500`

Header fields are `g_WadHeader` at `0x8007A6D0` (`0x80012518` / `0x800129A4..0x800129AC` read
`[0x8007A6D4]` length and `[0x8007A6D0]` offset for the Universal logo, `0x8007A6E0/E4` for the
title overlay, `0x8007A6E8` for cutscene 0, `0x8007A708/0C` for the game-over skybox, `0x8007A710/14`
for PETE, `0x8007A958/5C` for the credits overlay). That layout is `include/wad.h`, and the 36-entry
level table is confirmed independently by the archive: in 35 of 35 non-empty entries
`dataOffset == overlayOffset + overlayLength`.

| id | site | function | loads | size | blocks how |
|---|---|---|---|---|---|
| S01 | `0x8001253C` | `0x8001250C` WadInitialize | WAD header, offset 0, to the overlay arena (`[0x800113A0]`) | 2,048 | spin; caller boot only (`0x800128F8`) |
| S02 | `0x80012924` | `0x800127C0` boot | title cutscene header (`a3=[0x8007A6E8]`) | 2,048 | spin, SCEA logo frozen |
| S03 | `0x80012970` | boot | title VRAM: `a2 = 0x40000` (`0x80012948 lui $a2,4`), offset `[0x80076C00]+[0x8007A6E8]`, dest `0x801C0000-[0x800755A4]` | 262,144 | spin |
| S04 | `0x80012994` | boot | title overlay (`a2=[0x8007A6E4]`, `a3=[0x8007A6E0]`) | 14,336 | spin |
| S05 | `0x800129C0` | boot | Universal logo image (`a2=[0x8007A6D4]`, `a3=[0x8007A6D0]`) | 110,592 | spin |
| S06 | `0x8005B83C` | `0x8005B7D8` | PETE.WAD (`a2=[0x8007A714]`, `a3=[0x8007A710]`) | 237,568 | spin; callers `0x80012C84` (boot end), `0x8002D284` (credits start), `0x8002D4A4` (in `0x8002D440`, cutscene end) |
| S07 | `0x8002D31C` | `0x8002D228` | credits overlay (`a2=[0x8007A95C]`, `a3=[0x8007A958]`) | 8,192 | spin; callers `0x8002D52C` (in `0x8002D440`) and `0x8002D860` (in `0x8002D810`) |
| S08 | `0x8002E328` | `0x8002E12C` pause menu | title overlay (`a2=[0x8007A6E4]`, `a3=[0x8007A6E0]`), then a `LoadCutscene` loop (`0x8002E338`) - Quit-to-title | 14,336 | spin |
| S09 | `0x8002EEC4` | `0x8002EDF0` game over | `g_WadHeader+0x38/0x3C` = the game-over skybox | 40,960 | spin |
| S10 | `0x80033688` | `0x800334D4` demo end | title overlay, then a `LoadCutscene` loop (`0x80033698`) | 14,336 | spin |
| S11 | `0x80014518` | `0x800144C8` | current level scene (`a1=[0x800785E4]`, `a2=[0x80078604]`, `a3=[0x80078600]` + the level entry) - respawn reload; sole caller `0x8002EE84` (respawn arm, `g_Gamestate==4`) | up to 0x27800 | spin |

Timeout argument: `sw $s0,16($sp)` with `$s0=0x258` (600) on each boot call (`0x800128FC`,
`0x80012928`, `0x8001295C`, `0x80012998`, `0x800129B8`), and `0x8002EE78 addiu $v0,$zero,0x258` /
`0x8002EEB0 sw $v0,0x10($sp)` for S09. [B]

### 2.2 Streaming issuers: 19 sites of `jal 0x80016698` in the main image, plus 1 in the title overlay

These are three cooperative stage machines that issue one read per stage and return; a later call
does the next stage once the read has completed. Stage to site mapping for LoadLevel is fixed by the
jump table at `0x80010A88` (14 little-endian entries: stage 2 `0x800155E4`, 3 `0x800156B4`,
4 `0x800156FC`, 5 `0x80015770`, 6 `0x8001583C`, 8 `0x80015888`, 9 `0x800158E0`, 11 `0x80015B80`,
12 `0x80015BD8`, 13 `0x80015CCC`); each issuer below lies inside its stage's range. [B]

| id | site | function | stage | loads | size |
|---|---|---|---|---|---|
| A01 | `0x8001569C` | `0x80015370` LoadLevel | 2 | level overlay | `LevelEntry[i].overlayLength`, 40,960-81,920 observed (WAD census) |
| A02 | `0x800156E4` | LoadLevel | 3 | level header | 2,048 |
| A03 | `0x80015750` | LoadLevel | 4 | level VRAM | 0x80000 (`512*512*2`) |
| A04 | `0x8001582C` | LoadLevel | 5 (second half) | SPU data | `vramSramSize - 0x80000` |
| A05 | `0x800158C8` | LoadLevel | 8 | level data | `dataSize` |
| A06 | `0x80015A3C` | LoadLevel | 9 | model data | `modelDataSize` |
| A07 | `0x80015BC0` | LoadLevel | 11 | level scene | `sceneSize` |
| A08-A14 | `0x80014608 .. 0x80014A08` (7 sites) | `0x80014564` LoadCutscene | 0,1,3,4,5,7,8 | cutscene header, VRAM (`D_8006EE5C[idx]` = `{0x40000,0x80000,0x60000,0x80000}` at `0x8006EE5C`), SRAM, data, model, (intro) model tail, scene | per cutscene |
| A15-A18 | `0x80014CCC, 0x80014E6C, 0x80015188, 0x80015248` | `0x80014B70` LoadDragonCutscene | 0,1,2,2 | dragon-rescue data (whole or overflow in blocks) | per dragon |
| A19 | `0x8003381C` | `0x800334D4` demo end | - | `wad1` title graphics, `0x40000` from `[0x8007A6D8]` | 262,144 |
| T01 | `0x8007ADFC` | title overlay (entry 2) | TSM_Init sub-state 0 | `wad1`, `a1 = [0x800785E8] - 0x40000` (`lui $a1,0xFFFC`), `a3 = [0x8007A6D8]` | 262,144 |

Counts agree with the decompilation: LoadCutscene 7, LoadDragonCutscene 4, LoadLevel 7, update.c 1,
titlescreen.c 1 = 20. The decompilation has **10** `CDLoadSync` calls but the image has **11**: the
missing one is S09 (`0x8002EEC4`), which lives in `func_8002EDF0`, an unmatched function the
decompilation carries only as assembly. A source-level census would have been short by one.

**Overlay closure.** Across the 36 code entries (501,760 words): `CDLoadSync` 0 sites,
`CDLoadAsync` 1 site (T01), `CDLoadTime` 2 sites (`0x8007AD78`, `0x8007AF5C` of the title overlay,
the same gate pattern as LoadLevel). The 35 level overlays and the credits overlay issue **no** CD
reads themselves. They do reach the loaders: 10 of the 35 level overlays (entries 9, 11, 15, 21, 23,
25, 33, 45, 57, 69) contain 3 `jal 0x80015370` sites each, five (19, 31, 43, 55, 67) contain
`jal 0x800144C8` (the scene reload), and **29 of the 36** contain a `sw` to `g_LoadStage`
(`0x80075864`; lui-paired over 501,760 words) - e.g. entry 9 at `0x8007C7B0`, `0x80084BAC`,
`0x80084D90`. So the level overlays initiate loads (portal and level-exit code) and let the main
image's stage machine do the I/O.

What the overlay `LoadLevel` sites look like (entry 9 as the example, [B]): `0x8007C7B0 sw $zero,0x5864($at)`
(`g_LoadStage=0`), `0x8007C800 jal 0x80015370` with `$a0=1` in the level's init;
`0x8007C908..0x8007C924` is `if (g_LoadStage < 13) LoadLevel(1)` per frame
(`slti $v0,$v0,0xD`); `0x8007CAE4..0x8007CB04` is `if (g_LoadStage == 13) { [0x800777E8]=6; LoadLevel(1) }`.
This is the same shape as the flyby card below. **Which authored sequence each of the ten overlays
implements was not read** [I].

### 2.3 Who drives the stage machines (and how each caller waits)

Callers of `0x80015370` LoadLevel (8 main-image sites) and `0x80014564` LoadCutscene (7) and
`0x80014B70` LoadDragonCutscene (1), attributed by function range. Blocking means a
`while (g_LoadStage < N)` loop with no presentation; per-field means one call per game step behind a
presented screen.

| caller | site | waits | what is on screen |
|---|---|---|---|
| boot `0x800127C0` | LoadCutscene `0x80012B28`, loop while stage < 10 (`0x80012B3C slti $v0,$v0,0xA` / `0x80012B40 bnez`) - **no VSync in the loop** | blocking | Universal logo, frozen at full fade |
| `0x80032B08` GamestateCutsceneTransition, `TSS_Loading` | LoadCutscene `0x80032D04`, `0x80032CF0..0x80032D28` while stage < 3, with CDMusicUpdate `0x80032D0C`; LoadLevel `0x80032D34` while stage < 6 (same shape) | blocking | one field of the card (Spyro at the start pose) |
| same, `TSS_Active` (level) | `0x800330EC..0x80033104`: `if (g_LoadStage < 13) LoadLevel(1)` per field (`slti $v0,$v0,0xD`) | per-field | the flyby card |
| same, `TSS_Active` (cutscene) | LoadCutscene `0x8003301C` (if stage < 7) and, after tick, `0x800330A8` while stage < 10 | per-field, then blocking | the card; then a screen clear |
| same, terminal | `0x80033118 slti $v0,$v0,0x180` (tick >= 384) `bnez` out; `0x80033130 bne $v1,13`; LoadLevel `0x80033104` and, after `0x80033158 jal 0x8004AC24`, `0x80033160 jal 0x80015370` | gated on **tick and** stage 13 | the card's last field |
| `0x8002DF9C` GS_LevelTransition | `0x8002DFC8 slti $v0,$v0,0xB` `bnez` -> `0x8002DFE8 jal 0x80015370`; else load the HUD flag `[0x800756B0]` and `bnez` past it | per-field; **held at stage >= 11 while the tally flag is set** | the gem tally |
| `0x800324D8` balloonist flight | `0x80032530 slti $v0,$v0,0xA` `beqz` past `0x8003253C jal 0x80015370` | per-field while stage < 10 | the balloon flight (authored, playable) |
| `0x8002EDF0` game over | LoadLevel `0x8002F304` (stage < 11, `0x8002F2F8`) and `0x8002F380` (after `[0x8007593C] >= 0x11`, `0x8002F374`) | per-field | the game-over spiral |
| `0x800333DC` credits state | `[0x80075704]==0x63` -> `sw 1` to `g_LoadStage` (`0x80033418`); `==0x64` -> `0x800334BC jal 0x80015370` | per-field | credits end fade [I] |
| `0x8002F3E4` dragon rescue | LoadDragonCutscene `0x80031428` | per-field | the dragon cutscene |
| `0x800334D4` demo end | LoadCutscene `0x80033698` blocking (`while stage<10`) then A19 async | blocking | a cleared screen |
| `0x8002E12C` pause-quit | S08 then LoadCutscene `0x8002E338` blocking | blocking | frozen pause screen |
| `0x800331AC` cutscene update | LoadCutscene `0x80033280` | blocking (outro cutscene entry) | cleared screen |

LoadLevel itself contains two non-I/O gates that an I/O-only view would miss. Stage 12
(`0x80015BD8..0x80015CC8` [B]): if `[0x800756D0]` (`g_HasLevelTransition`) is 0 it advances at once
(`0x80015BE4 bnez`; `0x80015BF8 addiu $v0,$v0,1`); otherwise it returns while `[0x80075910]` is
nonzero (`0x80015C00..0x80015C08`), then requires the portal/camera alignment
(`slti $v0,$v0,0x80` at `0x80015C5C`, or `< 0x10` at `0x80015CB0`) before advancing. That is a wait
on **Spyro's walk-in animation**, not on the disc. Stage 13 is the final initialisation and sets
the next gamestate (entrance animation, or playing).

**Who sets `g_LoadStage`** (lui-paired, 103,936 words, 21 stores): boot `0x80012B1C` (=3);
LoadCutscene `0x8001468C, 0x80014854, 0x80014870, 0x80014B50`; LoadLevel `0x800155E0, 0x8001581C,
0x80015B74, 0x800163B8`; `0x8002C690` (`func_8002C664`, return home, =0); `0x8002D7C4`
(`0x8002D580`); `0x8002E31C` (pause-quit, 0); `0x8002F098, 0x8002F0B8` (game over/respawn);
`0x80032B7C, 0x80032BB4, 0x80032C08` (the card's setup); `0x80033238`; `0x80033418` (credits);
`0x8003366C` (demo end); `0x80057200` (`0x80056F64`, the portal-without-path fallback, which also
stores the HUD flag `0x80057210` and ticks `0x80057218`). Plus the 29 overlays above.

## 3. What each load would cost in retail (arithmetic, not a measurement)

Sizes are read from the archive: 35 non-empty level entries, **payload per level 1,591,296 to
2,099,200 bytes, mean 1,923,306** (overlay + 2,048 + VRAM/SRAM + data + model + scene). At the
nominal 2x CD rate (307,200 B/s) that is 5.2 to 6.8 s per level **before seeks and before the XA
stop/start commands**, which this census cannot size. Boot's five synchronous reads total 391,168
bytes (1.27 s); PETE 237,568 (0.77 s); the four cutscene entries 0.98 to 2.42 MB (3.2 to 7.9 s;
cutscenes 1 and 3 read exactly their whole archive entry, 2,420,736 and 2,222,080 bytes, which is a
consistency check on the LoadCutscene read sizes above). Retail's tally length is
`g_LevelTransTicks > 416` (`0x8002DB20 slti $v0,$v1,0x1A1`, ticks advance by `g_DeltaTime`
`[0x800756CC]`), which is the same order as the nominal load time - the tally is sized to cover the
load.

## 4. Classification

**LOADING-ONLY** means removing the wait removes no authored content. **AUTHORED** means a
gameplay-visible or designed sequence that stays.

| # | operation (ids) | blocking or streaming | wait it drives | presentation | class | state in the port |
|---|---|---|---|---|---|---|
| 1 | S01-S05, boot | blocking spin | first hold: 210 fields counted from the stamp at `0x80012844`, so retail **includes** the load time in it | SCEA logo, static | wait: LOADING-ONLY; logo hold: LOADING-ONLY presentation (G005: logos) | spin already removed; hold skippable (see 2) |
| 2 | boot logo holds | - | `0x800129C8..0x800129E8` (`VSync(-1)`, `subu $v0,$v0,$s4`, `slti $v0,$v0,0xD2`) and `0x80012B48..0x80012B68`; the stamps are `0x80012844` and `0x80012A7C`. **No pad read exists in `0x800127C0..0x80012CEF`** (none of the three `g_Pad` words `0x80077378/7C/80` is lui-paired inside it, and none is `lui`+`addiu`-materialised there; 103,936 words scanned; the instrument fires elsewhere: `0x80077380` is paired in 10 other functions, including `0x800331AC` and `0x8002EDF0`, and the base is materialised in 9 others) | two logos, 8-field fades | LOADING-ONLY presentation (retail has no cancel) | `BootSequence` `HoldFirst`/`HoldSecond` skip on Start or Cross (enhancement) |
| 3 | boot stages 3->10 (LoadCutscene) | blocking, no VSync | the Universal logo is on screen while it runs | logo | LOADING-ONLY wait | port presents one field per LoadCutscene call (`AdvanceLoadState`), i.e. at least 7 fields of logo that retail shows as one frozen stretch; Start is not honoured until the second hold |
| 4 | S06 PETE at boot end | blocking | - | none | LOADING-ONLY | override removes it |
| 5 | title TSM_Init sub-states 0/1 (T01) | per-field, gated by the CD gate | title fly-in does not start until `wad1` is in; `CDLoadTime` + gate + CdSync + `0x40` at `0x8007AD78..0x8007ADC0` | title fly-in | wait LOADING-ONLY; the fly-in itself AUTHORED | guest-owned; instant CD makes it ~2 guest calls [I] |
| 6 | title fly-in skip | - | `0x8007AC38 slti $v0,$v0,0x12C` (tick >= 300), `0x8007AC48 lw [0x80077380]`, `0x8007AC50 andi $v0,$v0,0x840`, then `0x8007AC78 addiu $v0,$zero,0x492` (tick = 1170) | - | AUTHORED, retail cancel exists | guest-owned, no arm needed |
| 7 | new-game card `TSD_Cutscene` (LoadCutscene 0->3 blocking, 3->7 per-field, 7->10 blocking at tick >= 384) | mixed | `0x80033118` tick >= 384 and stage == 7 | card: Spyro flies, text | card AUTHORED (384-tick minimum); the loader waits inside it LOADING-ONLY | no cancel (0151 F3: terminal block at `0x8003304C..0x800330E8` ends `j 0x80033190`, the epilogue) |
| 8 | "THE ADVENTURE BEGINS" `TSD_Level` (LoadLevel stage 1->6 blocking, ->13 per-field) | mixed | tick >= 384 **and** `g_LoadStage == 13` (`0x80033118`, `0x80033130`) | card | see decision D1 below | `TransitionSkip` Start/Cross arm, gated on stage 13 |
| 9 | attract `TSD_DemoLevel` card (same code, `0x80033140 bne $v1,2`, then `0x8003314C sw [0x80075714]=1`, `0x80033154 sw [0x80075884]=0`) | as 8 | as 8 | card, then the demo | AUTHORED | none, deliberately (0151; issue 0129) |
| 10 | portal entry GS_LevelTransition (LoadLevel per field) | per-field, **held at stage >= 11 by the tally flag** | `0x8002DFC8..0x8002DFE8`; the flag clears at `0x8002DB38` once `g_LevelTransTicks > 416` | gem tally | tally AUTHORED (information); the loader's stage 2-11 time is LOADING-ONLY and is hidden behind it | `TransitionSkip` tally arm (clears the same flag) |
| 11 | LoadLevel stage 12 | - | Spyro walk-in alignment, not I/O | the portal/entrance motion | AUTHORED gate | guest-owned |
| 12 | GS_EntranceAnimation (after stage 13) | - | camera gate | level entrance sweep | AUTHORED | 0151: no retail cancel, not dispatchable |
| 13 | return home (pause-quit in a sub-level): GS_ExitLevel glide then `0x8002C664` | per-field | two counter wraps (0151) | glide | AUTHORED | arm exists, never run live (0151) |
| 14 | balloonist (LoadLevel stage < 10 per field) | per-field | - | the balloon flight | AUTHORED (playable) | guest-owned; loader hidden |
| 15 | dragon rescue (A15-A18) | per-field | - | the dragon cutscene | AUTHORED | guest-owned |
| 16 | respawn (S11 at `0x8002EE84`) | blocking spin, after the spiral's first `0x10` ticks (`0x8002EE5C slti $v0,$v0,0x10`) | none but the frozen frame | spiral then gameplay | LOADING-ONLY | override removes it |
| 17 | game over (S09, LoadLevel stage < 11) | blocking then per-field | the spiral runs while the homeworld loads; retail accepts **held Start** once `g_GameOverTicks >= 0x169`: `0x8002F320 slti $v0,$v0,0x169`, `0x8002F330 lw [0x80077380]`, `0x8002F338 andi $v0,$v0,0x800`, `0x8002F344 jal 0x8003FDC8`, `0x8002F350/0x8002F35C` | spiral | AUTHORED with a retail cancel | guest-owned |
| 18 | pause-quit to title (S08 + LoadCutscene loop) | blocking | frozen pause screen | none | LOADING-ONLY | override removes the spin; the loop is instant |
| 19 | attract demo end (S10 + LoadCutscene loop + A19) | blocking then async | none | cleared screen | LOADING-ONLY | same |
| 20 | credits start (S07, S06) | blocking | none | cleared screen | LOADING-ONLY | same |
| 21 | XA music (`0x8002BBE0`, CdlReadS / CdlSetfilter) | streaming | gates every loader stage via `[0x800774B4] & 0x40` | none | not a load; **a load-gating dependency** | host XA decoder (C003, issue 0115); residual latency unmeasured |
| 22 | memory card (libmcrd Exist/Accept/ReadFile/CreateFile/WriteFile/Format, title overlay save picker and the fairy save state) | async, callback chain | "checking card" / save UI | AUTHORED UI | **enumerated, not byte-resolved**: only `MemCardExist 0x8006635C` and `MemCardAccept 0x800665B8` are recorded (issue 0096); the fairy function `0x800314B4` jal's `0x800665B8` once and five more libmcrd addresses (`0x800662BC, 0x8006631C, 0x80066E28, 0x800670E4, 0x80067628`) that were not named here |

## 5. The retail cancellation routes (what this issue confirms of 0151)

Not re-derived. Confirmed from the image in this session (every line an address quoted above):

* **Boot has no pad read** - confirmed, with the denominator above. The port's boot skip is an
  enhancement, as 0151 and `start-skip-map.md` say.
* **Title fly-in skip (held Start/Cross, tick >= 300, writes 1170)** - confirmed at
  `0x8007AC38/0x8007AC48/0x8007AC50/0x8007AC78`. 0151 lists the reader; the destination value
  `0x492` and the tick gate are additions. The skip stores to the cutscene layout's tick through the pointer at `[0x80075680]`
  (`0x8007AC6C/0x8007AC70`, `0x8007ACCC sw $v0,($v1)`; the tick advances by 2 at `0x8007AC04` and
  the ordinary path caps at `0x44C`), inside the guest's own function, so it is a guest-owned route.
* **Level-flyby terminal** (`0x80033118`, `0x80033130`, `0x80033158 jal 0x8004AC24`,
  `0x80033160 jal 0x80015370`) and the two `TSD_DemoLevel` stores (`0x8003314C`, `0x80033154`) -
  confirmed.
* **Tally terminal** (`0x8002DB20`, `0x8002DB38`) and the LoadLevel hold
  (`0x8002DFC8..0x8002DFE8`) - confirmed, including that the flag test is on `[0x800756B0]`.
* **Game-over held Start** (`0x8002F338 andi $v0,$v0,0x800`) - confirmed, including the
  `g_GameOverTicks >= 0x169` gate.
* **Not re-checked:** the intro-cutscene held-Start acceleration at `0x80033354`, the stage-9
  terminal, the balloonist `jalr` at `0x800329D8`, the return-home counter chain, every live
  measurement in 0151's table, and Finding 4 (the Start leg of the flyby arm).

Two additions 0151 lacks, both for the logos:

* **A press during a fade or during the 3->10 loader is dropped. [FIXED 2026-10-01, see "Latch measured" below.]** `BootSequence::step` reads
  `fields_.presentationSkipPressed()` (a per-field edge, `FieldOwner::presentationSkipPressed` ->
  `pad.pressedButton(kPadStart|kPadCross)`) only in `HoldFirst` and `HoldSecond`. A press during
  `FadeFirstIn`, `FadeFirstOut`, `FadeSecondIn`, `AdvanceLoadState` or `FadeSecondOut` is neither
  latched nor consumed. [S]+[I]: the consequence for a user who taps Start at power-on is "nothing
  happens, then it works on a later tap". Settle with a run that injects one Start edge per boot
  phase and logs which phase consumed it.
* **The first hold is shorter than it looks in the product**, because retail's stamp is taken at
  `0x80012844` before the fades and loads. `BootSequence::initialize` reproduces that
  (`firstHoldStart_ = fields_.counter()` before the first fade). With instant loads the hold is
  therefore `210 - 8 - load-fields` of *visible* logo, not 210. [S]+[B]; the visible duration was
  not measured.

## 6. Removal designs, per loading-only wait

Rules applied: loads complete through the title's own load mechanics; no fast-forwarding of the
simulation; no write of a phase, timer or scene word; cancellation only through a recovered route
or a purpose-built skip that reaches the same lifecycle; authored cutscenes stay.

**R1. CD primitive waits (rows 1, 4, 16, 18, 19, 20 and S01-S11).** *Done* by `cd_queue.cpp`.
Nothing is written to a phase word: the override returns the loader's own success value and, for the
async form, lets the guest's own callback `0x80016490` run. Remaining work is proof (M1, M2) and
one measured risk: the copy runs on the emulation thread and decompresses CHD hunks. From issue
0115 a hunk costs about 0.43 ms and a sector read is 8 per hunk, so the largest stage read
(`0x40000` = 128 sectors = 16 hunks; the 0x80000 VRAM read is 32) is about 7-14 ms and a whole level
about 55 ms [I, arithmetic on 0115's number]. That is below one field, so no worker thread is
justified **unless M3 shows otherwise**. If it does, the owner is `ArchiveTransfer`: its
`SectorReader` is already injected (`archive_transfer.h`), so a prefetching reader keyed by
`(lba, length)` and filled at `cd_stream_read` issue can be substituted while the guest-visible
completion point (`cd_retry_step`) stays identical.

**R2. The two logo holds (row 2).** Owner: `titles/spyro1/core/spyro1_boot_sequence.cpp`
(`BootSequence`), already in place. Retail has no route, so this is the "purpose-built skip that
establishes the same lifecycle" case: a press takes the cleanup `0x80016914` and the fade-out the
expiry takes (`leaveFirstPresentationHold`, `leaveSecondPresentationHold`). Gap to close: make the
press latch. The smallest change is one member on `BootSequence` (a `PressLatch`, set by any
Start/Cross edge seen in a fade or loader phase, consumed by the next hold phase; DONE, see "Latch measured") so a tap during a
fade-in is honoured at the first opportunity instead of lost. It must not advance `fields_`, and it
must not skip `AdvanceLoadState`; the loader still has to reach stage 10 (row 3), which is what makes
the second skip lawful. The alternative - cancelling mid-fade - would have to call the fade's
remaining iterations, which is a second lifecycle path and is rejected.

**R3. Boot stages 3->10 presented as one field each (row 3).** Retail runs this as a tight loop with
no presentation; the port presents a field per call. Smallest change: loop `LoadCutscene` inside one
step until stage 10 and deliver one field (the guest code has no VSync here, so this is the retail
shape). It changes no guest state, only how many host fields the logo occupies. Do it only after M3
shows the cadence costs visible time; it is at most about 7 fields.

**R4. Cooperative loaders behind authored cards (rows 5, 7-10, 14, 15, 17).** No removal: the card
or tally is the authored presentation and the loader completes inside it. With the native CD the
loader's own duration is one guest call per stage (LoadLevel 12 stages, one of them stage 5 twice), so
the card is now governed by its authored minimum (384 or 416 ticks), not by I/O. The cancel arms
already cancel only the presentation minimum and leave the load to complete (`TransitionSkip` holds a
press until `g_LoadStage == 13`). Nothing new is needed; the open item is decision D1.

**R5. The stage-12 walk-in alignment (row 11).** Authored; leave it. Cancelling it would mean
writing the camera/Spyro state the gate waits on, which is forbidden.

**R6. The XA readiness gate (row 21).** Not a removal. It is the one place where a *port-side*
latency may survive the native CD: the loaders will not advance until `[0x800774B4] & 0x40` (music
command idle). If the host XA model takes real time to acknowledge the pause/stop that precedes a
data read, that time is a loading-only wait that the override does not touch. Whether it exists is
M3. If it does, the fix is in the CD/XA model (complete the control command on the next field), not
a write to the flag: the flag belongs to `CDMusicUpdate`.

**R7. Memory card (row 22).** Out of the CD census. The title save picker's "checking" states and the
fairy save are authored UI with host-file completion. Decide only after the libmcrd entry points are
named (`0x8006635C`/`0x800665B8` are, the rest are not) and M3 counts how many fields the card
chain takes.

**New class?** None is needed for R1-R7. The one addition worth making is a **`LoadLedger`**
(`game/core/load_ledger.{h,cpp}`, owned by `cd_queue.cpp`'s registration and fed by
`ArchiveTransfer::read`, which already computes the digest and discards it after
`imageCatalog().activate`). One entry per operation: issuer site (`$ra - 8` on entry to the
override), LBA, byte length, destination, SHA-256, `deferred`, field at issue, field at completion,
`g_LoadStage`. It is the instrument for M1-M3 and it is diagnostics, not gameplay: it reads and
logs, never writes guest state.

## 7. What would prove payload and terminal state identical to retail

"Loading removal is suppressed under oracle comparison" (G005), so the proof is a three-way
comparison, and each leg must be shown capable of failing.

**M1. Payload (per operation).** Run the same deterministic route under (a) the oracle core with
real CD timing, (b) the port with the override. Compare the **multiset** of
`(issuer site, LBA, length, destination, SHA-256)`. Denominator to print: issuer sites exercised out
of the 31 in section 2 (`S01-S11`, `A01-A19`, `T01`), and which were *not reached* - a route that
never dies never reaches S09/S11, and a route that never pauses-and-quits never reaches S08.
Negative: truncate the archive by one sector (the existing `test_archive_transfer` seam) and require
M1 to report the operation as unequal.

**M2. Terminal state (per terminal).** At each operation's natural terminal - LoadLevel leaving stage
13 (`g_LoadStage = -1`, store `0x800163B8` [B] is the stage-13 exit; other terminals listed in
section 2.3), LoadCutscene reaching 10, each sync return - compare (i) a hash of the destination
range, (ii) a named field list: `g_LoadStage`, `g_Gamestate` (`0x800757D8`), `g_LevelId`,
`g_LevelIndex`, the `g_Buffers` pointers, `g_LevelHeader` (`0x80076C00`, 464 bytes), `g_CdMusic`
flags (`0x800774B4`), and the gate `0x80076BB8`. Use the field-list comparer 0151 describes
(`tools/ram_compare.py`, in that worktree). The expected result is **not** "all equal": the clocks
(`g_LevelTransTicks`, `m_Tick`, field counters) differ by design, and 0151 Finding 2 predicts a
different `D_800758B8` after a return-home cancel. Report those as the known deltas and require
everything else to match.

**M3. Residual latency (is anything loading-only left).** With the `LoadLedger`, for every
operation print fields from issue to completion and, for the stage machines, fields per stage,
split by whether `[0x800774B4] & 0x40` was clear at issue. A zero latency and one field per stage
is the expected answer for the blocking issuers and the one-call answer for the streaming ones; any
operation over a few fields names the R6 gate or a host cost. **Shown the other answer**: run once
with the override's completion latch withheld and require M3 to show the stage machine stalling.

**M4. Absence of loading presentation.** Capture frames across the level entry on both legs and
assert the port never presents a frame the retail run presents only while the gate
(`0x80076BB8`) is 1 *and* no authored counter is advancing. For Spyro 1 this should be vacuous -
there is no loading screen - and the test's value is exactly to say so with a denominator.

## 8. Decisions (resolved by the standing rules)

**D1. The level flyby card and the gem tally are AUTHORED transitions and stay.** The card has an
authored camera path and text (`draw.c:func_8001E6B8`) and runs to its own authored terminal
(tick >= 384); with the loader completing inside it, its length is authored, not load-bound. The
rule keeps authored transitions and allows them "a minimum duration with the same cancellation
route", so issue 0129's Start/Cross cancel through the recovered stage-13 route is consistent with
it, not in conflict: the card is presented, and it is cancellable. Nothing changes.

**D2. The logo holds take no minimum.** The rule is that logo screens accept Start/Cross through a
complete cancellation route; it names no minimum for logos. `BootSequence`'s skip at the first hold
field stands. The one defect is the press lost during a fade or the stage 3->10 loader (section 6),
which the latch fixes.

## 9. Verified versus inferred

**Byte-verified in this session [B]:** everything quoted with an address above, in particular the
five CD primitives and both spin loops; all 11 blocking issuers with their header-field arguments;
all 19 streaming issuers and their stage mapping through the LoadLevel jump table; the closed
scans (0 indirect references in 103,936 + 501,760 words, with the `0x80016490` positive control);
the gate's three stores; the 21 `g_LoadStage` stores; the boot holds and stamps; the title overlay
gate, skip and `0x492`; the flyby/tally/game-over terminals; LoadLevel stage 12; the WAD layout and
its 35/35 adjacency check.

**Read from source only [D]/[S]:** the stage bodies (what each stage loads beyond its size
argument), the decompilation-side names, the port's override behaviour and the boot skip.

**Inferred [I], with the settling measurement:**
* the port's residual fields per stage and per card (M3);
* whether the XA readiness gate costs real time (M3/R6);
* the host decode cost per stage (arithmetic on 0115; M3);
* what the ten overlay `LoadLevel` callers and the 29 overlay `g_LoadStage` writers sequence
  (read one overlay's three sites; the rest are structural matches only);
* the credits-end fade (row 20's neighbour, `0x800333DC`);
* ~~that a Start tap in a boot fade is lost (code reading; needs one run)~~ - measured, see "Latch
  measured".

**Not done (as of the census):** libmcrd addresses other than Exist/Accept; per-overlay classification; any run (the
gate was in use by another agent); re-authentication of `scratch/wad_census/WAD.WAD`.

## Latch measured (2026-10-01)

`PressLatch` (`titles/spyro1/core/spyro1_press_latch.h`, one member of `BootSequence`) holds the
per-field Start/Cross edge from the field it was pressed in to the next hold phase, which takes it
once through `leaveFirstPresentationHold` / `leaveSecondPresentationHold`. It is read once per step
(each step follows one delivered field), advances no field, skips neither fade nor loader, and writes
no guest word. A press in the last fade or finalisation, past every hold, is not carried. Unit test:
`tests/test_press_latch.cpp` (`press_latch` in CTest), with the negative (no press, no skip), a press
on the first, last and loader fields, one press taken once, and two presses making two skips.

Product proof, `tools/drive.py gameplay --debug pace`, offscreen, one instance at a time, fields counted
from the `site=boot-*` field-delivery lines; the press is a forced Start edge at a named field
(`PSXPORT_FORCE_BUTTONS`/`PSXPORT_FORCE_HOLD` with `..._AT`/`..._STOP_AT`). "old" is the pre-latch
binary `build/bin/spyro_port` of 2026-09-29, "new" is this tree:

| run | press at field | logo fields (fade) | first hold | load-state | second hold | reached GS_Playing |
|---|---|---|---|---|---|---|
| new, no press | none | 32 | 202 | 4 | 198 | frame 6360 |
| old, press in first fade | 0 | 32 | **202 (press lost)** | 4 | 198 | see note |
| new, press in first fade | 0 | 32 | **0** | 4 | 198 | frame 5320 |
| old, press in the loader | 227 | 32 | 202 | 4 | **198 (press lost)** | frame 5520 |
| new, press in the loader | 227 | 32 | 202 | 4 | **0** | frame 5320 |
| new, press in the 2nd fade-in | 220 | 32 | 202 | 4 | **0** | frame 5320 |

The 32 fade fields and the 4 load-state fields are unchanged in every leg, which is the evidence that
the latch skips neither a fade nor the loader. The log prints the existing "Start/Cross ends ..."
line for the hold that took the press. The old/first-fade leg did not reach GS_Playing because of a
driver defect, not the product: `title_prompts` sent the PRESS START platform one Start per run, and
with the logos skipped that screen is reached about 1000 fields earlier where it does not yet accept
it, so it was never retried (`drive.py REFUSED` after 12000 fields on old and new binaries alike);
the platform prompt is now repeatable and the same new-binary run reaches GS_Playing.

Not measured: a Cross press (the edge is one mask, `kPadStart | kPadCross`, and the unit test is
button-agnostic); a press on a real controller; retail's own logo duration under the oracle.

## 10. Method notes for whoever repeats this

* Build the RAM image from the file, not the file from the image: text is at file offset `0x800`,
  `t_addr 0x80010000`. Mapping the file from its start lands `0xF800` high.
* A `jal`-only scan of the image finds nothing wrong here, but only because the loaders are never
  reached through a pointer (shown, not assumed). This title's libs are reached through `jal`; Spider-Man 1's are not.
* The decompilation's 10 `CDLoadSync` calls versus the image's 11 is the lesson of the census: an
  unmatched function (`func_8002EDF0`) is invisible to a source-level grep.
* The lui-paired store scan cannot see accesses through a materialised pointer; the gate's three
  stores are complete, its loads are not claimed.
