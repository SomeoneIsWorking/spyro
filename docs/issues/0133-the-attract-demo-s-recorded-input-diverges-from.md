---
id: 133
title: The attract demo's recorded input diverges from the console at g_GameTick 556: player position offsets (+9,+51,-2) growing to (+59,+405,0), with the camera following
status: investigating
symptom: oracle compare on the no-input attract route holds 553 per-iteration comparisons then DIVERGE on player.position at tick 556; the level entry itself MATCHES every decisive range
tags: oracle,attract-demo,physics,divergence,level-entry
created: 2026-09-26
updated: 2026-09-27
---

## 2026-09-27: "the level overlay's update is never entered" is REFUTED — the store observer was armed on DATA addresses

### The premise, and why it looked solid

The sections below conclude that the level overlay's own update function is never entered, that
`func_80051FEC` never runs, and that the moby list at `0x800700F4` is therefore never filled. The
evidence offered was a validated store-observer report:

```
[store-observe] report: armed=yes targets=3 jit_instructions=116056872 ...
[store-observe]   [0] 0x800700F4 stores before=0 after=0 — MATCHED NONE of the 116056872 executed JIT instruction(s)
[store-observe]   [1] 0x80077868 ... [2] 0x80077870 ...
```

**Those are DATA addresses, and `PSXPORT_STORE_OBSERVE` matches the guest PC of a translated STORE
INSTRUCTION.** `LightrecExecutor::Impl::observeStore` compares `target.guestPc != guestPc`, so an
armed value is compared against the address the store instruction EXECUTES AT. A data address can
never equal a store instruction's PC, so the line above would print the same for a word that changes
every frame. It is a tautology, and it is the whole basis of the "never entered" claim. The framework
now says so in the knob's own help text (`config.cpp`: "these are the PCs OF store instructions, NOT
the guest words they write"), and its report row was changed to say `store PC` and to print
`EXECUTED: N store(s)` instead of echoing the armed value in a column that read like an address. The
earlier `test_handoff_store_observer` "other answer" is not a defence of this use: that test arms real
store PCs, so it shows the instrument works, not that a data address is a valid target.

### The dispatch, named with evidence

It is a plain function pointer, not a computed call and not a table of offsets.
`tools/probe_level_update_dispatch.py` derives all of it from `SCUS_942.28` and cross-checks it.

* **The global.** `g_UpdateMoby` = **`0x80075734`** (`external/spyro-1/asm/data/game.sbss.s:158`).
* **The writer.** `SetOverlayPointers`, a `switch (g_LevelId)` whose 100-entry jump table is at
  **`0x800113A4`** in the main image. There are exactly **43** `sw ..., 0x5734($at)` sites, one per
  implemented level, each preceded by the `lui`/`addiu` that materialises the value. The other 57
  level ids share the tail at `0x8005B6E0` (`g_Buffers.m_DiscCopyBuf = g_Buffers.m_CopyBuf`) and store
  nothing. Level 10's is **`0x8005A744`**, storing **`0x8007D9C8`**; level 11's is `0x8005A7DC`,
  storing `0x8007DA78`. **43 of 43 levels agree with `external/spyro-1/src/overlay_pointers.c`.**
* **The readers.** Five `lw` of `0x80075734` in the main image; four have a `jalr` on the loaded
  register within 8 instructions: `0x8002F47C` (in `func_8002F3E4`, the gamestate update's case 0),
  `0x80033AA4`, `0x80042EE8` (in `func_80041670`) and `0x8004A4A0` (in `func_8004A200`).
  `0x8002EAD8` loads it and does not call it.

The "guest address alone is not identity" concern does not apply here, and that is worth stating
rather than assuming: the writer is in the MAIN image, the values are the level overlays' fixed
addresses, and this is a data flow between two globals, not a call the framework resolves.

### It is entered, with denominators, and `$ra` names the caller

Live route (`tools/drive.py gameplay`, new game, `g_LevelId = 10`, `g_Gamestate = 0`), armed on the
STORE PCs the image actually contains — `0x80051FF8` is `func_80051FEC`'s unconditional first store
(`sw $zero, ($at)`, the clear loop over `D_80077868`, 4 iterations per call) and `0x8005205C` is its
near-path append (`sw $at, ($t6)`):

```
[store-observe] report: armed=yes store_pcs=2 jit_instructions=116056872 fallback_instructions=0 callback_lines=1112
[store-observe]   [0] store PC 0x80051FF8 EXECUTED: 280 store(s) observed at this PC
[store-observe]   [1] store PC 0x8005205C EXECUTED: 276 store(s) observed at this PC
```

**280 = 4 x 70**, and `g_GameTick` was 70: the filler ran once per update, every update. The
per-callback records carry `$ra = 0x8007D9F8`, which is the instruction after level 10's overlay
calls the filler — so the caller is named, not inferred. A second run armed the pointer writer and the
consumer too: `0x8005A744` (the switch's level-10 store) fired **1** time, `0x80033A6C`
(`g_GameTick++`, immediately before the `jalr $v0` at `0x80033AA4`) fired **90** times over
117,543,447 executed instructions, and `0x800523E8` — `func_800522C0`'s animation flush, reachable
ONLY inside the list walk — fired **368** times. The walk runs over a populated list. The one target
that never fired is `0x8002F478`, the gamestate-update path: this port does not go through
`func_8002F3E4`'s case 0, it goes through the main loop's own `jalr`. That is a fact about which path
is taken, not a defect.

The console was observed the same way at `g_GameTick` 41 on the demo route: `0x8005205C` recorded
**16** times with `ra = 0x8007DAA8`, level 11's overlay resume address. Both cores reach the filler
from the resident overlay.

### The list reads zero at a frame park, and the guest's own draw pass is why

`0x800700F4` is `D_8006FCF4 + 0x400`, and `D_8006FCF4` is the head of the region the guest clears in
its DRAW pass. `func_8002B9CC` (`0x8002B9CC`, read out of the image) is
`memset(0x8006FCF4, 0, 0x1C00)`: `jal 0x80016930` at `0x8002B9E0` with `$a0 = 0x8006FCF4` built at
`0x8002B9D0` and `$a2 = 0x1C00` in the delay slot. The list base is `+0x400` inside that region. This
repo reproduces it at `game/render/field_scene_recipe.cpp:62-67`
(`spyro::field_scene_recipe::applyEnvironment`), byte-wise over the same 0x1C00.

So a list read taken at a frame boundary is not an observation of the update. On this route it is
zero at every park sampled (four parks, 0 of 64 words each). The console's read at `g_GameTick` 41 is
NOT zero (`801A92E8, 801A92E8, 0, 0, 801A9130, ...`) while `g_GameTick` 40 is zero on both cores — and
the console was also observed executing `func_8002B9CC` inside the update ending at tick 41. **The two
parks are therefore not the same point in the frame.** Whether that is a product ordering difference or
a harness park-phase difference is named as UNKNOWN below, not resolved.

A sentinel write settles the product side on its own: `0x80077868` (which the filler clears
unconditionally) and `0x800700F4` were each written through the control channel and read back
immediately — both VISIBLE, so the channel and the RAM are fine — and both were zero again within one
field. The product's list really is filled and really is cleared, and the only host-side eraser of
that address is `applyEnvironment`, which mirrors the guest.

`PSXPORT_CW` over `0x8006FC00..0x80070600`, uncapped, names every writer: **196,860 byte stores of
zero** at `0x8006FCF4..0x800705FF` with `interp_pc=0xDEAD0000` (a host context, not guest execution)
and `$sp=0x801FFFF8`, i.e. exactly `applyEnvironment`'s 0x1C00-byte `mem_w8` loop clipped by the
watch band, in 85 complete passes. The same census shows 7,733 word stores of RAM pointers from
`interp_pc=0x8002BEA4` across `0x8006FCF8..0x80070230` — the guest writing moby pointers into this
region, which the native clear then erases.

### What is now known not to be the cause

The dispatch is a function pointer that is written correctly once, read correctly, and called. The
filler is entered once per update and appends. The consumer walks the filled list. The store-observer
zero was a tautology. Nothing in the framework's dispatch, invalidation or image identity is
implicated, and no native owner is missing or wrong.

### What is still UNKNOWN, named as unknown

1. **Whether the product's list CONTENT matches the console's during the update.** The append counts
   differ (about 4 per update on level 10 here; 16 in one console update on level 11), but the routes
   are different levels and the level-11 comparison has not been run on the product. The per-moby
   identity of the appended entries is unmeasured.
2. **Why the two parks differ.** Whether the product's native producers run at a different point in
   the frame than the guest's `func_8002B9CC`, or whether the harness parks the two cores at different
   points, is not established. It needs a park the two cores share, which this route does not have.
3. **Whether anything downstream of the list is wrong.** This section refutes "the update is never
   entered" and "the list is never filled". It does not establish that the product's list content
   equals the console's, and so it does not close the `Moby+0x42` / `g_DynMobyCount` observations the
   sections below rest on. Those remain open, on a premise that has since been corrected.

### The next measurement, and the instrument gap that produced this section

`tools/probe_level_update_dispatch.py` derives the dispatch from the image, checks the derivation
against the decompilation (43/43), reports the controls before the readings, and states that a park
is not a valid observation point instead of printing its zero as a finding. `--selftest` drives
nothing and requires both answers — a level id with a case body and one without — plus the derived
values that must agree with the decompilation.

The instrument gap that caused this whole section is now closed in the framework, so the next question
can be asked directly: read the list base and its contents **from inside the update**, on both cores, at
the same point. `PSXPORT_STORE_OBSERVE` can do it for the product (arm the filler's own store) and the
console's PC observer can do it for the reference, but the product has no PC observer, so the two
cores cannot yet be compared at a point inside the same update.

## 2026-09-27: the writer of `Moby+0x42` is NAMED, and the convergence with the spawner is REFUTED

### The earlier "zero stores cover 0x42" was a property of the search, not of the tree

`scratch/conv/store_scan.py` re-runs the search over all 513,543 instructions / 64,891 stores of
`external/spyro-1/asm` and prints, per byte of the moby header, how many stores of each width can
reach it. Column `0x42`:

```
  k         1B       2B       4B       8B
 40       1495       11      310        0
 41       1361       11      310        0
 42          0       10      310        0   <- the divergent byte
 43         11       10      310        0
```

**310 word stores reach `Moby+0x42`.** The earlier search looked for a direct `0x42` displacement and
so missed every `sw ..., 0x40(base)`, which covers 0x40..0x43.

### `Moby+0x42` is `m_AnimationFlags`, and its only writer in level 11 is `func_800522C0`

`func_800522C0` (asm/moby_lists.s:214) stages each moby's 8 animation bytes in D_MEM at `$t8` =
`0x1F800004` and flushes them with **two** word stores:

```
0x800523E0  lw   $at, 0x1F800004($t8)
0x800523E4  lw   $v0, 0x1F800008($t8)
0x800523E8  sw   $at, 0x40($t5)     ; writes Moby+0x42
0x800523EC  sw   $v0, 0x3c($t5)
```

and the second exit repeats it at **0x8005243C**. Both read out of the real image with
`tools/probe_guest_disasm.py` (words `4000A1AD` at both, offset check 62,183/62,183 agree). `$t5` is
the list entry, so `$t5` is the Moby. The scan's other non-stack candidates are `$sp` (270), the
cyclorama's own state (12) and moby *props* blocks — `0x8007F304 sw $s0, 0x40($s2)` is props+0x40
(`$s2 = m_Props`, set at `0x8007E0B8`).

The staged flags byte has exactly three writers, which is the whole lifecycle: `0x80052378`
(`ori $v0,$v0,0x1` at `0x80052374`) sets bit 0 on a frame boundary, `0x8005238C`/`0x80052394` sets
bit 1 when the animation runs past its last frame, and `0x8005242C` (`sb $zero, 2($t8)`) clears it on
the `beqz $at, 0x80052424` path where the moby has no frame left to advance. So the console's
`0x00/0x01/0x03` oscillation is that state machine, and `Moby[0x42] & 2` is its "animation finished"
bit — which is what the class-`0x71` handler at `0x80083858` polls.

**The writer allocates nothing and is not in the spawner's call chain.** The per-level constructor is
reached through the `g_SpawnMoby` function pointer (`0x800758CC`), and `func_800522C0` is called
directly by the moby update at `0x8007DAB4`. Issue 0133's hypothesis — that `+0x42` is a class/flags
byte whose writer is the spawner, which is where the missing allocations point — is **refuted**.

The moby header band is now read off the tree's own accesses, and the community header is wrong about
it: the image writes and dispatches on the halfword at **0x36** (`lh $v1, 0x36($s3)` at `0x8007DB30`;
`sh $s0, 0x36($s3)` in the per-level constructor), and 0x42 is a one-byte flag field. The header's
field ORDER is right, its offsets are not.

### The product RUNS the animation updater, and this moby's animation state is frozen

`--store-observe 0x800523E8,0x8005243C,0x800524D8` over 60 iterations (ticks 3..62), 271,544
store-observe lines, 3 armed targets, 0 fault lines:

| armed store PC | what it is | per-callback lines | stores |
|---|---|---|---|
| 0x800523E8 | animation flush, frame crossed | 32,398 | 16,199 |
| 0x8005243C | animation flush, no frame left | 238,738 | 119,369 |
| 0x800524D8 | `MobyAlloc`'s `g_DynMobyCount++` | 404 | 202 |

So `func_800522C0` executes on the product, tens of thousands of times. And `--read
0x80173B80:0x58 --read-at 3,40,41` on both cores says the class-`0x71` moby at `0x80173B80` (level
moby index 15; `0x36` = `0x0071`) is frozen on the product and moving on the console:

| tick | byte | native | console |
|---|---|---|---|
| 3 | 0x3C | 0x00 | 0x01 |
| 3 | 0x42 | 0x00 | 0x01 |
| 40 | 0x3D | 0x00 | 0x12 |
| 40 | 0x40 / 0x41 | 0x20 / 0x20 | 0x20 / 0x20 |
| 40 | 0x42 | 0x00 | 0x00 |
| 41 | 0x3C | 0x00 | 0x01 |
| 41 | 0x40 / 0x41 | 0x20 / 0x20 | 0x10 / 0x10 |
| 41 | 0x42 | 0x00 | 0x01 |

The product's 0x3C..0x43 is `00 00 20 20 00 00 FF` at all three ticks.

**That value is a proof, not a symptom.** The product's `0x40 + 0x41` = 32 + 32 = 64, and
`func_800522C0` computes `at = (moby[0x40] + (moby[0x41] >> $t6)) >> 6` with `beqz $at, 0x80052424`:
64 >> 6 = 1, so the updater would take the ADVANCE path and set bit 0 of the flags at `0x80052378`.
`0x42` is 0. **The updater therefore does not process this moby on the product**, even though it runs
on the product. What differs is the moby LIST both the updater and the level dispatch walk: the
NULL-terminated `Moby*` array at `D_8006FCF4 + 0x400` = `0x80070BF4`, which `func_800522C0` walks at
`0x800522E0` and the update walks at `0x8007DAF0`. That list is the next thing to read, on both cores.

### `g_DynMobyCount` does not get its increment from `MobyAlloc` — the premise was wrong

`g_DynMobyCount` (`0x800756A4`) and `g_MobyAllocPtr` (`0x8007573C`) first differ at `g_GameTick` 528
and in only 5 of 530 iterations, so the reference jumps 4 -> 15 in ONE update while the product goes
4 -> 5. The pool census at that tick: both pools start at `0x80177960`; the reference's cursor has
walked 15 slots, the product's 4; the reference's slots 4..13 are class `255/256/257` and LIVE, and
the product's slots 4..13 are the same LEVEL DATA untouched (`Moby[0x48] = 0xFE`, every other byte
equal, `m_Props` included). So the product never allocated them.

Armed on the console for exactly that update — `0x800524D8` (`MobyAlloc`'s count store), `0x800525E0`
(the release function `func_80052568`'s decrement), `0x8008772C` (`g_SpawnMoby`), and `0x8003FE7C` as
a positive control — the observer reports **scanned 680,953, matched 1, retained 1, dropped 0,
pairing_errors 0**:

| target | entries |
|---|---|
| 0x8003FE7C (positive control) | **1** — `instr=AC208AE0`, `at=80080000`, i.e. the `sw $zero` to `g_Spyro+0x88` |
| 0x800524D8 | 0 |
| 0x800525E0 | 0 |
| 0x8008772C | 0 |

and the record's RAM snapshot of `g_DynMobyCount` is `0x0000000F` = 15.

So the +11 did **not** pass through `func_800524C4` and did not go through `g_SpawnMoby`.
`g_DynMobyCount = 0` at `loaders.c:643` is the only other write in the tree. Issue 0133's line
"`g_DynMobyCount` is incremented once per call by `func_800524C4` … so the product made 10 FEWER
`MobyAlloc` calls" is therefore **not established**: the reference's increment came from code this
route has not identified, and the number of `MobyAlloc` calls is not what the counter measures.

### What the resident images contain, measured

`--find-instruction 0x800524C4` scans all 2,097,152 bytes of main RAM on both cores: **exactly three
`jal 0x800524C4` words, identical on both cores**, at `0x80030524`, `0x80030610` (both inside
`func_8002F3E4`, gamestates/update) and `0x80087750` — the last one inside the function `g_SpawnMoby`
points at, `0x8008772C`, **which the reconstructed tree does not carry** (`grep -rn 8008772C
external/spyro-1/asm` matches nothing). The 16 bytes before it are the constructor's prologue
(`sw $s5,0x50($sp)` / `s4,0x48` / `s3,0x44` / `s2,0x40`), which is level 20's shape exactly.

Three `func_800522C0` callers execute on the product, from `$ra`: `0x8007DAB8` (16,199 flush stores,
the update's `jal` at `0x8007DAB4`), `0x8007CFF8`, `0x8007B7EC` and `0x8007AEFC` — of which only
`0x8007DAB4` is in the reconstructed listing. **Guest address alone is not identity**: the product's
`MobyAlloc` store records carry `$ra` of `0x80082054 / 0x8008461C / 0x80087758 / 0x80086E04`, and
level 11's listing has `addiu $s1, $s1, %lo(g_CollisionNormal)` at `0x80082050`, so those 202 calls
were made by a DIFFERENT resident image at the same addresses, during the approach to the level. The
counter was reset to 0 by the level load (`loaders.c:643`) after them.

## ROOT CAUSE FOUND 2026-09-27: the product's moby list is EMPTY

**This is the divergence's cause, and it explains every symptom above it in one fact.**

Measured with `tools/probe_emitter_predicate.py`, both cores, at `g_GameTick` 41. The list base was
recovered from the guest itself rather than from the listing: `0x800522CC` is `addi $t9, $a0, 0x0`, the
second instruction of `func_800522C0`, and its observer record carries `a0=800700F4` with
`ra=8007DABC` and `v1=80077888` — the list cursor. (`0x800700F4` is `D_8006FCF4 + 0x400`, so the listing's
constant was right and an earlier transcription of it as `0x80070BF4` was an arithmetic slip, not a
different address.)

Reading 192 bytes from that base on both cores:

```
RANGE 0x800700F4..0x800701B4 at g_GameTick 41
  +0x00  native 00000000   console 801A92E8
  +0x04  native 00000000   console 801A92E8
  +0x10  native 00000000   console 801A9130
  +0x14  native 00000000   console 801A9130
  ... every word: native 00000000
```

**The product's list is entirely zero. The console's holds real moby pointers, and every pointer the
product has is one the console has too — the product simply has none.** The node shape is two pointers
followed by a NULL terminator, and the console reaches its terminator where the product has been zero
since before the first sample.

That single fact accounts for the whole chain, and each link was previously measured in isolation:

- **`Moby+0x42` is never set** because the writer is `func_800522C0`'s animation state machine
  (`sw $at, 0x40($t5)` at `0x800523E8` and `0x8005243C`), which stages the flags byte in scratchpad
  `0x1F800004` from three writers — `0x80052378` (bit 0, a frame boundary), `0x8005238C/94` (bit 1, ran
  past the last frame) and `0x8005242C` (clear). The console's `0x00/0x01/0x03` IS that state machine and
  `& 2` is the "animation finished" bit `0x80083858` polls. **A moby that is not in the list is never
  animated**, so its byte stays 0 forever. That is the tick-41 `rand()` divergence: the console fires
  `jal RandRange` twice because two of its mobies are animated, the product zero times.
- **`g_DynMobyCount` 5 against 15** is not a `MobyAlloc` call count at all — a positive-controlled
  observer proved the console's `+11` never passes through `0x800524D8`. It is the size of a list the
  product never filled.
- **The frozen animation bytes on moby `0x80173B80`** (`0x3C..0x43` identical at ticks 3, 40 and 41 while
  the console's move every tick) are the same fact seen from inside one moby.
- **The updater demonstrably runs on the product** — `0x800523E8` fired 16,199 times — so this is NOT
  "the moby update never executes". The walk runs; it has nothing to walk.

**What is NOT the cause, now measured rather than argued:** the class-0x71 handler never runs (it does),
`0x80037EA0`'s emitter `func_80039AA8` has no caller in level 11 at all, and `Moby+0x42` is not a class
byte — the image dispatches on the halfword at `0x36` (`lh $v1, 0x36($s3)` at `0x8007DB30`), so
`external/spyro-1/include/moby.h`'s `m_Class` at `0x42` is a header offset error, not evidence.

## ROOT CAUSE, MEASURED END TO END 2026-09-27: the product never runs the code that FILLS the list

The previous section found the list empty and named the missing scan. The scan is no longer the next
step: the writer has been found, read out of the listing, and then **shown not to execute on the
product at all**.

### 1. The list's shape, from the image

`func_800522C0` reads it as one pointer per word, stride 4, NULL-terminated:

```asm
addi $t9, $a0, 0x0
L800522E0: lw $t5, 0x0($t9) ; addi $t9, $t9, 0x4 ; beqz $t5, .L80052448
```

(An earlier version of `tools/probe_moby_list.py` assumed a two-word node and would have counted the
reference's `[801A92E8, 801A92E8]` as one node instead of two. The stride is pinned by a selftest case
whose expected count a stride-8 reader cannot produce.)

### 2. The list is not the only consumer, and BOTH consumers bail on the first word

`func_level_11_8007DA78` is the level's own update, and at `0x8007DAE8` it does the same thing:

```asm
lui    $t1, %hi(D_8006FCF4 + 0x400) ; addiu $t1, $t1, %lo(...)
lw     $s3, 0x0($t1) ; addiu $t1, $t1, 0x4 ; beqz $s3, .Llevel_11_800876F8
```

**So on the product no moby is ever updated, in any of these paths.** That is the whole defect in one
instruction, and it retro-explains every word the earlier passes found diverging, because all of them
are written inside the loop that `beqz` skips: `Moby+0x42` (`lbu $v0, 0x42($s3)` at `0x8007DB24`),
`D_80075794` (`0x8007DB3C`), `D_800756C4` (`0x8007DB48`), `D_800757F4` (`0x8007DB1C`) — and
`D_80075794` is the word the earlier widening pass independently found diverging at tick 49.

### 3. The writer, and its gates

`func_80051FEC` (`asm/moby_lists.s`) is the only writer. It walks `g_LevelMobys` and appends:

```asm
lw   $at, 0x0($at)                       ; 0x80052010  $at = *g_LevelMobys
lui  $t6, %hi(D_8006FCF4) ; addiu ...     ; 0x80052030  $t6 = D_8006FCF4
addi $t6, $t6, 0x400                     ; 0x80052038  $t6 = 0x800700F4  THE LIST
addi $at, $at, -0x58                     ; 0x8005203C  one Moby before...
addi $at, $at, 0x58                      ; 0x80052040  ...so the first step lands on it. Stride 0x58.
lb   $v0, 0x48($at) ; bltz $v0, .L8005212C ; 0x80052044  terminator is +0x48 == -1
lb   $v1, 0x51($at) ; beqz $v1, .L8005207C ; 0x80052048  +0x51 == 0 selects the NEAR path
sw   $at, 0x0($t6)                       ; 0x8005205C  APPEND (near)
sw   $at, 0x0($t6)                       ; 0x8005210C  APPEND (far, after a GTE SQR/MAC test)
```

### 4. The input is identical, so the difference is the code not running

Two-core read at `g_GameTick` 41, `g_LevelMobys = 0x80173658` **on both cores**, and the gate bytes of
three consecutive mobies byte-identical:

```
RANGE 0x80173698..0x801736B0   +0x48 = 0x0E (positive)   +0x51 = 0x00
       native == console, every word, three mobies
```

`+0x51 == 0` takes `beqz` at `0x80052054` straight into the near-path append at `0x8005205C`. **On
identical input both cores should append, and the reference does** (its list holds `801A92E8`,
`801A9130` at tick 41). So the moby data is not the defect and the gate logic is not the defect.

### 5. The product never executes that store — with a denominator, and the instrument validated

`PSXPORT_STORE_OBSERVE=800700F4,800700F8,800700FC` over a real live-play run to `GS_Playing`:

```
[store-observe] report: armed=yes targets=3 jit_instructions=116056872 fallback_instructions=0 callback_lines=0
[store-observe]   [0] 0x800700F4 stores before=0 after=0 — MATCHED NONE of the 116056872 executed JIT instruction(s)
```

**116,056,872 executed JIT instructions, and not one translated store to the list base.** The observer
is not trusted on this alone: `test_handoff_store_observer` shows it reporting the other answer
(`before=1 after=1 last_guest_pc=0x80013698` and three more), so the negative is a measured absence
rather than an instrument that cannot see.

This also holds in the LIVE route, not only the attract demo. `tools/probe_moby_list.py` drives to
`GS_Playing` and reads the list there: **0 nodes**, with its controls passing (`g_Gamestate = 0`,
`kLevelMobys = 0x8016D3E8`, a RAM pointer whose array holds 15 non-zero words). So the level loads
its mobies and the step that walks them into the update list does not happen — in the game a player
plays, not only in the demo the oracle replays.

**The consequence for the player is the whole point:** the level's static geometry and Spyro render
(see `tools/live_play.py`, a real 25-second run), but nothing in the moby list animates, updates or
behaves, because every consumer tests the first word and finds zero.

### 6. What is still unknown, stated rather than guessed

**Which** guest path fails to reach `0x8005205C` is not yet established. The honest possibilities, none
yet chosen between: the level-11 overlay's caller at `0x8007DAA0` does not run on the product; or
`func_80051FEC` runs and leaves before the append; or the append happens and something zeroes the list
afterwards. The store observer rules out only the last of those for these three addresses. Naming which
of the first two it is needs the same instrument pointed at a store the filler is known to make — the
clear loop at `0x80051FF8` (`sw $zero, 0($at)`, over `D_80077868`, stride 8) is the right next target
because it is unconditional, so a zero there means the function never started.

## THE MOBY LIST IS NOT THE DEFECT — the previous section is WRONG, and here is what it got wrong

Commit `27acd14` in this file claimed the list's defect is "four bytes wide": that the filler's first append
is `0x800700F8` while the consumer is handed `0x800700F4`. **Both halves of that are wrong, and the image
refutes it directly.**

**1. The base is `0x800700F4`, so there is no disagreement.** Disassembled from the image, not the listing:

```
0x80052030  07800E3C  lui    $t6, 0x8007
0x80052034  F4FCCE25  addiu  $t6, $t6, -0x30c      -> 0x8006FCF4
0x80052038  0004CE21  addi   $t6, $t6, 0x400       -> 0x800700F4
```

`0x8007_0000 - 0x30C + 0x400 = 0x800700F4`. The filler's cursor starts exactly where the consumer is
pointed, and both were read from the same guest code. The `0x800700F8` in that commit's log line is a
**LATER append**: the cursor advances 4 per append on the `bltz` path at `0x80052064`, and the first line I
quoted was misattributed to the first observation. So the "four-byte disagreement between the function that
writes the list and the function that reads it" is an artefact of reading one line of a report as a summary.

**2. The list is not empty at a frame park, because the guest clears it every frame.** Also from the image:

```
0x8002B9D0  lui    $a0, 0x8007
0x8002B9D4  addiu  $a0, $a0, -0x30c      -> 0x8006FCF4
0x8002B9D8  move   $a1, $zero
0x8002B9E0  jal    0x80016930            -> memset
0x8002B9E4  addiu  $a2, $zero, 0x1c00    -> 0x1C00 bytes
```

`memset(0x8006FCF4, 0, 0x1C00)` covers `0x8006FCF4 .. 0x80078EF4`, **which contains the list**. The list is
built and consumed WITHIN one update and cleared before the next, so a probe that reads it while the frame
loop is parked reads the clear. **Every "the list is empty" reading in this issue, including
`tools/probe_moby_list.py`'s verdict, was taken at exactly that moment and is evidence of nothing.** A park
is not an observation point for a per-frame scratch list; that is now stated in the probe rather than
discovered a third time.

**3. The decisive refutation, and it needs no arithmetic at all.** The consumer's own store at `0x800523E8`
— reachable only on a NON-NULL entry, since `0x800522E8` is `beqz $t5` — fired **284 times**, and its first
write was `0x8016D690`, i.e. moby `0x8016D650`, a real main-RAM moby. **If the list's first slot were an
unwritten hole the walk would have exited immediately and the consumer would have fired ZERO times.** It
fired more often (284) than the filler appended (276), so the walk processes every appended moby.

**So there is no moby-list defect.** The list is filled, walked, and cleared, exactly as retail does. What
this investigation actually established is the three facts worth keeping:

- `func_80051FEC` (the filler) runs — 280 executions of its unconditional clear, 276 near-path appends, with
  resolved destinations walking `0x800700F4` upward four bytes at a time.
- `func_800522C0` (the animation pass) runs — 284 executions, first resolved write a real moby pointer.
- The level-10 update is reached through `g_UpdateMoby` (`0x80075734`), written by `SetOverlayPointers` via a
  100-entry jump table at `0x800113A4`; level 10's is `0x8005A744 -> 0x8007D9C8`, level 11's is
  `0x8005A7DC -> 0x8007DA78`, and `$ra` at the filler's call site is `0x8007D9F8`, inside level 10's overlay.

**The "WAD images reuse one load address" hypothesis is refuted, and that is measured**: the writer is in the
main image and the values are the overlays' fixed addresses. See `tools/probe_level_update_dispatch.py`.

### The pattern in this file, stated once so it stops recurring

Three claims in this issue were wrong, and all three came from the same move: **reading a value at a moment
when it does not hold, or trusting a derivation that had not been checked against an independent oracle.**

| claim | what was wrong |
|---|---|
| "the filler never executes" | armed DATA addresses on a PC-matching observer — a guaranteed answer, given a 116M denominator |
| "the list is empty" | read a per-frame scratch list while the frame loop was parked, i.e. after the guest's own `memset` |
| "the defect is four bytes wide" | read one line of a report as a summary; the cursor had already advanced |

The instruments were not the problem in the first two cases — the store observer reported faithfully both
times. **The measurement was.** A per-frame list needs a per-frame observer, and a control run with no
observer is what proves the instrument is not the cause.

## ROOT CAUSE, CORRECTED 2026-09-27: the filler DOES run and DOES append — to the wrong slot

**Everything above this line is wrong about one thing, and the correction is a measurement.**

`PSXPORT_STORE_OBSERVE` matches the PC OF each executed translated store, not a guest data address. Every
run above armed DATA addresses (`0x800700F4`, `0x80077868`) and read "MATCHED NONE" over 116,056,872
executed JIT instructions as proof the filler never ran. **`0x800700F4` is not an instruction, so no store
instruction can be AT it: that answer was guaranteed before the game started.** The 116-million denominator
is what made a tautology read as evidence. Two commits and one findings document carried that claim before
it was caught; see `psxport/docs/findings/diagnostics-that-cannot-lie.md`.

Armed on real store PCs, with the instrument's own abort defect fixed first (it was killing the product for
being watched — `psxport` commit `17dcc33e`):

```
[0] store PC 0x80051FF8 EXECUTED: 280 store(s)     <- the filler's UNCONDITIONAL clear
[1] store PC 0x8005205C EXECUTED: 276 store(s)     <- the near-path append,  sw $at, 0($t6)
        last write:  address=0x800701C8 value=0x8016F228 (base $r14, disp +0, from $r1);
                     275 further address change(s) observed
        first write: address=0x800700F8 value=0x8016D6A8 — THIS STORE MOVED
[2] store PC 0x800523E8 EXECUTED: 284 store(s)     <- the animation pass, func_800522C0
```

**So the filler runs, appends ~117 moby pointers, and the animation pass runs 284 times.** The earlier
"the filler never executes" is refuted outright.

### The defect, as measured

**The filler's first append is `0x800700F8`. The consumer is handed `0x800700F4`.** One slot — four bytes.
`func_800522C0` does `lw $t5, 0($t9)` on the address it is given, and `0x800700F4` is a slot nobody ever
writes, so it reads zero and `beqz` exits before walking anything. The list is full; the walk starts one
slot too early and stops on a hole.

That is the whole reason no moby ever animates or updates, and it is a four-byte disagreement between the
function that writes the list and the function that reads it.

### And the walk is unbounded, which is a second, independent fact

`tools/probe_moby_list.py` reports, over the live level's own moby array:

```
NO TERMINATOR in the first 64 mobies: +0x48 never reached -1, so the filler's own walk would run off the
end of the array
```

`func_80051FEC` terminates on `moby[+0x48] == -1` (`0x8005212C`: `addi $v1, $zero, -0x1` / `bne $v1, $v0,
.L80052040`). If no moby in 64 satisfies that, the walk has no stop and appends pointers from whatever memory
follows — which is what ~117 appends spanning `0x800700F8`..`0x800701C8` looks like. So either the
terminator field is not at `+0x48`, or its value is not `-1`, and **that is now the open question**, with
the instrument that can answer it.

**Read the offsets as unverified.** This file asserted `+0x48`, `+0x51`, `+0x43`, `+0x52` from the listing
alone. The same class of error already produced two false claims in this issue (`Moby+0x42` is not a class
byte — the image dispatches on the halfword at `0x36`; and `m_Class` in `include/moby.h` is a header offset
error). The gate fields must be confirmed against the IMAGE before the walk is judged, exactly as the store
sites were.

### The next two steps, both single commands

1. **Confirm the four-byte disagreement from the guest, not from us.** The filler computes `$t6` as
   `D_8006FCF4 + 0x400` at `0x80052038` and the consumer is entered with `$a0` from the level's caller
   (`0x8007DAB0`: `addu $a0, $s0, $zero`, with `$s0` set at `0x8007DAA8`). Read both and see which of
   `0x800700F4` and `0x800700F8` each side really uses. `PSXPORT_STORE_OBSERVE` now reports the resolved
   destination of every observed store, so one run answers it.
2. **Find the terminator.** The filler ran ~70 times (280 clear executions / 4 slots per call) and appended
   ~117 entries per run. Either the terminator field's offset is wrong or the walk is genuinely unbounded in
   retail too — and "retail is also unbounded" is a real possibility that this evidence does not exclude,
   because the reference's own list is only 2 entries at tick 41 of the DEMO route, a different level.

### The next step, and it is not a guess

`--find-instruction 0x800700F4` over all 2,097,152 bytes of both cores' main RAM finds **0 `jal` and 0
big-endian data pointers on either core**, so the list is not reached by a direct call and not through a
pointer table — it is loaded GP-relative (`%hi(D_8006FCF4) + 0x400`), which is why an absolute-pointer
scan cannot see it. Naming the writer needs a scan for **stores at displacement `+0x400` off a `$gp`
base**, which no instrument here does yet. That scan, over the reconstructed listing and then over the
image, is the one thing that turns this into a located write.

## THE LEAD IS EARLIER STILL: `Moby+0x42` already differs at the FIRST sample (tick 3)

Measured 2026-09-27 with `tools/probe_emitter_predicate.py`. **One byte**, on the two class-`0x71`
mobies `0x80173B80` and `0x80173BD8`:

| tick | `Moby+0x42` product / console |
|---|---|
| 40 | `0x00` / `0x00` — agree |
| 41 | **`0x00` / `0x03`** |

The product's value is `0x00` at all eight sampled ticks (3, 10, 20, 30, 38, 39, 40, 41) while the
console's oscillates `0x00/0x01/0x03`. **The product never sets it.** And it already differs at the
FIRST sample of the window, on 7 mobies (2 class-`0x71`, 5 class-10), product `0x00` / console `0x01`.

So the tick-41 `rand()` divergence — 25 calls on the product against 27 on the console, with 37 updates
before it agreeing exactly — is **not the first divergence in absolute terms**, and the cause of
`Moby+0x42` predates this probe's window. Two cores reach `demo_playing` at the same `g_GameTick` 3
but by different field counts (5928 vs 4319 advances), so the window's start is not a common state and
the existing harness cannot step both cores back before tick 3.

### The site, and a gate that is not a gate

`jal RandRange` at **`0x8008387C`**, inside level 11's class-`0x71` handler (reached from
`beq $v1,$v0(0x71)` at `func_level_11_8007DA78.s:250-251`). Its full predicate, with `Moby[0x48] == 0`
and `m_Props[0] == 0` holding on both cores at tick 40:

```
0x80083858  lw    $v0, D_80075794
0x80083860  beqz  $v0, 0x80083910      -> (Moby[0x42] & 2) != 0
0x80083868  lw    $v0, 0x0($s0)        (s0 = Moby->m_Props, 0x80083740)
0x80083870  bnez  $v0, 0x800838C8       -> m_Props[0] == 0
```

**`D_80075794` is not a global gate.** It is rewritten *per moby* at `0x8007DB3C` as `Moby[0x42] & 2`
(`andi $a0,$v0,0x2` at `0x8007DB34`), so reading it at a park measures the LAST live moby, not the one
being asked about. This is why the predicate has to be evaluated per moby from the pool on both cores —
195 pool slots read per core, 193 live.

### The previous attribution to `func_80039AA8` is REFUTED

The console's PC observer at `0x80037EA0` (`RandRange`, body read out of the image) recorded
`ra=80083884` on **both** records — one site, `0x8008387C`, `a0=3 a1=6` both times. The observer armed on
`0x80039AA8` itself recorded **`entries=0`**: the emitter was not executed at all in that update. And a
full 2 MiB main-RAM scan of both cores for `jal 0x80039AA8` and for a big-endian `0x80039AA8` data
pointer found **0 of each on both cores**. Level 11 has no caller for it at all.

The two calls are separated by moby, read from the console's own registers at the latch test
(`0x80083870`, where `a0` is the delay slot's `addu $a0,$s3,$zero`): `a0=80173B80` and `a0=80173BD8`.
Full decision chain for update 41: visit 1 `v0=2` moby `0x80173B80` `m_Props[0]=0` → call; visit 2
`v0=2` moby `0x80173BD8` `m_Props[0]=0` → call; visit 3 `v0=0` → `beqz` taken, no call.
`dropped=0 pairing_errors=0` over 9 records.

The console's post-call `m_Props[0] = 3` is the **return value** of `RandRange(3,6)`: `0x80083884
sw $v0,0($s0)` stores `v0`, which the call has just overwritten — the `addiu $v0,$v0,-1` in the branch
delay slot is dead. So the latch is "fire once from 0, then freeze at a random 3..6", and the console's
`3` is direct evidence that the two calls happened.

### Who writes `Moby+0x42`: no available instrument can name it

A scripted search of the whole reconstructed tree (`external/spyro-1/asm`, 429,885 overlay instructions)
finds **abundant** `sb`/`sh`/`sw` to `Moby+0x40` and `Moby+0x41` (268 `sb $zero,0x40($s3)`, 231
`sb $v1,0x41($a0)`, …) and **zero stores of any width covering `Moby+0x42`**. The listing only ever
*reads* it: `lbu $v0, 0x42($s3)` at `0x8007DB24` and `0x8007DB40` in level 11 and at the same offset in
ten other overlays. So the writer is in a function the decomp does not carry — exactly as `0x80037EBC`
was. And the product **executes** the site's latch store at other ticks, so this is not "the class-`0x71`
handler never runs on the product": it is that whatever sets `Moby+0x42` never runs there.

`PSXPORT_STORE_OBSERVE` cannot close this: it matches store **PCs**, so it can only confirm a PC already
known. `PSXPORT_CW` sees host-side stores only. The console observer needs a PC up front.

### The store observer fires, and its silence is ambiguous

It works: 24,332 per-callback lines, split exactly `0x8007DB3C x12166` and `0x8007DB54 x12166`
(before+after), with both positive controls — `sw $a0, D_80075794` and `sw $v0, D_800757F4`, executed
once per live moby per update — firing.

But it also **killed the product**: `[executor:error] frame-update required a completed guest call, but
execution exited as fault at 0x80083884 after 21662 cycles: Lightrec selected-store observer rejected
unsupported translated PC` → `abort()`, REPL exit 139. So **a missing callback line is ambiguous
between "the store never happened" and "the instrument refused it"**, separable only by reading the
fault line. That is why `probe_tick_divergence.py` now prints it as a warning rather than as a zero.

### The rand-count oracle does NOT agree, and no fix was made

Over 400 updates, bit-identical to the previous run: 399/399 updates had a bounded LCG path on both
cores with 0 unexplained, and **338/399 updates MISMATCH** (26,356 product calls against 26,422
console). First still-mismatching update is the one ending at `g_GameTick` 41. The oracle is the
measurement here because nothing was changed.

**What would prove a fix:** `tools/probe_tick_divergence.py --ticks 400 --rand-calls` agreeing on the
per-update `rand()` count for 400 consecutive updates, with 0 unexplained and 0 mismatching. Nothing
weaker counts — the seed chain needs no observer and no product edit.

### What is now known not to be the cause

- `D_800757F4` (`0x800757F4`, native 0 / console 1) is *written from* the divergent byte —
  `sw $v0, D_800757F4` at `0x8007DB54` with `andi $v0,$v0,0x1` at `0x8007DB4C` — so it is downstream of
  `Moby+0x42`, not upstream, and it flips sign between samples, making it a per-frame residue of
  whichever moby is processed last. The site's own gate reads the per-moby bit directly
  (`0x8007DB34/3C`), so it cannot be the site's input.
- `D_80075858` / `D_800758FC` (both 6 vs 4) are the **cyclorama scroll offsets**: `func_80050BD0`
  advances both by 2, wraps one mod `0x1000`, and clamps. They are a call-count residue of the field
  cadence, in a function that touches no Moby and calls no `rand`.
- `dist2d` / `OctDistance` are faithful — 5 of 5 listing-derived values match, including both asymmetric
  arms a swapped transcription gets wrong.

## The store lead was a RED HERRING, and the real lead is 28 ticks EARLIER

Corrected 2026-09-27. Three measurements dissolve the earlier lead, and the reason the earlier lead
looked solid is worth recording: **`--ram-diff` is ADDRESS-sorted, so "the first word to differ" carried
no temporal information at all.** The per-word `FIRST AT` ticks, which do, put two divergences that
PERSIST to the end of the window 28 ticks before the movement one:

- tick 311 — `D_80075788` 350 native vs 174 console
- **tick 528 — `g_DynMobyCount` (`0x800756A4`) = 5 native vs 15 console, with `g_MobyAllocPtr`
  (`0x8007573C`)**

`g_DynMobyCount` is incremented once per call by `func_800524C4` (`asm/42CC4.s:12`) = `MobyAlloc`,
which also advances `g_MobyAllocPtr`. **The product made 10 FEWER `MobyAlloc` calls** — a moby
population difference that feeds the very routine writing the diverging word. This is territory an
earlier pass declared eliminated, and that declaration was wrong.

About the store itself, three facts:

- **No static guest store of `1` to `0x80078AE0` exists.** A sweep of 200 files / 516,085
  instructions / 65,086 stores found exactly ONE store reaching that word: `0x8003FE7C`, storing
  `$zero`. A per-function sweep of everything writing into `g_Spyro` agrees — only `func_8003FE40`
  touches `+0x88`. The value arrives through a **pointer parameter**, the class the scan documents
  as un-attributable. "The decompiled tree contains no store of 1 to it" was TRUE and MISLEADING.
- It is a real write, not a skipped clear. The per-word mode shows the word AGREEING at 555
  (`00000000` both) and differing at 556 (`00000001` native, `00000000` console).
- The writer is a **guest** instruction: `PSXPORT_CW` on `0x80078AE0` logged 38 host-reaching
  stores, 35 of them `=00000001`, and `PSXPORT_CW_BT=1` gives
  `Core::cw_check_slow -> Core::writeGuestMemory<uint32_t> -> lightrec_rw -> Lightrec block thunk`.
  Statically that is `func_8004E3C8` (the `g_MobyCollisionChain` walker, which sets `$gp = $a0` at
  `0x8004E424`): `0x8004EB4C  sw $s7, 0x00($gp)`, called from `func_8003FE40` at `0x80040810` with
  `$a0 = $s2` built as `g_Spyro+0x88` at `0x8004075C`/`0x8004079C`. The console's
  `0x8003FE7C`/`AC208AE0` is not an alternative to this — the console ran the clear AND this
  function; `$s7` simply came out different.

**So the divergence is NOT the store and NOT a native override** — no override exists for
`func_8004E3C8` or `func_8003FE40`, and the writer is guest code. A count difference means fewer
CALLS, so the defect is upstream in the spawner's control flow, not in `MobyAlloc`. Next step:
per-tick `g_DynMobyCount` on both cores plus a product PC observation at the spawn call site.

### A framework belief that was measured false, and cost time

This investigation was stalled for a session on the belief that `PSXPORT_CW` "sees host-side stores
only" and "cannot attribute a guest-executed one". **That is false**, and it is the likeliest reason
the instrument holding the answer was set aside: `cw` fires for guest stores Lightrec routes through
`lightrec_rw`, which re-enters `Core::writeGuestMemory`, and that is exactly how the writer above was
found. What is true is narrower: most stores to a hot word are inlined by Lightrec and never reach the
host, so the watch is UNBIASED TOWARD THE SLOW PATH and a quiet watch means "no store took the slow
path", not "no store happened".

**`PSXPORT_STORE_OBSERVE` is not the unbiased instrument this paragraph once claimed, and psxport
`9bd4e9a8` corrects both the framework's documentation and this line.** It matches the guest PC of a
translated STORE — `LightrecExecutor::Impl::observeStore` compares `target.guestPc != guestPc` — so it
answers "what does this store write, and with what registers", NOT "which instruction wrote this word".
Arming a DATA address therefore matches nothing. That is not a subtlety: the per-target report row
echoes the armed value in a column that reads like an address. Measured on Mega Man X4, arming the data
address `0x80078AE0` produced `MATCHED NONE of the 9,366,306 executed JIT instruction(s)` on a word
that demonstrably changes every frame, while arming two real store PCs here produced 24,332 correctly
attributed lines. **To find which instruction wrote a word you must already know the instruction.** The
same commit fixes a use-after-scope in the observer's callback context that made every observation
increment a dead stack object — the first callback printed `seen=140723098021161`, which a
freshly-zeroed local cannot produce.

### Known gaps in the current evidence

- `SPYRO_FIELDS` in `tools/probe_tick_divergence.py` MISLABELS offsets: it calls `0x88`
  `m_floorIdleTime` and `0x8C/0x90/0x94` `m_airTime`/…, while the value evidence and the header's own
  `// 0x8c` comment say `0x8C` is `m_previousPosition` and `0x88` is `m_touchingMoby`. Not rewritten:
  the header, the port's render offsets and the table disagree across the `0x28-0x40` band and guessing
  would be worse than flagging it.
- 50,935 of 524,288 RAM words already differed at tick 555, mostly transient `0x801C05xx-0x801C09xx`.
  The watch set is 556 words, so an earlier PERSISTENT divergence outside it is not excluded.
- `interp_pc` is Core's CACHED pc at the last executor boundary, not the faulting pc. `0x8004EB4C` was
  identified statically and the backtrace confirmed, but the product's pc at the store was not observed.
- Why this hot per-frame main-RAM store takes Lightrec's slow path at all is unexplained.
  `0x80078AE0` masks into main RAM, which Lightrec normally inlines. Observation only.

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

## Per-word localisation (2026-09-26, `tools/probe_tick_divergence.py`)

The next measurement named above was made. The instrument is `tools/probe_tick_divergence.py`: it
drives both cores with the ROUTE's own predicates and advance, steps them in lockstep, and records
for every watched guest word the first iteration at which it differs, so the answer is the smallest
such tick read off a table rather than argued. `--ram-diff FIRST:LAST` widens the watch set to ALL
of main RAM (524,288 words per core).

Windows and denominators, each from one run:

| scan | what it compared | result |
|---|---|---|
| 580 iterations, ticks 3..582 | 540-word watch set, 310,880 word comparisons | 433 words equal on every iteration; 7 words already differed on the first sample; the rest first differed later |
| full RAM at ticks 555 and 556 | 524,288 words per core | 50,935 already differed at 555, 50,869 of those still differ at 556, and **107 words agreed at 555 and differ at 556** |
| 560 iterations, ticks 3..562 | the 107-word set re-checked | the same tick, the same values |

### The first guest word to differ

Not `player.position`. The full-RAM diff puts the collision raycast's own output first, in the order
the update produces it:

| address | symbol | product | console | delta |
|---|---|---|---|---|
| `0x80076B80` | `g_CollisionPoint` (`game.bss.s:28`) | 0x2F018 / 0x2650D / 0x4FB0 | 0x2F023 / 0x2653F / 0x4FC0 | **(-11, -50, -16)** |
| `0x80078A58` | `g_Spyro.m_Position` | 0x2F394 / 0x26493 / 0x5124 | 0x2F39D / 0x264C6 / 0x5122 | (-9, -51, +2) |
| `0x80078B64` | `g_Spyro.m_Physics.m_TrueVelocity` | -4064 / 512 / 128 | -3776 / 2144 / 64 | (-288, -1632, +64) |
| `0x80078B80` | `g_Spyro.m_Physics.m_TrueSpeed` | 0x1000 (4096) | 0x10DA (4314) | -218 |
| `0x80078AE0` | `g_Spyro + 0x88` (unnamed; read at `0x80042FF0`, `0x80043448`, `0x80043F4C`) | 1 | 0 | +1 |

Still EQUAL at tick 556: `g_CollisionNormal` (`0x80077368`), `g_CollisionTriangleIndex`
(`0x80075808`, 10708 on both), `m_Physics.m_Velocity` (`0x80078B58`), `m_Physics.m_Acceleration`
(`0x80078B4C`), `m_slopeAngle`, `m_againstWall`, `m_airTime`, `m_onSlope`, the whole 0x110-byte
camera, all 0xA8 bytes of `g_Pad`, `g_DemoDataPtr` (0x8007585C), `g_DeltaTime`, the level identity
and the load stage.

### What the numbers say

`m_TrueVelocity` is the movement the collision left over, in 1/32 units: its delta is **exactly 32x**
the position delta in all three components (-288/-9, -1632/-51, +64/+2), and `m_TrueSpeed` is the
faithful magnitude of each (4096 vs 4314 against |(-4064,512,128)| = 4098 and
|(-3776,2144,64)| = 4343). So the true velocity is a CONSEQUENCE of the position, not a second
independent fault, and the magnitude functions are not implicated.

`g_DeltaTime` is 2 on both (a demo assigns `g_UnprocessedFrames = 2`, gamepad.c:214), and the physics
runs `for (i = 0; i < g_DeltaTime; i++)` sub-steps, so an update integrates twice. `m_previousPosition`
(`g_Spyro + 0x8C`, 0x80078AE4) is written once per update, before the sub-step loop, and HOLDS the
tick-555 position on both cores at the park of 556. Therefore the whole (-9, -51, +2) is the sum over
the update's sub-steps, and the equal `m_previousPosition` says the sub-step loop started from equal
state.

The collision response is IDENTICAL on both cores. Console step (-125, +79, +4) with a retained true
velocity of (-118, +67, +2); product step (-134, +28, +6) with a retained (-127, +16, +4). Both lost
exactly 7 in x, 12 in y and 2 in z to the collision. Only the pre-collision MOVEMENT differs.

### The shape

Z is Spyro's UP axis, not depth: the floor normal is `(332, -255, 3328)` and `UpdateSlopeFloorCollision`
seeds "up" as `m_floorPositonOnSlope.z = 0x1000` (pete.c:755-757). So the offset is in the ground
plane with the height matching -- the product's Spyro is 9 units behind and 51 units off-line at tick
556 and 59/405 at 702, standing at the same height. Per-tick ground-plane steps at 556: console
`(-125, +79)` = speed 147.8 at heading 147.7 degrees; product `(-134, +28)` = speed 136.9 at heading
168.2 degrees. The product is 7% slower AND 20.5 degrees off heading in the same tick, and the
separation then grows because two equal-speed agents on different headings separate at second order.
That is why +405 tracks +59 at tick 702 without either being a ratio of the other: 405/59 = 6.9 while
the tick ratio is 702/556 = 1.26, so the growth is geometric, not proportional.

### Ruled out by measurement, not by argument

- **The recorded input stream and its pointer walk.** Over 580 iterations, 0 of the 42 words of
  `g_Pad` and 0 of `g_DemoDataPtr` ever differed. The demo's own words, including the STICKS at
  `g_Pad + 0x14` that the route's three `pad.*` ranges do not cover, are bit-identical throughout.
- **Every native override group in the physics path.** Rebuilt with `spyro_register_native_leaves`
  (copy3/zero3/fill/copyw) removed, and then with leaves + `native_vec` + `native_gte` + `native_angle`
  (vadd/vsub/vsra/angtblA/angtblB/angdist, mvmva/veclen/isqrt/vscale, angdiff8/spin60) removed as well,
  the divergence is BIT-IDENTICAL both times: tick 556, (-9, -51, +2), `g_Spyro + 0x88` = 1 against 0.
  Guest execution reproduces the product's behaviour exactly, so the fault is not in a hand-written
  body. (`VecRotateByMatrix` at 0x80017048 -- the `mvmva` override, which builds the collision ray --
  was additionally checked instruction by instruction against the real bytes in SCUS_942.28 and
  Beetle's COP2 field layout: CR0..CR4 and IR1..IR3 exactly as the guest writes them.)
- **The collision normal and triangle selection.** Both agree at 556, so the raycast hit the same
  triangle with the same normal and only the intersection POINT differs.
- **The store watch as a guest tracer.** `PSXPORT_CW` sees host-side stores only: armed on
  `g_Spyro.m_Position` it reported 8,970 hits, and every hit printed was from a native override
  (`0x80017758` vadd, `0x80017700` copy3); armed on `g_CollisionPoint` it reported 3 stores in a
  whole 560-iteration run, all three from boot, while the collision raycast writes that word on every
  call. It cannot attribute a guest-executed store, which is what the next step needs.

### The lead, and what the console's own PC observer says about it

`g_Spyro + 0x88` is 0 on both cores at ticks 552..555 and 1 on the product and 0 on the console at
556. The only store to it in the whole decompiled tree is the CLEAR at `0x8003FE7C` in `func_8003FE40`,
and it gates three branches in the physics: `bnez` at `0x80042FF0` (skip to `.L80043058`), a divide
compare at `0x80043448`, and a `bnez` at `0x80043F4C`. So one tick of this field decides whether a
movement adjustment runs, and it is the one small integer that differs in the same tick as the
movement.

Armed on the console for exactly the update at tick 556 (console field 7038) on `0x8003FE40`,
`0x8003FE7C` and `0x80041670` (`uv run --frozen python tools/probe_tick_divergence.py --ticks 557
--observe 0x8003FE7C,0x8003FE40,0x80041670:0x80078A80:16 --observe-at 556`), the observer reports
**scanned 677,984 matched 3 retained 3 dropped 0 pairing_errors 0, observation complete**:

| pc | instruction | registers |
|---|---|---|
| 0x8003FE40 | 27BDFF68 | v0=00000001 a0=801FFE80 |
| **0x8003FE7C** | **AC208AE0** | at=80080000 a0=80078B10 |
| 0x80041670 | 3C038008 | a0=0000003F a1=00000001 a2=0000000B a3=00002F00 |

`AC208AE0` is `sw $zero, -0x7520($at)` and `$at` holds 0x80080000, so the target is exactly
0x80078AE0 = `g_Spyro + 0x88`: the console EXECUTED that clear inside `func_8003FE40` in this update.
`a0 = 0x3F` at 0x80041670 is the sub-step index the `for (i = 0; i < g_DeltaTime; i++)` loop passes,
and the function was entered once in the update (retained 3, dropped 0).

The same observer with the RAM range on the field itself
(`--observe 0x8003FE7C,0x8003FE40:0x80078AD8:16`, 16 bytes at `g_Spyro + 0x80`; scanned 677,984
matched 2 retained 2 dropped 0, complete) reads, at BOTH records and therefore before the store:

| address | offset | value |
|---|---|---|
| 0x80078AD8 | +0x80 | 250 |
| 0x80078ADC | +0x84 | 4 |
| **0x80078AE0** | **+0x88** | **0** |
| 0x80078AE4 | +0x8C | 193432 |

So the console's field was already 0 when it cleared it, and the product's is 1 at the park with no
listed store that could put a 1 there. The two readings together say: the product executed a store to
0x80078AE0 with the value 1 that the reference did not execute, and it is not in the decompiled tree.

The call site of the console's clear is `func_80047B60 + 0x88` (0x80047BE8), immediately after
`func_8003E628` (the moving-platform capture) and immediately before `UpdateSlopeFloorCollision` --
the order that matters, because the field is cleared BEFORE the collision query and read by the
movement code after it.

### The next measurement

The console's path is observable per PC; the product's is not, and that is the actual gap.
`PSXPORT_CW` is host-side only, so nothing the product's Lightrec executes leaves a trace. The
framework already has the other half -- `psx::cpu::LightrecExecutor::configureStoreObserver`, which
reports the guest PC of a translated store together with the whole register file (the API
tests/test_dynarec_contract.cpp:623 uses) -- but it is a C++ API with no REPL or environment surface,
so nothing in this repository can arm it today. Exposing it (a REPL verb that arms a guest PC and
prints the hit's GPRs, product-side only, test-only) is what turns the next step from a bisection of
guesses into a comparison: arm both cores on the sub-step block at 0x80047B60 and find the first
instruction where the two register files differ.

### What the moby list is NOT, and the one-PC measurement that names it

The listing resolves the list base as `%hi(D_8006FCF4 + 0x400)` = `0x80070BF4` (spimdisasm printed the
nearest preceding label, and `D_8006FCF4` is `g_SonyImage`, a `.data` blob in
`asm/data/sony_text.s:5`). Reading 1,024 bytes there on both cores:

| tick | differing bytes | product | console |
|---|---|---|---|
| 3 | 223 / 1024 | **every word zero** | `0x8BC, 0x8CE, 0x8D1, 0x8DD, 0x8D5, 0x8F5, …` |
| 41 | 466 / 1024 | **every word zero** | `0x9D3, …` |

So that region is a real, growing divergence — and it is **not** the moby list, for two measured
reasons: the console's values (2183..2515) are far too small to be `Moby*`, and the product's
`func_800522C0` demonstrably executes 135,568 flush stores in the same window. The listing's constant
for the list base is therefore not the list base, which is consistent with
`func_level_11_8007DA78` being a 10,832-line `nonmatchings` reconstruction.

**The measurement that names the list, with no guess, is one PC and no RAM range:**
`tools/probe_emitter_predicate.py --ticks 45 --observe-at 41 --observe 0x800522CC`. `0x800522CC` is
`addi $t9, $a0, 0x0`, `func_800522C0`'s second instruction, and it fires ONCE PER CALL (1-2 per
update, against the updater's ~1,000 per update), so it fits the observer's 128-record ring where the
per-moby instructions do not. The record's `$a0` is the list base the guest itself used; walk it to
its NULL terminator on both cores and compare entry by entry.



* `Moby+0x42` is the moby's animation-flags byte and its only writer in level 11's resident code is
  `func_800522C0`'s `sw $at, 0x40($t5)` at `0x800523E8` / `0x8005243C`. The writer is the animation
  state machine, **not** the spawner, so the two leads are not the same defect as hypothesised.
* The product runs that updater (16,199 + 119,369 flush stores over 60 iterations) and the
  class-`0x71` moby at `0x80173B80` is nevertheless frozen, with `0x40 + 0x41 == 64`, which the
  updater's own arithmetic says would have set bit 0. So the moby is not being REACHED.
* `g_DynMobyCount`'s reference-side jump at tick 528 does not pass through `func_800524C4` (0 observer
  entries against a positive control's 1, 680,953 instructions scanned), so "fewer `MobyAlloc` calls"
  is not what the counter shows.

**The next measurement is one PC and no RAM range:** `--observe-at 41 --observe 0x800522CC`, whose
`$a0` is the list base the guest itself used, then walk that list to its NULL terminator on both
cores and compare entry by entry. That decides whether the product's list is short, reordered, or
holds a different pointer for the frozen moby, and it is the one thing both leads now point at: the
updater, the class-`0x71` handler and the level dispatch all walk that list, and the dynamic mobies
the reference allocated and the product did not are exactly the kind of thing a list-builder
difference produces. If the list matches, the next candidate is what BUILDS it — the level loader's
moby section, whose `g_DynMobys` / `g_MobyAllocPtr` bookkeeping (`loaders.c:635-641`) is the only
other place the pool is advanced.

The blocker is an INSTRUMENT GAP, narrowed: to name which translated store last wrote an arbitrary
word, neither `PSXPORT_STORE_OBSERVE` (store PCs, so it can only confirm a PC you already have) nor
`PSXPORT_CW` (biased to the slow path) is enough, and the console observer also needs a PC up front.
With the writer now named, neither is needed for this lead.

No fix is claimed and no tolerance is proposed: this is a decisive declared range, so it either matches
or it is a defect. Nothing in this session wrote a guest byte.

## Route result

The route exits 1. It is not a clean pass, and it is not tuned to be one: the level entry matches, 553
iterations match, and one real divergence 555 ticks in is reported rather than hidden. The second level
entry (the demo's return to the title and the next demo) is still uncompared, because the route stops
at the first decisive divergence.
