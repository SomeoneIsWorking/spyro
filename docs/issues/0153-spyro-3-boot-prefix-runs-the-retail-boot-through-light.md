---
id: 153
title: Spyro 3 now runs its own measured boot prefix through Lightrec, and the CD completion is the frontier
status: open
symptom: Spyro 3 had identity facts and an explicit unimplemented boundary — SCUS_944.67 was never executed; it now enters its own retail boot prefix 0x8002AB38 and runs it through Lightrec
tags: spyro3,boot,loader,re,ownership,cd,lightrec
created: 2026-09-30
updated: 2026-10-01
---

## Answer

**Spyro 3 executes its own retail boot prefix through Lightrec and stops at a named, byte-named
frontier.** One disc-backed headless process on `SCUS_944.67`:

```
[boot-native] Spyro 3 enters the retail boot prefix 0x8002AB38; game main 0x8001200C is NOT
               dispatched and libetc VSync 0x8005956C stays a frame boundary
[boot] installed no Spyro 3 native overrides: every boot service is a measured library leaf or the
        framework's stock CD seam
[frameloop:error] Spyro 3's boot prefix ran 480 step(s) without returning and delivered 483
        field(s) in total (step bound 480); it is polling rather than waiting, so the field bound
        cannot see it -- ending the run at resume 0x800504F0
[runtime] run complete: fields=484 product_steps=481 presentation_fences=481
        translated_blocks=429 executed_blocks=23373822 executed_instructions=135585657
        cache_hits=23373393 cache_misses=432 host_dispatches=506 invalidations=9365575 faults=0
[executor] fallback telemetry: fallback_blocks=0 fallback_instructions=0
        reasons{compilation_failed=0,self_modifying_code=0,unsupported_block=0,
        load_delay_hazard=0,unsafe_instruction_fetch=0} refused_fallback_blocks=0
```

So every acceptance number is present and nonzero where it must be: **23,373,822 executed blocks**,
**135,585,657 executed instructions**, **429 translated blocks**, **481 product steps**, **484
fields**, **481 presentation fences** (one per host step), **0 faults**, and **0 interpreter
fallback blocks or instructions of any reason** — nothing ran on the bounded interpreter, so this is
genuine dynarec execution and not a fallback in disguise. `cache_hits=23,373,393` against
`cache_misses=432` is a 99.998% hit rate over the run.

**The stop is a PC and a reason, not a timeout with no address.** The resume PC `0x800504F0` is
`lw $ra,0x1c($sp)` — the shared epilogue the loader's wait returns through. Where the guest spends
its 480 steps is a `bnez` self-edge at `0x80050658`, and saying so took an instrument rather than a
listing: my first account put the stall at the `CdRead` at `0x800504D0`, and arming the store at
`0x800504CC` immediately before it returned **zero executions**, which refutes that reading.
Section 3 carries the corrected account and the reach census behind it.

**Guest libetc VSync did not execute, and that is measured rather than asserted.** `0x8005956C`
*is* in the run's function-reach report, which looks like the opposite — §4.1 explains why reach
records dispatch, not execution. The discriminating measurement is the store observer armed on all
six store instructions in the VSync body plus one control store proven to be on this exact boot
path: **6 of 6 VSync stores never executed across 135,585,657 JIT instructions, while the control
`0x80050620` executed.** Same run, same observer, one of each class.

## 1. The identity, read out of the executable's own crt0

The three addresses this repository already recorded for `SCUS_944.67` were executable-analysis
results with no execution behind them. They are now confirmed against the crt0's own instruction
bytes, and the rest of the boot plan is confirmed the same way. Every row below is decoded with
`external/psxport/tools/disasm.py` over a RAM image built the PS-X EXE way — `file[0x800]` copied
to `t_addr` from the header at `0x18`, which is the only mapping under which `0x8001xxxx` holds
code — and the image is built from the identity-verified
`scratch/assets/spyro3/SCUS_944.67` (380,928 bytes, SHA-256
`cb819ee7…bb26e`).

| fact | value | the bytes that produce it |
|---|---|---|
| entry / crt0 | `0x80059444` | header `0x10`; `80059444 lui $v0,0x8007` is its first instruction |
| bss | `0x8006C4F4`–`0x800742D0` | `80059448 addiu $v0,$v0,-0x3b0c` (low) and `80059450 addiu $v1,$v1,0x42d0` (high, the `sltu` bound at `8005945C`) |
| stack top | `0x8006C3E4` | `8005946C lw $v0,-0x3c1c($v0)`, then `80059474 addi $v0,$v0,-8` and `or $sp,$v0,$t0` — the `-8` in `stackBias` |
| heap size store | `0x80069F04` | `800594A8 sw $a1,-0x60fc($at)`, where `$a1` is `sp - *(0x8006C3E0) - 0x800742D0` |
| heap base store | `0x80069F00` | `800594B4 sw $a0,-0x6100($at)`, where `$a0` is `0x800742D0` |
| global pointer | `0x8006C3B0` | `800594C0 lui $gp,0x8007 ; 800594C4 addiu $gp,$gp,-0x3c50` |
| libc init | `0x8005F63C` | `800594CC jal 0x8005F63C` — the BIOS `A(39h)` InitHeap thunk, `addiu $t2,$zero,0xa0 ; jr $t2` |
| game main | `0x8001200C` | `800594E0 jal 0x8001200C`, and its own `8001200C addiu $sp,$sp,-0x18 ; sw $ra,0x10($sp)` |
| resident text | `[0x80010000, 0x8006C800)` | header `0x18`/`0x1C` |
| libetc VSync | `0x8005956C` | 36 `jal` sites reach it; its body polls a counter POINTER and returns a difference (below) |

`tests/test_spyro3_runtime.cpp` asserts all of these, including the four (stack top, stack reserve,
heap size store, heap base store) the earlier revision of that test did not check — a wrong value
there breaks silently.

### The frame loop, from game main's own body

```
8001200C  addiu $sp,$sp,-0x18 ; sw $ra,0x10($sp)
80012014  jal  0x800594EC    the constructor-table walk
8001201C  jal  0x8002AB38    the boot prefix, which does not return
80012024  jal  0x80055400    the per-frame update, and the loop head
8001202C  jal  0x8001E638    the draw, after the update
80012034  j    0x80012024    the retail loop is these two and nothing else
```

The boot prefix is `0x8002AB38` (`addiu $sp,$sp,-0x40 ; sw $ra,0x3c($sp)`), and its leaves in call
order are `0x8005C684`, the display bootstrap `0x8002A834`, `0x8002A794`, the CD bootstrap
`0x8002A7B4`, `0x8004F8EC`, the geometry init `0x8002A99C`, `0x80050578`, and — at `0x8002ACB4` —
`jal 0x80074DEC` with `$a0 = 1`, a loaded module's entry **outside the resident text**. This port
never seeds `0x80074DEC` and never dispatches it.

## 2. The measured library leaves, and how each was located

`Spyro2Runtime` and `Spyro3Runtime` declare the same *shape* of `PlatformHlePlan` because both
images link the same SCEI libraries. That is a reason to look in the same place, not a reason to
copy a number: every address below was found in `SCUS_944.67`'s bytes, and the two places where
that process changed a conclusion are called out.

| leaf | address | the bytes that identify it |
|---|---|---|
| libetc VSync | `0x8005956C` | `8005956C lui $v0,0x8007 ; 80059570 lw $v0,-0x60d8($v0)` (mode-0 counter pointer) and `80059578 lw $a1,-0x60d4($a1)` (mode-1), then the poll loop at `800595A8` and `800595C0 subu` |
| VSync query counter | `0x1F801110` | see below |
| libgpu SetGeomOffset | `0x8005D35C` | `8005D35C sll $a0,$a0,0x10 ; 8005D360 sll $a1,$a1,0x10`, called once in the whole image, at `8002A9B0` with `(0x100, 0x78)` |
| libcd CdInit | `0x8005DB1C` | `8005DB2C jal 0x8005DBAC` (the handshake) inside a four-attempt loop, then the four success stores and the `"CdInit: Init failed\n"` at `0x80011A2C` materialised at `8005DB94` |
| libcd CdRead | `0x8005D96C` | `addiu $sp,$sp,-0x28 ; move $s4,$a0 ; move $s3,$a1 ; move $s2,$a2`, work area `0x8006B3B4`, `jal 0x8005956C` with `$a0 = -1` at `8005D9A8`, and `8005D9FC andi $v1,$a2,0x30` (the `mode & 0x30` sector selector) |
| libcd CdSync | `0x8005E074` | the loader's own poll: `80050490 addiu $a0,$zero,1 ; 80050494 jal 0x8005E074 ; 8005049C bne $v0,$s0` with `$s0 = 2` — `CdSync(1)` until `CS_SELF` |
| libcd CdCommand | `0x8005E0BC` | `move $s1,$a1 ; move $s2,$a2 ; move $s4,$a0 ; andi $s3,$s4,0xff`, command table `0x8006B3E0`, sync-callback slot `0x8006B568`; reached from the CD bootstrap at `8002A7D0` with `$a0 = 0x0E` |
| CdReadyCallback | `0x8005DB08` | `lw $v0,-0x4c30($v0) ; sw $a0,-0x4c30($at) ; jr $ra` — a pointer setter over the slot `0x8006B3D0` |

### The VSync query counter is measured, not inherited

`0x8005956C` does not read a register; it reads a **pointer** and dereferences it. `lui`+displacement
reaches `0x80069F28` and `0x80069F2C`, and resolves them to three and
two access sites, **every one of them a `lw` inside VSync and its timeout helper**. No instruction
in the resident text stores either word, so their values are the image's own initialised data —
which is inside the mapped text range, because this executable declares `d_size == 0`. In it:

```
0x80069F28 = 0x1F801814   GPUSTAT
0x80069F2C = 0x1F801110   root counter 1
0x80069F30 = 0x00000000
```

The polled pointer is the second one, so a negative `VSync` answers with `0x1F801110` — root
counter 1, which the framework serves from `Timing::hSyncCounter` and which **has no writable
state**. The field owner therefore owns no guest word for it, and the host does not advance one.
`tests/test_spyro3_boot_driver.cpp` pins the write-discard, because a field owner that "owns" a
counter nobody can write is the failure this lineage already hit once.

### What the guest's own leaves do, and why two of them are left executing

**`setGeomScreen` is NOT declared, and the reason is a measurement, not an omission.** The
companion call one instruction after `SetGeomOffset` is `8002A9B8 jal 0x8005955C` with
`$a0 = 0x155`. `0x8005955C` is a three-word leaf whose body is `48c4d000` (a COP2 register read)
then `jr $ra`. It is not a fixed-arity geometry leaf, and the same address is called from game main
at `8001206C` with a **computed** argument — `lw $a0,-0x1e30($a0)` then `addiu $a0,$a0,0x155` —
which settles it. Binding the framework's projection *writer* there would both mis-name the leaf
and put GTE state where retail's own call puts none.

`SetGeomOffset` **is** declared, and the native handler is a superset rather than a divergence: the
guest leaf's two `sll`s are the two argument shifts the framework handler performs, so every
register the guest can still read comes out the same, and the handler then writes the GTE geometry
offset the leaf's two dead COP2 reads would have needed.

**The GPU timeout pair is left executing.** `0x8005C2FC` is this image's `DrawSync` timeout arm —
`jal 0x8005956C` with `$a0 = -1`, `addiu $v0,$v0,0xf0`, `sw $v0,0x8006A25C`, `sw $zero,0x8006A260` —
and `0x8005C330` is the check that tests the deadline. The host GPU consumes guest work
synchronously, so the guest's own arm and check complete without spending a display field, and the
host has no reason to own two guest words in order to say so.

## 3. The frontier — the same CD completion, on this title's own words

The loader is a **chain of one-sector reads, each started by the previous sector's completion
callback**, and the words it uses are measured rather than inherited. The CD bootstrap leaf
`0x8002A7B4` registers this title's own per-sector reader through
`CdReadyCallback(0x8005DB08)` at `0x8002A7E0`, and the reader is `0x80050504`:

```
80050504  addiu $sp,$sp,-0x18
80050508  lui $a1,0x8007 ; addiu $a1,$a1,-0x1b80   -> 0x8006E480
80050514  lw  $v0,($a1)                             a read in progress?
8005051C  beqz $v0,0x80050568                       no -> nothing to do
80050520  andi $v1,$a0,0xff                         the sector number from the CD response
80050528  bne $v1,$v0(2),0x8005053C                 not the last sector -> read the next
80050530  sw  $zero,($a1)                           the last sector: clear the in-progress word
8005053C  addiu $a1,$a1,-8                          -> 0x8006E478
80050540  jal 0x8005E0BC  (a0 = 2)                  CdCommand(CdlReadS) for the next sector
8005055C  sw  $zero,-0x1b7c($at)                    -> 0x8006E484, bytes so far
80050560  jal 0x8005D96C  (a2 = 0x80)               CdRead(dest, src, 0x80)
```

**CORRECTION 2026-09-30, after the run: the wedge is on the executed path, and my first
dreading of it from the static listing was wrong.** The first account above placed the stall at the
`CdRead` at `0x800504D0`. The run's own instruments do not support that, and naming how they refute
it matters more than the account it replaces.

`PSXPORT_STORE_OBSERVE` armed on `0x800504CC` — the `sw $zero` immediately before that `CdRead` —
returned **`before=0 after=0`, "no STORE INSTRUCTION at this PC executed, of the 135585657 executed
JIT instruction(s)"**. The store did not run, so the block containing it never ran, so that
`CdRead` was never issued. I had read a straight-line listing and assumed the path; the arming shows
the assumption was wrong.

**Where it actually stops, on the executed path.** `PSXPORT_REACH_REPORT` puts the block boundaries
this run crossed, inside the loader:

```
0x800503f8  0x8005042c  0x800504e0  0x800504e8  0x800504f0
0x80050578  0x800505b0  0x800505c4  0x800505d8  0x800505ec  0x800505fc
0x80050610  0x80050650  0x80050658
```

`0x800504E0` is the `beqz` target of the in-flight test at `0x8005043C`, so on the polls this run
made the word at `0x8006E480` was **0** — the read was not in flight. And `0x80050578`-`0x80050658`
is the sector chain at `0x80050504`, which **did** run. The two facts together locate it:

```
80050618  addiu $v0,$zero,1
80050620  sw    $v0,-0x1b80($at)     0x8006E480 = 1   arm the read
80050624  addiu $v0,$a0,0x78
8005062C  sw    $a0,-0x1b8c($at)     0x8006E474 wanted
80050634  sw    $a1,-0x1b84($at)     0x8006E47C source
8005063C  sw    $v0,-0x1b78($at)     0x8006E488 wanted byte count
80050644  sw    $zero,-0x1b7c($at)   0x8006E484 bytes so far = 0
80050648  jal   0x8005D96C           CdRead(dest, src, 0x80)
80050650  jal   0x800503F8           <-- the wait, entered EVERY spin iteration
80050658  bnez  $v0,0x80050650       <-- SPIN: retry until the wait returns 0
```

and the wait returns "busy" on the bytes, not on the in-flight word:

```
80050444  lui  $v0,0x8007
80050448  lw   $v0,-0x1b7c($v0)      0x8006E484  bytes so far
8005044C  lui  $v1,0x8007
80050450  lw   $v1,-0x1b78($v1)      0x8006E488  bytes wanted
80050458  slt  $v0,$v0,$v1
8005045C  bnez $v0,0x800504f0        still short -> return "1"
```

**The wait condition is `0x8006E484 >= 0x8006E488`, and only the `CdReadyCallback` at `0x80050504`
can advance `0x8006E484`.** The guest registers that callback itself — `0x80050488 jal 0x8005DB08`
with `$a0 = 0x80050504` — so the whole completion path is the guest's own, and it never runs. The
framework's synchronous stock `CdRead` reads the sector from the image and returns; it never
queues a controller response, never raises INT1, and so never dispatches the slot the guest
registered.

That is the same root cause the static reading reached, but the **mechanism is the byte counter,
not the in-progress word**, and the distinguishing measurement is the armed store at `0x800504CC`
coming back zero.

 over the authenticated image gives the whole access set, and every
resolved site is in this loader apart from one, so the word is the loader's and not a coincidence:

| word | accesses | what it is |
|---|---|---|
| `0x8006E480` | 4 — `lw 0x80050434`, `lw 0x80050514`, `sw 0x80050620`, `sw 0x80050728` | the read in progress |
| `0x8006E484` | 6 — `lw 0x8003A41C`, `lw 0x80050448`, `sw 0x800504CC`, `sw 0x8005055C`, `sw 0x80050644`, `sw 0x8005074C` | bytes so far / bytes wanted |
| `0x8006E478` | 4, materialised only | the second byte counter of the same chain |

**On retail the BIOS's own CD-ROM interrupt handler runs `0x80050504` once per sector. This port
does not have that handler.** `runtime/psx/cd_ready_delivery.cpp` is the framework's stand-in for
it, and it is delivered at the interrupt, from a controller response that the stock synchronous
`CdRead` never produces: `cd_read_stock_sync` (`external/psxport/runtime/psx/cd_override.cpp`)
performs the whole read from the disc image and never touches the controller, so no response is
queued, no INT1 rises, and the arm has nothing to deliver. Spyro 1 does not hit this — it declares
no `GuestCdStreamCallbackLayout` and its loader is not chained — and Spyro 3 does, on its own word
`0x8006E480` and its own poll at `0x80050434`.

**Required framework change, not made here** (`~/repo/psx/psxport` is not this repository's to
edit; a parallel worktree is on it). For a direct runtime that declares
`GuestCdStreamCallbackLayout::DeliveryOwner::GuestInterrupt` and whose guest chains reads on the
ready callback, the stock `CdRead` path must also produce the completion the declared owner
delivers: after a successful `cd_read_stock_sync`, acknowledge the controller and raise the CD IRQ
with a data-ready response, so `cd_ready_delivery` dispatches the registered slot exactly once per
completed read. Acceptance is a run in which `cd_ready_delivered` equals the number of reads the
guest issued, `0x8006E480` returns to 0, and the load at `0x80050440` returns. A title-selectable
seam is equally acceptable; what is not acceptable is the title writing its own copy of the
framework's interrupt handler.

## 4. Measurement instruments, and the two whose clobber model bit

### 4.1 The function-reach report records DISPATCHES, not executions

`PSXPORT_REACH_REPORT=…` over the same boot gives 346 reached entries for
`scratch/assets/spyro3/SCUS_944.67`, content identity `0x091BA3EB04F7C8BD`, `complete: true`,
`unowned_dispatches: 4` — and `0x8005956C` is among them. **Read naively, that says the guest's
libetc VSync body ran. It did not, and the report cannot tell the two apart.**

`runtime/cpu/lightrec_executor.cpp`'s `blockBoundary` does this, in order:

```cpp
if (impl.reach) {
  impl.reach->observe(guestPc);                       // <-- the recorder sees it
}
...
if (boundary.reason == BoundaryReason::None && boundary.dispatchHostServices &&
    classifyGuestHostDispatch(impl.core, guestPc) != GuestHostDispatchKind::ExecuteGuest) {
  boundary.reason = BoundaryReason::HostDispatch;     // <-- the native owner takes it
}
if (boundary.reason == BoundaryReason::None) {
  return LIGHTREC_BLOCK_CONTINUE;                     // <-- only here does guest code run
}
boundary.pc = guestPc;
return LIGHTREC_BLOCK_STOP;
```

`observe()` is called **before** the classification, so a reach entry means "the executor examined
this address at a block boundary". For an address owned by a native override, the boundary then
returns `BLOCK_STOP` and the block is never translated, let alone executed. The reach report's
denominator is *addresses examined*, not *addresses executed*, and nothing in its JSON says so.

This is the same class as the dead taps catalogued for this workspace (`is3d`, `OtAttr`, the
`VSync(0)` census): a confident answer about the wrong subject. A reach entry is evidence of
**dispatch**, and a dispatch to `PlatformHle::vsync` is evidence the VSync boundary was honoured,
which is the opposite of what a naive reading concludes.

### 4.2 The store observer, armed on the VSync body's own store PCs with a live control

`PSXPORT_STORE_OBSERVE` matches the **guest PC of a translated STORE instruction**, not a data
address (`docs/issues/0133`). So the discriminating arm is the six store *instructions* inside the
VSync body, plus one store instruction proven to execute on this exact boot path as the control:

| armed store PC | what it is | role |
|---|---|---|
| `0x800504CC` | `sw $zero,-0x1b7c($at)` -> `0x8006E484`, the loader's bytes-so-far counter | **positive control** — it is in the block immediately before the `CdRead` at `0x800504D0` that the stop PC proves ran |
| `0x80059580` | `sw $ra,0x20($sp)` | VSync body |
| `0x80059584` | `sw $s1,0x1c($sp)` | VSync body |
| `0x80059588` | `sw $s0,0x18($sp)` | VSync body |
| `0x80059598` | `sw $v0,0x10($sp)` | VSync body |
| `0x800596A4` | `sw $v0,-0x60cc($at)` -> `0x80079F34`, the wait-mode latch | VSync body, global |
| `0x800596B0` | `sw $v0,-0x60d0($at)` -> `0x80079F30`, the previous-counter latch | VSync body, global |

Six is the **complete** set: `disasm.py` over `[0x8005956C,0x800596E4)` decodes 94 of 94 words
with zero unknowns and finds exactly those six `sw` instructions, so the negative covers the whole
VSync body rather than a sample of it.



**Measured, on the corrected seven-PC arm:**

```
[store-observe] report: armed=yes store_pcs=7 jit_instructions=135585657 fallback_instructions=0
                    callback_lines=2
[store-observe]   [0] store PC 0x80050620 EXECUTED: 1 store(s) observed at this PC
[store-observe]   [1] store PC 0x80059580 stores before=0 after=0 — no STORE INSTRUCTION at this
                    PC executed, of the 135585657 executed JIT instruction(s).
[store-observe]   [2] store PC 0x80059584 … before=0 after=0
[store-observe]   [3] store PC 0x80059588 … before=0 after=0
[store-observe]   [4] store PC 0x80059598 … before=0 after=0
[store-observe]   [5] store PC 0x800596A4 … before=0 after=0
[store-observe]   [6] store PC 0x800596B0 … before=0 after=0
```

**This is the measurement, and it is a two-class discriminator rather than a clean zero.** The
control `0x80050620` — `sw $v0,-0x1b80($at)`, the store that arms `0x8006E480` to 1 immediately
before the `CdRead` at `0x80050648` — **executed**, once, in the same run in which the six VSync
stores did not execute at all. So the arming was live, the observer was fed, and the zeros are
zeros about the guest VSync body rather than about a dead tap.

Note that the control fired **once**, not once per spin iteration: the guest issues the read once and
then spins at `0x80050650` on the *wait*, which stores nothing. That is consistent with the section 3
reading and is itself a check on it.

**So: 6 of 6 stores in the guest libetc VSync body, 0 executions, against 1 execution of the
control, over 135,585,657 executed JIT instructions and 0 fallback instructions.** The guest body at
`0x8005956C` did not run. The 484 delivered fields came from the framework's `PlatformHle::vsync`
owning that address.

**One caveat that keeps this honest.** `0x8005956C` appears in the *reach* report of the same run
(§4.1). That is not a contradiction and it is not a second measurement: reach records block-boundary
addresses **examined**, the store observer records store instructions **executed**, and the two
instruments disagree by construction. Anyone reading either alone gets a confident answer about the
wrong subject.

The control is what makes this a measurement rather than a dead tap. If `0x800504CC` did not fire
either, the arming itself would be wrong and the six zeros would say nothing — which is precisely
the failure this repository has already published twice.

## 5. What is NOT claimed

Stated explicitly, because every one of these is a claim that would be easy to make from the
numbers above and would be false:

- **No gameplay.** `game main 0x8001200C` was never dispatched — the run ends inside the boot
  prefix's module load. Nothing here says anything about Spyro 3's stages, its update/draw leaves,
  or its presentation.
- **No picture.** 481 presentation fences is the *frame contract* being honoured (one fence per
  host step), not evidence of a rendered scene. The boot prefix never reaches game main, so there
  is no Spyro 3 scene to show. No frame was captured and no non-black-pixel count is claimed.
- **No `setGeomScreen` interception.** The candidate `0x8005955C` is a three-word COP2-read leaf
  that game main also calls at `0x8001206C` with a computed argument, so it is NOT libgpu
  `setGeomScreen` and is deliberately left executing as guest code.
- **No GPU-timeout claim.** The arm/check pair at `0x8005C2FC`/`0x8005C330` is deliberately left
  running in guest code, because the host GPU is synchronous and the guest's own wait therefore
  completes without spending a display field. Nothing is claimed about it either way.
- **`PSXPORT_SPYRO3_DISC` was reported UNKNOWN and that line is FALSE.** The run's config audit says
  `UNKNOWN knob PSXPORT_SPYRO3_DISC is set and matched nothing — it did NOTHING in this run`.
  343 lines later the same log says
  `[disc] opened <media>/Spyro - Year of the Dragon (USA).chd (31742 hunks,
  8 frames/hunk)` — the path is the `.env` value of that exact key, so it selected the media. The
  one fill in `[disc] disc hunk cache at shutdown: 1 hunk lookup(s), 0 hit(s), 1 fill(s)` is that
  image being read. See section 6: this is a framework audit defect, not a title defect.
- **Only 1 CD sector was filled.** 484 fields of a module-chained loader cost one 2048-byte disc
  read. The wedge is therefore reached on the *first* sector of the *first* module, not deep into a
  chain — so "the chain never advanced" is measured at its first link, not extrapolated.
- **`[producers:warn]` and `[ndepth:warn]` are reported as NEVER FED**, and are read that way. They
  are not "nothing drew".

## 6. A framework audit defect this run exposed, with its falsifier in the same log

The config audit printed:

```
[cfg:warn] UNKNOWN knob PSXPORT_SPYRO3_DISC is set and matched nothing — it did NOTHING in this run
```

and the same run printed, 343 lines later:

```
[disc] opened <media>/Spyro - Year of the Dragon (USA).chd (31742 hunks, 8 frames/hunk)
```

That path is the `.env` value of `PSXPORT_SPYRO3_DISC` and no other key in the tree holds it, so
the "did NOTHING in this run" clause is **false**, and it is false in the worst direction: it is a
confident negative about a knob that demonstrably did something.

**Root cause, from the code.** The key is not a static string the framework can declare.
`runtime/psx/game.cpp:42` reads `runtime->discEnvVar()` — a per-title `const char *` — and stores it
as `disc.env_key`, and `runtime/psx/disc.cpp`'s `resolve_disc_path` uses that runtime string as the
environment key. A **title-supplied** name therefore cannot be an entry in the static declared-CVar
registry (`runtime/psx/config_vars.h`), so the audit's registry lookup always misses it. Spyro 1 and
Spyro 2 have exactly the same shape and would produce the same false line; this run is only the one
where the falsifier happens to be visible.

This is NOT a title-side defect and nothing in this repository should be changed to "fix" it:
`titles/spyro3/core/spyro3_runtime.cpp:231` correctly returns a name and never calls `getenv`.

**Required external change, not made here** (`~/repo/psx/psxport` is not this repository's to
edit): the config audit must learn the title-supplied disc key. Concretely, `config.cpp`'s env
audit should take the resolved `disc.env_key` as a known-read key once `disc_open` has consumed it,
so it is reported as declared-and-read rather than UNKNOWN — and, independently, the audit's
UNKNOWN branch should stop asserting "it did NOTHING in this run", which is a claim about a knob
whose readers the audit has not enumerated. The acceptance check is the same run printing the
disc key as read, with the false UNKNOWN line gone.

## 7. Gates run on this change

**All counts below are from the REBASED tree, against `origin/main` `5f23ffd`.** The pre-rebase
numbers are recorded separately underneath, because the counts moved when upstream landed and a
report that quoted only the old ones would be stale.

| gate | result |
|---|---|
| `cmake --build build -j 4` (Clang 22.1.8, ccache) | green, **0 errors**, 245 targets |
| `ctest --test-dir build -j 4` | **113 of 113 passed**, `100% tests passed, 0 tests failed` |
| `clang-tidy -p build --warnings-as-errors="*"` over all 5 first-party units (`spyro3_runtime.cpp`, `spyro3_frame_driver.cpp`, `spyro_guest_call.cpp`, `test_spyro3_boot_driver.cpp`, `test_spyro3_runtime.cpp`) | clean, 0 diagnostics |
| `tools/format.py --check` | **393/393**, unformatted=0 |
| `tools/source_policy.py` | **533** live files scanned, **376** product paths, PASS |
| `tools/drive.py gameplay` (Spyro 1, the regression this work must not break) | `reached GS_Playing at frame 6360`, exit 0 |
| Spyro 3 boot run (`scratch/s3re/run_spyro3.py`, disc-backed, headless) | 484 fields / 481 product steps / 481 presentation fences / 23,373,822 blocks / 135,585,657 instructions / 429 translated / **0 faults / 0 fallback blocks / 0 fallback instructions**; named stop at resume `0x800504F0` |
| Spyro 3 VSync discriminator | control `0x80050620` **EXECUTED: 1 store**; **6 of 6** VSync stores `before=0 after=0` — zero executions |

The Spyro 3 run on the rebased binary reproduced the pre-rebase run **exactly**: the same 480 boot
steps, the same resume `0x800504F0`, the same 23,373,822 executed blocks, the same 135,585,657
instructions, the same 429 translated blocks, the same 432 cache misses and the same 506 host
dispatches. Nothing in the 15 upstream commits moved this title's behaviour.

**Pre-rebase, for comparison:** build 324 targets, ctest **110 of 110**, format 386/386, source
policy 498 files / 344 product paths, `GS_Playing at frame 6380`. The deltas are entirely upstream's
— `93b49f1` added the shared field owner, its tests and ; `route_trace` and the
remaining commits added their own.

**The oracle contention, recorded rather than deleted.** While `heavy.py` still queued behind other
agents this suite twice reported **109 of 110**, the one failure being `oracle_compare_selftest`:
`console reference refused: another console-oracle build/run owns the activity lock`. That test
passed 1 of 1 alone and `ctest -j 4 -E '^oracle_compare_selftest$'` gave 109 of 109, so it was lock
contention and not a defect. It is the same class as every other dead tap in this workspace, except
that here the gate **correctly refused** instead of reporting a false pass — which is the behaviour
those other instruments got wrong.

The oracle contention is the same class as every other dead tap in this workspace, except that here
the gate **correctly refused** instead of reporting a false pass — which is the behaviour those
other instruments got wrong.

Three other tests were red on the first full run for a different reason and are now green:
`probe_emitter_predicate_selftest`, `probe_level_update_dispatch_selftest` and `decomp_selftest`
all died with `FileNotFoundError` on `external/spyro-1/asm/...` because the declared
`external/spyro-1` submodule had never been initialised in this worktree. `git submodule update
--init --checkout external/spyro-1` restored it at its recorded gitlink
`ccddb80294d9d5967ba523171628ecd4a1e9300d`, and all three then pass. That is provisioning, not a
code change.

## 8. A build break this work inherited, and its cause

The tree did not compile on arrival. `game/core/cd_queue.cpp` and `game/core/field_owner.cpp` both
failed with `no member named 'dispatchGuestToReturn2' in namespace 'psx::cpu'`,
`too many arguments to function call, expected 4, have 6`,
`no member named 'dispatchGuestToReturn0'`, and `no member named 'ExecutionBudget'`.

**Cause:** placing the shared guest-call owner at `game/core/guest_call.{h,cpp}` **shadowed
psxport's own `runtime/cpu/guest_call.h`.** `game/core` precedes the framework's CPU include
directory on the compile line, so every Spyro translation unit that wrote `#include "guest_call.h"`
for `psx::cpu::dispatchGuestToReturn2` and `psx::cpu::ExecutionBudget` silently received the
title's `spyro::GuestCall` instead. The framework's header still compiled and was still on the
include path; it simply was not the one being included. The error named a missing *function* when
the real fault was a colliding *filename*, which is why it is written down here.

**Fix:** the shared owner is `game/core/spyro_guest_call.{h,cpp}`, and the reason is stated in the
header so the name cannot be re-collided. No framework file was touched.

### Note (2026-10-01)
2026-10-01: the module entry 0x80074DEC stop was a missing image publication, fixed in issue 0156; the boot prefix returns (336 steps / 546 fields) and the first main-loop draw faults at 0x8001C4EC (scratchpad 0x1F800400).
