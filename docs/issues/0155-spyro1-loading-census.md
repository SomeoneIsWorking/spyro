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
issue; it is the file already uses.

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

## M3 measured (2026-10-01) — the `LoadLedger` exists and the residual latency is 0 to 2 fields

**What landed.** `spyro::load_ledger::Ledger` (`game/core/load_ledger.{h,cpp}`), one member of
`SpyroContext` beside the `ArchiveTransfer` whose reads it records, fed by the two CD overrides in
`cd_queue.cpp` and reported once at run end by `reportRuntimeRun`. The per-operation table goes to
the path named by `PSXPORT_LOAD_LEDGER`; the one-line summary is always logged as `[load-ledger]`.
It is **diagnostics only** — it reads `g_LoadStage` (`0x80075864`) and `g_CdMusic.m_Flags`
(`0x800774B4`) and writes neither, and no pending operation is ever read to make a decision. The
digest is the one `image_publication::digest` already computed for the image identity, handed over
through a `PayloadObserver` the transfer calls: **a second SHA-256 over the same bytes was not
written**, because two digests over one payload are two facts that can disagree and the one that
names the image is the one the guest's code will execute. [S]

**Coverage denominator.** `0x80016500` and `0x80016698` are entered by the port itself as well as by
the guest: `BootSequence::loadAssets` (`titles/spyro1/core/spyro1_boot_sequence.cpp:104-135`)
performs four of the census's own blocking reads — S02, S03, S04, S05 — by **dispatching
`0x80016500` directly**, not by letting the guest's boot code reach it. That is the port replacing
retail's blocking boot loop with one call per read, and it is why the run below reads `16/31`
exercised: those four operations have no guest `jal` to name. See "the `$ra - 8` failure mode"
below; the site column is instrumented, not asserted. [S]+[B]

### The M3 table

`tools/drive.py gameplay` to `GS_Playing` (frame 6360 route, `PSXPORT_LOAD_LEDGER=scratch/load_ledger_m3.txt`),
offscreen, one instance. **26 operations, 7 blocking and 19 streaming, 0 pending at run end, worst
measured latency 2 fields, no operation over 2 fields.** The first ten rows are cut off at
`nocounter` because they are issued before the title's field owner is published — they are counted
as such rather than reported as a free load. The `pc` column is the guest PC the override was
entered with and is `0x80016500` or `0x80016698` in every row, which is the falsifier that the tap is
on the loader and not somewhere that merely looks like it.

| # | site | lba | bytes | dest | kind | `g_LoadStage`@issue | field@issue | field@done | latency (fields) | XA bit clear @issue | sha256[0:16] |

**Format note (2026-10-01).** `load_ledger.cpp`'s report now carries the byte offset and the FULL
digest, because that table is M1's input record and a 16-character prefix plus no offset is a
comparison of a prefix and a guess. The table above is left as measured, with its own header.
|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | **S01** `0x8001253C` | 0x25 | 2,048 | `0x8007AA38` | blocking | 0 | nocounter | nocounter | 0 | no | `15d3e9c45f07e27a` |
| 1 | host dispatch | 0x25 | 2,048 | `0x8007AA38` | blocking | 0 | nocounter | nocounter | 0 | no | `229efedc2014e753` |
| 2 | host dispatch | 0x25 | 262,144 | `0x801BF800` | blocking | 0 | nocounter | nocounter | 0 | no | `2209002b8c4f8754` |
| 3 | host dispatch | 0x25 | 14,336 | `0x8007AA38` | blocking | 0 | nocounter | nocounter | 0 | no | `8ee66fe644407bf1` |
| 4 | host dispatch | 0x25 | 110,592 | `0x801A4800` | blocking | 0 | nocounter | nocounter | 0 | no | `6f82c9e7efacbe5f` |
| 5 | **A10** `0x80014740` | 0x25 | 292,864 | `0x8007DDE8` | stream | 3 | nocounter | nocounter | 0 | no | `8e744bf40bd05b3a` |
| 6 | **A11** `0x800147C8` | 0x25 | 137,216 | `0x8007DDE8` | stream | 4 | nocounter | nocounter | 0 | no | `fe0ade3114ed19f5` |
| 7 | **A12** `0x800148AC` | 0x25 | 262,144 | `0x8009F20C` | stream | 8 | nocounter | nocounter | 0 | no | `ca2313f64c15214c` |
| 8 | **A14** `0x80014A08` | 0x25 | 26,624 | `0x800DF20C` | stream | 8 | nocounter | nocounter | 0 | no | `c6d12694330e58f6` |
| 9 | host dispatch | 0x25 | 237,568 | `0x8018B800` | blocking | 10 | nocounter | nocounter | 0 | no | `b73da4852629d203` |
| 10 | **T01** `0x8007ADFC` | 0x25 | 262,144 | `0x80147BB0` | stream | 10 | 436 | 438 | **2** | no | `e082fe407e99012f` |
| 11 | **A08** `0x80014608` | 0x25 | 2,048 | `0x8007AA38` | stream | 0 | 1861 | 1861 | 0 | no | `c90a76a360501288` |
| 12 | **A09** `0x80014680` | 0x25 | 524,288 | `0x8007AA38` | stream | 1 | 1861 | 1861 | 0 | no | `ab614e10145406fd` |
| 13 | **A10** `0x80014740` | 0x25 | 491,520 | `0x8007AA38` | stream | 3 | 1863 | 1865 | **2** | no | `68fc72ba5bcf30a1` |
| 14 | **A11** `0x800147C8` | 0x25 | 75,776 | `0x8007AA38` | stream | 4 | 1865 | 1867 | **2** | no | `503b741f825c334b` |
| 15 | **A12** `0x800148AC` | 0x25 | 851,968 | `0x8008D020` | stream | 6 | 1867 | 1869 | **2** | no | `c20a3b2e49e41d86` |
| 16 | **A13** `0x80014920` | 0x25 | 393,216 | `0x8015D020` | stream | 7 | 2629 | 2629 | 0 | no | `93e94b3be96a25bd` |
| 17 | **A14** `0x80014A08` | 0x25 | 81,920 | `0x801BD020` | stream | 8 | 2629 | 2629 | 0 | no | `49028e7ae021a14c` |
| 18 | `0x8002D4A4` (unattributed) | 0x25 | 237,568 | `0x8018B800` | blocking | 10 | 5571 | 5571 | 0 | no | `b73da4852629d203` |
| 19 | **A01** `0x8001569C` | 0x25 | 57,344 | `0x8007AA38` | stream | 2 | 5573 | 5573 | 0 | no | `7df6cf2f3ed4a71a` |
| 20 | **A02** `0x800156E4` | 0x25 | 2,048 | `0x80088620` | stream | 3 | 5573 | 5573 | 0 | no | `ebc99669a4ca0d3c` |
| 21 | **A03** `0x80015750` | 0x25 | 524,288 | `0x80088620` | stream | 4 | 5573 | 5573 | 0 | no | `a96e0f2471697a57` |
| 22 | **A04** `0x8001582C` | 0x25 | 450,560 | `0x80088620` | stream | 6 | 5573 | 5575 | **2** | no | `321a0cecbb0b163a` |
| 23 | **A05** `0x800158C8` | 0x25 | 579,584 | `0x80088620` | stream | 8 | 5579 | 5581 | **2** | no | `0a670f58d08e1f86` |
| 24 | **A06** `0x80015A3C` | 0x25 | 317,440 | `0x8011593C` | stream | 9 | 5581 | 5583 | **2** | no | `71aeec6c579796aa` |
| 25 | **A07** `0x80015BC0` | 0x25 | 83,968 | `0x8016313C` | stream | 11 | 5585 | 5587 | **2** | no | `6ca481a134fa5e28` |

**Fields per stage, `LoadLevel` (rows 19-25), which is the form §7 M3 asks for:** one read per
stage, stages 2, 3, 4 and 6 issued in the **same field 5573**, and stages 8, 9 and 11 at 5579, 5581
and 5585. Per-stage cost 0, 0, 0, 2, 2, 2, 2 fields. **Stage 12 issues no read at all**, which is
§2.3's walk-in alignment gate and is not an I/O wait — the ledger showing no operation there is the
positive control that the tap is on CD reads and not on stage transitions. A cutscene's seven reads
(rows 11-17) behave the same way: 0, 0, 2, 2, 2, 0, 0.

**The 2 fields are the guest's own call cadence, not a host cost and not R6.** A streaming read
completes at the next `cd_retry_step`, which happens when the guest next calls `CDLoadTime`; the
measured 2 is the gap between one loader call and the next, and the 0 rows are the calls that land
twice inside one field. Nothing in the table scales with the read size — 524,288 bytes cost 0
fields in row 12 and 2 fields in row 21, and 851,968 bytes cost 2 in row 15 — which is the
discriminator: a host cost proportional to bytes would order the table by size, and this one is not
ordered by size. That also retires §6 R1's arithmetic worry ("a whole level about 55 ms") as
unnecessary at the field granularity, though it does not measure sub-field host time.

**R6, the XA readiness gate: the bit was SET in 26 of 26 operations, including the ten issued
before the field counter exists and before any music is audible.** The gate's own bytes confirm the
address and the shape: `0x800153D8 lw $v0,0x74B4($v0)` with `$v0 = 0x80070000` from
`0x800153D8`'s `lui`, `0x800153E4 andi $v0,$v0,0x40`, `0x800153E8 beq $v0,$zero,+0x3F4` — a branch
AWAY to `0x800157E0`, inside stage 5's range, when the bit is CLEAR. So the bit reads as a
"CD music stream is active" flag and the loaders branch around the music-aware path when it is
clear; with music active every load took the music-aware path and it still cost at most 2 fields.
**What this does NOT establish is that the gate can never cost anything** — a constant reading
across 26 operations is what a permanently-set flag looks like and also what a wrong address looks
like, and the two are not separable from this measurement. R6 stays open with a named next step: a
run with the music stopped (attract demo, `PSXPORT_XA` silent) should show the bit clear at issue on
some operations, and if the latency column does not move, the gate is free.

**Nothing here is over a few fields, so the R6-sized fix in §6 (a prefetching reader in
`ArchiveTransfer`) is not justified by this measurement** and was not built.

### The `$ra - 8` failure mode, named because the site column looked fine

`$ra - 8` recovers the `jal` site only when the **caller** was guest code. 6 of the 26 operations
record an address outside the census: five are the port's own host dispatches (`ra` = `0xDEACFFF8`,
and the byte-identical payloads identify them as S02, S03, S04, S05 and S06 by the sizes §2.1
tabulated), and **one is a guest path that recorded `0x8002D4A4` — a real guest address, and the
exact trap the workspace map warns about: a lead that MATCHES when nothing should.** `0x8002D4A4`
is, per §2.1, a *caller of* `0x8005B7D8`, not the `jal 0x80016500` site (`0x8005B83C`, S06) inside
it, so the framework entered that override without setting `ra` to the intercepted call's return
address. The ledger's answer is to record the guest PC **beside** the site and to print
`outside the 0155 census` for anything it cannot name, which is why the coverage line reads
`16/31` for a route that in fact exercised 20 of the census's sites. The honest coverage number for
this route is therefore **20 of 31, 11 unreached, with 6 of the 20 unattributed by site**; the
instrument's own number is 16 and the difference is the six.

### Shown the other answer

`tests/test_load_ledger.cpp` (CTest `load_ledger`) drives the **real** `Ledger` with a stage machine
built to §2.3's shape. The positive leg completes every read in the field it was issued: 12
operations, every latency 0, and the report says `issuer sites exercised 2 of 31` and names
`S09@0x8002EEC4 UNREACHED`. The negative leg is the same machine with the completion **withheld** —
the guest never calls the retry step that dispatches `0x80016490` — and the machine stops after its
first streaming read: **2 operations, 1 pending**, and the stalled operation's latency is
`kPendingFields` (`~0ull`) rather than 0, because a report that printed 0 for an unfinished operation
would claim the removal is free. Also covered: a refusal that stays visible in the record (a route
that failed and a route that never ran are different reports), a measured 3-field latency, and an
operation issued before any field owner existed being excluded from the latency maximum.

**A live withheld run was NOT done, and the reason is the product, not the schedule:** withholding
it live needs a new conditional in the shipping completion path, i.e. a product flag that can stall
the game forever, in exchange for evidence the unit test already produces from the same class. If
that run is wanted, the honest form is a fault-injection knob owned next to the latch in
`ArchiveTransfer` and a drive that refuses on `pending > 0` rather than waiting 12,000 fields.

### What M1 and M2 would still need (not attempted) — SUPERSEDED 2026-10-01

**Both were run the same day; see "M1 and M2 measured, R6 decided" at the end of this issue.** What
follows is the M3 session's own record of what was missing, kept because the two gaps it names are
exactly the two the new section had to solve and it is worth seeing that neither was closed by
reading more code.

The ledger now records the payload multiset M1 compares — `(issuer site, LBA, length, destination,
SHA-256)` per operation, with the digest taken from the image identity rather than recomputed — and
carries the guest words M2's field list names, so both legs have their input. **Neither was run:**
M1 needs the oracle core with real CD timing, and M2 needs `tools/ram_compare.py` from the `skips`
worktree, and both are longer than this change. The one thing the table above already says about
M1 is that within one run every operation at the same site can be compared operation-by-operation
(row 9 and row 18 are the same PETE payload by digest, `b73da4852629d203`, read at boot end and
again at the cutscene end).

**Both gaps are now closed, and the run added what this section could not have predicted:** the
reference's guest boot issues the four reads the port dispatches itself, from S02-S05, with
byte-identical payloads, which independently confirms §2.1's size-based identification of them.

### The instruction-field form the census scan used (method note, and a trap)

Re-deriving §2's site list in this worktree found **zero** loader call sites under the plain MIPS
rule (`target = pc + 4 * imm`) and exactly **11 / 19 / 5** for `CDLoadSync` / `CDLoadAsync` /
`CDLoadTime` under `target = 0x80000000 + 4 * imm` (103,936 main-image words). The absolute form
reproduces every address §1 and §2 quote — `0x800165A8` → `0x80063C48` (CdControl), `0x800165E0` →
`0x80064094` (CdIntToPos), `0x80016758` → `0x8006606C` (CdRead), `0x80016584` → `0x800163E4` (the
Loop A `CDLoadTime` call), `0x80016594` → `0x8002BBE0` (CDMusicUpdate), `0x8001658C` → `0x8005637C`
(SoundsUpdate) — and 0 indirect references still holds. **So the ledger's site table is the census
re-measured, not transcribed, and it is the only tool in this issue that would have caught the
plain-relative reading silently reporting an empty corpus.** What that says about the image is not
settled here and is not claimed; what matters for the ledger is that the table's 31 addresses are
the ones the image actually contains, checked against the file itself.

## M1 and M2 measured, R6 decided (2026-10-01)

**The instrument: `tools/load_compare.py`** (`load_compare_selftest` in CTest), which drives the
title's own oracle route (`tools/oracle_spyro1.py`: save picker → `GS_Playing` → settled play →
the twelve gameplay segments, the same policy `tools/oracle_compare.py --policy artisans` runs) on
BOTH cores at once — the port through its REPL with `PSXPORT_LOAD_LEDGER` set, the reference through
the pinned full-console core — and then answers M1, M2 and R6 from what each core actually did. It
reuses `compare.py`/`compare_cores.py` for the driving and `tools/ram_compare.py` for the field-list
comparison; it forks neither. One run: **native 7,738 frames / console 8,778 fields, 15 checkpoints,
every decisive range MATCH, 88.9 s**, with the reference's CD loader entries read by the pinned
core's bounded PC observer (4 targets, 2 RAM ranges, 2,546,226,609 instructions scanned, 30 matches
on the loaders plus the R6 writers, **0 dropped, 0 pairing errors**).

### M1 — payload: **28 of 28 operations identical, including every SHA-256**

| | port leg | reference leg |
|---|---|---|
| operations | 28 (7 blocking, 21 streaming) | 28 (7 blocking, 21 streaming) |
| source | `LoadLedger` report (`PSXPORT_LOAD_LEDGER`) | PC-observer records at `0x80016500` / `0x80016698`, arguments `a0`/`a1`/`a2`/`a3` |
| payload digest | the digest `image_publication` already computed | **SHA-256 of the same disc sectors, computed by this tool from `WAD.WAD`** (110,260,224 B, sha256 `7ba8961c3626bcec`), never read from the port |

**Payload identity: 28 matched, 0 only on the port, 0 only on the reference, 0 refused.** Order
matched too (the table in `scratch/load_compare/analysis.txt` is row-for-row identical), which is a
stronger result than the multiset asks for and is not claimed as more than it is: the route is the
same on both cores, so the order is expected to be the same.

**§7's strict tuple differs in exactly one column, and the difference is the site's.** 22 of 28
match on the full `(site, LBA, offset, length, destination, SHA-256)` key; the other 6 are 6 port
rows and 6 reference rows with **byte-identical payloads**, and every one of them is a case §"the
`$ra - 8` failure mode" already predicted:

| # | port records | reference records (real guest `jal`) | payload (both) |
|---|---|---|---|
| 1 | site `0xDEACFFF8` (a host return address) | **S02 `0x80012924`** | `229efedc2014e753` |
| 2 | site `0xDEACFFF8` | **S03 `0x80012970`** | `2209002b8c4f8754` |
| 3 | site `0xDEACFFF8` | **S04 `0x80012994`** | `8ee66fe644407bf1` |
| 4 | site `0xDEACFFF8` | **S05 `0x800129C0`** | `6f82c9e7efacbe5f` |
| 5, 6 | site `0x8002D4A4` | **S06 `0x8005B83C`** (twice) | `b73da4852629d203` |

So the reference's own guest boot issues the four reads the port's `BootSequence::loadAssets`
dispatches, from the four sites the census names, and the bytes are the same bytes; and the PETE
read the port attributes to `0x8002D4A4` (a *caller* of the function containing the `jal`) is
`0x8005B83C`'s on the reference. **The census's identification of those five host-dispatched reads
by size is confirmed against a core that names them itself**, which is a check of §2.1 the census
could not have performed on its own. The six rows are reported, not excused: the tool's verdict is
the payload verdict and the site gap is printed as its own line, because §7's tuple includes a column
the port provably cannot fill in for a read its own native code issued.

**Coverage denominator, with the sites that were NOT reached named.** **23 of the census's 31 issuer
sites exercised on at least one core** — the port alone reaches 18 named sites (and 6 operations
whose site it cannot name), and the reference adds **S02-S05** (the four boot reads the port
dispatches itself) and **S06** (the PETE read). **8 unreached: S07 credits overlay, S08 pause-quit,
S09 game over, S10 demo end, S11 respawn, A17/A18 dragon-cutscene overflow, A19 demo-end
graphics** — every one of them a route this comparison does not take (it does not die, does not
pause-and-quit, does not reach the credits or the end of a demo, and crosses no second dragon cutscene).
Two addresses were reached that the census does not name at all: `0xDEACFFF8` (the port's host
dispatch) and `0x8002D4A4` (the PETE caller, already named in the M3 section as the sixth
unattributed row). The ledger's own coverage line for this route reads `18 of 31 ... 6 operation(s)
came from an address the census does not name`.

### M1's negative: the archive truncated by one sector makes the comparison report UNEQUAL

§7 asks for exactly this and it is implemented at the same place `tests/test_archive_transfer`
truncates: the payload source refuses a range whose sectors are not there rather than hashing a
prefix. Measured on the recorded legs with the archive truncated so the last four reads lose their
sectors (`--truncate-bytes 100000000`, leaving 10,260,224 of 110,260,224 bytes): **four
operations reported UNEQUAL with the byte range named** — `[0x97C800,0x9CA000)`, `[0x9CA000,
0x9DE800)`, `[0x9DE800,0xA2A000)`, `[0xA2A000,0xA37000)` — payload identity 24 matched / 4 only on
the port / 4 only on the reference, exit status 1. The exactly-one-sector form is in the selftest,
where the sectors before the truncation still hash identically and the ones past it are refused.

### M2 — terminal state: every hand-off field agrees at all four terminals

`tools/ram_compare.py`'s field list plus §7 M2's named fields (`g_LoadStage`, `g_Gamestate`,
`g_LevelId`, `g_LevelIndex`, the `g_Buffers` pointer block, `g_LevelHeader`, `g_CdMusic.m_Flags`, the
`0x80076BB8` gate) as hand-off fields, and the six clocks (`g_GameTick`, `g_LevelTicks`,
`g_DeltaTime`, `g_LevelTransTicks`, `g_UnprocessedFrames`, `D_800758B8`) reported separately. Each
core is dumped at ITS OWN terminal and the two dumps are compared.

| terminal (each core's own) | fields equal | HAND-OFF differing | clock differing | payload destination ranges byte-identical |
|---|---|---|---|---|
| LoadCutscene stage 10, #0 (boot title cutscene) | 17/19 | **0** | 2 (`g_LevelTicks`, `g_UnprocessedFrames`: 231 vs 435) | **28 of 28** |
| LoadCutscene stage 10, #1 (level-entry cutscene) | 18/19 | **0** | 1 (`g_LevelTicks` 2701 vs 2826) | 23 of 28 |
| LoadCutscene stage 10, #2 | 18/19 | **0** | 1 (5653 vs 6176) | 16 of 28 |
| LoadLevel leaving stage 13 (`g_LoadStage = -1`, store `0x800163B8`) | 18/19 | **0** | 1 (`g_LevelTicks` 0 vs 1) | 12 of 28 |

**Zero hand-off differences at every terminal: the load hands over the same state it hands over on
retail.** The clock deltas are the expected ones and are named: the port arrives earlier because it
spent no fields on the disc, and the boot cutscene's 231-vs-435 is that difference whole.

**The destination-range column needs its own sentence, because it is NOT a payload test except at
the first terminal, and the three-way print says why.** Each destination range was hashed in both
cores' RAM *and* against the payload on the medium. At the boot terminal — the only point where no
later read has re-used any of the addresses — **all 28 are byte-identical**. At the later terminals
the guest has consumed its own staging buffers on each side at its own rate: of the ranges that
differ, **both** cores have already moved past the payload in 32 of 33 cases across the three later
terminals, and in the single exception (A06's `0x8011593C` at cutscene terminal #2) the PORT still
holds the payload byte-for-byte while the reference has moved on. So the divergence is in what the
game did with the buffer afterwards, not in what the load delivered; the decisive payload evidence
is M1's 28 digests, and the first terminal's 28/28 identical ranges is the in-RAM confirmation of it.

### R6 — the XA-ready bit is **not** always set; the 26/26 was a property of WHEN reads are issued

The M3 section left this open with a named next step, and the next step settles it three ways.

**The bit is clear routinely, on both cores, in the same run.** Polled every field (the port through
its REPL, the reference through the console's RAM read):

| core | observations | bit SET | bit **CLEAR** | values seen |
|---|---|---|---|---|
| port | 7,738 | 5,506 | **2,232** | `0x10`×2212, `0x40`×5506, `0x100`×2, `0x200`×18 |
| reference | 8,778 | 5,337 | **3,441** | `0x00`×340, `0x10`×2277, `0x40`×5337, `0x100`×8, `0x200`×17, `0x88880000`×799 |

(The reference's `0x88880000` is a value **no writer in the image produces** — §"the CD primitives"
and `tools/writers.py 0x800774B4` together account for every store of that word, and they store
`0x40` or `0x100` — so it is whatever the word held before the game first wrote it. It is listed
rather than dropped, and it is the one value here this issue does not interpret.)

**And the writer that clears it EXECUTED, on the reference, in this route.** `tools/writers.py
0x800774B4` returns six immediate-form writers, and the values are not all the same: four store
`0x40` (bit 6 set) and **two store `0x100` (bit 6 CLEAR)** — `0x8002BF2C` (in `CDMusicUpdate`'s
CdControl arm, after `jal 0x80063C48`) and `0x8002BFC4` (a routine that sets `0x100` when
`flags & 0x200` is clear). Both were PC-observer targets: **`0x8002BF2C` was entered 2 times**,
`0x8002BFC4` 0 times. So the clear path is reachable, and it ran.

**But at a loader entry the bit was set in 28 of 28 on the reference as well**, and the observer
carries `0x800774B4` in every record, so that is measured at the same instant the read was issued
rather than polled around it: `{0x80016500: 7 set, 0 clear}, {0x80016698: 21 set, 0 clear}` — the
same 28/28 the port's own ledger column reported.

**The decision: the address is right, the bit is not permanently set, and the 26/26 was a sampling
property of when Spyro issues CD reads.** All three answers the question asked for are in hand: the
bit is observed CLEAR thousands of times on both cores; the instruction that clears it executed on
the reference; and no read on this route was issued while it was clear. What is therefore settled is
that R6 cannot cost anything on this route — a gate that is already open when every read is issued
costs nothing, and the M3 measurement (worst 2 fields, not ordered by byte size) is consistent with
that. What is NOT settled, and is not claimed, is that a route which issues a read while the bit is
clear also costs nothing. R6 stays open only in that narrower form, and the measurement that would
close it is a route with a music command in flight — not a defect in the loaders.
