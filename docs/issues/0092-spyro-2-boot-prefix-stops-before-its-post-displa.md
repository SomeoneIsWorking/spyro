---
id: 92
title: Spyro 2's boot now runs the retail boot prefix through Lightrec, and the CD completion is the frontier
status: open
symptom: the finite three-field display bootstrap is gone; the retail boot prefix 0x80011E9C executes through Lightrec and wedges in the guest's chained per-sector CD reader with no completion delivered
tags: spyro2,boot,loader,re,ownership,cd,lightrec
created: 2026-08-28
updated: 2026-10-01
---

## Answer

**The recorded symptom is gone and the stop moved, twice, to a place that is now named from the
guest's own bytes.** The three host-owned display-bootstrap fields, the boot-prefix leaf `0x80011B1C`
stop, and the hand-transcribed bootstrap itself are deleted rather than extended: `SCUS_944.25` now
enters its own retail boot prefix `0x80011E9C` and executes it through Lightrec. Measured in one
headless run of `build/bin/spyro_port` with `PSXPORT_SPYRO2_DISC` set:

| claim | measurement |
|---|---|
| the run passes the leaf `0x80011B1C` | 12 product steps, 30 delivered fields, 27 presentation fences, **exit 0** |
| guest VSync body `0x80058EDC` never executes | every display wait comes back as a typed `FrameBoundary` exit; the library body is a native leaf and the library text is never entered |
| Lightrec does the work | 427 translated blocks, **1,319,744 executed blocks**, 7,655,547 executed instructions, 0 faults, 427 cache misses against 1,319,282 hits |
| the guest boots its display and prints | `[guest-stdout] ResetGraph:jtb=800661d0,env=8006` then `6218`, through the guest's own printf |
| the run stops | the loader's CD completion poll; the run is now ended BY NAME rather than by a frame cap, at `resume=0x80013788` after 481 steps (480 of boot steps, 484 fields, 135,585,691 executed instructions, 0 faults) |

**Two things block the rest of the boot, and both are outside this repository.** They are named
below with the mechanism, the exact instruction, and the change required. Neither is worked around.

## 1. What replaced the bootstrap

The retired owner was `titles/spyro2/core/spyro2_loaded_bootstrap.*` plus
`spyro2_gpu_sync.*`: three hand-owned fields, a finite stop, and a transcription of the first three
fields of a guest boot. It is deleted, together with its timing test and its `CMakeLists.txt` entry.
`Spyro2FrameDriver` (`titles/spyro2/core/spyro2_frame_driver.*`) owns a product step instead:

- **one finite guest call**, entered at the measured boot prefix and resumed across steps by
  `Spyro2GuestCall` (`titles/spyro2/core/spyro2_guest_call.*`). The return address is captured
  before the first dispatch and reused on every resume, because a resume that adopted the nested
  `$r[31]` the body left behind would end the call in the wrong place.
- **one delivered field per guest display wait.** A `FrameBoundary` exit is the host's field; the
  field is real time and the guest's vblank work runs with it, but it is not presented.
- **one presented field per product step**, which is the framework's frame contract.
- after the prefix returns, the retail per-step pair: game main calls the gamestate update
  `0x8001B140` and then the draw `0x800156FC` (`80011AEC` and `80011AF4`; the loop body at
  `80011AF4` is the draw and the loop back-edge at `80011B04` is all that is left of it).

The boot prefix's leaves were read from a RAM image built the PS-X EXE way (`file[0x800] -> t_addr`
from the header's `t_addr` at 0x18), which is the only mapping under which `0x8001xxxx` holds code:

| address | role | byte evidence |
|---|---|---|
| `0x80011E9C` | boot prefix entry | `80011E9C addiu $sp,$sp,-0x18 ; sw $ra,0x14($sp)` |
| `0x80011EA4` | first leaf | `jal 0x800548A4` |
| `0x80011EAC` | display bootstrap | `jal 0x80011BBC` |
| `0x80011BD0` | its VSync | `jal 0x80058EDC` |
| `0x80011EB4` | **the leaf the old bootstrap stopped at** | `jal 0x80011B1C` |
| `0x80011EBC` | CD bootstrap | `jal 0x80011B3C` |
| `0x80011EC4` | music init | `jal 0x80012B84` |
| `0x80011ECC` | geometry init | `jal 0x80011D24` |
| `0x80011EF8` | module load | `jal 0x80013810` |
| `0x80011F0C` | loaded module entry, **outside the resident text** | `jal 0x80077374` (`a0 = 1`) |

`0x80077374` is still never seeded as resident code and is never dispatched by the port. The
module-load leaf is reached; the loader's CD completion is not, so the dispatch is not.

## 2. The VSync query counter, measured instead of assumed

`0x80058EDC` is not only a wait. Its body loads two counter POINTERS and polls them:

```
80058EDC  lui   $v0,0x8006
80058EE0  lw    $v0, 0x6450($v0)     ; mode-0 counter
80058EE4  lui   $a1,0x8006
80058EE8  lw    $a1, 0x6454($a1)     ; mode-1 counter
...
80058F30  subu  $v0,$v0,$v1          ; minus the count stored at 0x80066458
```

and the earlier bootstrap refused to run because the framework's `PlatformHle::vsync` aborts on a
negative argument with no declared counter: *"VSync negative query at 0x80058EDC has no measured
libetc field counter"*. In the authenticated image the two pointer words hold

```
0x80066450 = 0x1F801814   (GPUSTAT)
0x80066454 = 0x1F801110   (root counter 1: the HBlank timer the BIOS display interrupt counts on)
0x80066458 = 0x00000000
```

so the counter this title's VSync reports is `0x1F801110`. **It is not a writable word, and the field
owner does not write it.** The framework serves it from `Timing::hSyncCounter`
(`runtime/psx/io_peripherals.cpp`), whose own comment says root counter 1 "has a value but no
writable state here". An earlier revision of this file described it as a D-SRAM word the host
advanced once per field, and proposed `FieldOwnerFacts::displayCounter` for it; that was wrong and
is **removed** — the owner holds no writable counter word for it, and `psxport`'s host field clock
is what advances the timer the query answers with.

`0x80066394` was considered and rejected as a field counter: `tools/probe_guest_word.py` finds
exactly one access to it, at `0x80057FB8`, which is libcd's own internal.

**No guest field-counter word is claimed for this title.** `FieldOwnerFacts::fieldCounter` is 0
because no Spyro 2 global has been shown to be a frame counter, and `rootHandlerSlot` is 0 because
SCUS_944.25 installs the BIOS `HookEntryInt` continuation as its libetc vblank callback
(`0x80054A78` stores `B(0x19)`, `0x80054A84` registers it through `VSyncCallback 0x8005AC34`), which
the framework's own interrupt path resumes. Both are stated rather than invented.

## 3. THE FRONTIER — the CD completion the framework's synchronous read never raises

The boot reaches the module load at `0x80013810` and stops there. The loader's state is three words
in the image's arena, and `tools/probe_guest_word.py` gives their whole access set:

| word | accesses | owner |
|---|---|---|
| `0x800682E8` (read in progress) | `lw 0x800136CC`, `lw 0x800137AC`, `sw 0x800138B8`, `sw 0x800139C0` | the loader `0x800136xx`-`0x800139xx` |
| `0x800682EC` (bytes so far) | 6, all in the same range | the loader |
| `0x800682F0` (bytes wanted) | `lw 0x800136E8`, `sw 0x800138D4`, `sw 0x800139DC` | the loader |

The read is a **chain of one-sector reads, each started by the previous sector's completion
callback** — not a stream and not a file read:

```
800138B8  sw  $v0,-0x7d18($at)   ; 0x800682E8 = 1
800138DC  sw  $zero,-0x7d14($at) ; 0x800682EC = 0
800138E0  jal 0x80058108         ; CdRead(1 sector, buf, 0x80)
800138E8  jal 0x80013690         ; poll "is the read done?"
800138F0  bnez $v0, 0x800138e8   ; until it is

800136C4  lw  $v0,-0x7d18($at)   ; 0x80013690: in progress?
800136D4  beqz $v0, 0x80013778
800136E0  lw  $v0,-0x7d14($at)   ; bytes so far
800136E8  lw  $v1,-0x7d10($at)   ; bytes wanted
800136F0  slt $v0,$v0,$v1        ; still short -> return 0
```

and the callback that advances it is `0x8001379C`, the function the CD bootstrap registers through
`CdReadyCallback(0x800582A4)` into the slot at `0x800663B8`:

```
8001379C  addiu $sp,$sp,-0x18
800137A4  addiu $a1,$a1,-0x7d18   ; $a1 = 0x800682E8
800137AC  lw   $v0,($a1)
800137B4  beqz $v0, 0x80013800    ; no read in progress -> nothing to do
800137B8  andi $v1,$a0,0xff       ; $a0 = the sector number
800137C0  bne  $v1,$v0(2), 0x800137d4
800137C8  sw   $zero,($a1)        ; the last sector: clear the in-progress word
...
800137F4  sw   $zero,-0x7d14($at) ; otherwise: read the NEXT sector
800137F8  jal  0x80058108
```

**On retail the BIOS's own CD-ROM interrupt handler runs that callback once per sector. This port
does not have that handler, so `runtime/psx/cd_ready_delivery.cpp` is the framework's stand-in for
it — and it is delivered at the interrupt, from a controller response that never arrives.** Measured:
`cd_ready_delivered + cd_ready_declined = 0` and **zero `cdirq` lines** for the whole run, with the
disc genuinely read (1 hunk fill, 1.3 ms). The cause is `cd_read_stock_sync`
(`external/psxport/runtime/psx/cd_override.cpp:475`): the native stock `CdRead` performs the whole
read from the disc image and **never touches the controller**, so no response is queued, no INT1
rises, and the arm has nothing to deliver. Its own comment says the override exists to remove "the
whole guest state machine: no ready-callback loop", which is right for a guest without one and
wrong for this guest, which chains one read per sector on that very loop.

Spyro 1 does not hit this: it declares no `GuestCdStreamCallbackLayout` and its loader is not
chained, so nothing waits for a completion the framework never raises.

**Required framework change (not made here — `~/repo/psx/psxport` is not this repository's to
edit).** For a direct runtime that declares `GuestCdStreamCallbackLayout::DeliveryOwner::GuestInterrupt`
and whose guest chains reads on the ready callback, the stock `CdRead` path must also produce the
completion the declared owner delivers: after a successful `cd_read_stock_sync`, acknowledge the
controller and raise the CD IRQ with a data-ready response, so `cd_ready_delivery` dispatches the
registered slot exactly once per completed read. Acceptance is a run in which `cd_ready_delivered`
equals the number of reads the guest issued, the guest's `0x800682E8` returns to 0, and the module
load `0x80013810` returns. A title-selectable seam is equally acceptable; what is not acceptable is
the title writing its own copy of the framework's interrupt handler.

## 4. The Lightrec false positive that blocks zero fallback

With `PSXPORT_LIGHTREC_FALLBACK_BLOCK_LIMIT=0` the shipping default refuses before any interpreter
execution, and the refusal is a **false positive in a shared dependency**, not a guest behaviour:

```
Lightrec fallback refused before interpreter execution: reason=self_modifying_code,
admitted_blocks=0, limit=0 at guest pc=0x8005FFFC ra=0x8005F820 sp=0x801FFEF0
executed_blocks=102 executed_instructions=657 fallback_blocks=0
```

`0x8005FFFC` is the newline arm of the guest's own printf character emitter, and it is a real
branch target — `8005FFB8 bne $a0,$v0,0x8005fffc`, a conditional branch, which does **not** end a
Lightrec block. The block therefore runs from `0x8005FFFC` to the next unconditional jump at
`80060050 jal 0x8005ef74`, i.e. `[0x8005FFFC, 0x80060058)`, and inside it:

```
80060028  lui   $at,0x8006
8006002C  sw    $v0, 0x6c58($at)     ; stores to 0x80066C58 -- ordinary data RAM
```

`optimizer.c:1730` decides a block is self-modifying by comparing the **base register's value**
against the block's own address range. `lui $at,0x8006` is `0x80060000`, and `0x80060000` lies
inside `[0x8005FFFC, 0x80060058)`, so the block is marked `BLOCK_NEVER_COMPILE` even though the
store's real target is data. The comparison ignores the displacement; comparing the resolved store
address, or excluding a base that is not itself inside the block's executable text, would not.

Two rules were needed to establish this, both from the source rather than from a guess:

- `lightrec_get_mips_block_len` (`lightrec.c:1421`) ends a block only at a syscall, a META_BIOS op
  or an **unconditional** jump, so conditional branches do not bound a block.
- `BLOCK_NEVER_COMPILE` is set in exactly two places, `optimizer.c:1734` and the exact-memset
  pattern at `optimizer.c:2242`; the block at `0x8005FFFC` is not the memset pattern, so the store
  rule is the one that fires.

This is not worked around. A title-owned reimplementation of a library printf is not the fix, and
the one override that would remove the block from the JIT is the wrong owner for a library leaf.
**The required change is in the shared Lightrec rule** (`~/repo/shared/lightrec`, reached through
psxport's vendored copy), and until it lands Spyro 2 cannot claim zero interpreter fallback. With
the fallback budget raised the boot proceeds past it, which is how the CD frontier above was reached;
that is a diagnostic budget and is not product evidence.

## 5. Measurement instrument, and the defect it had

`tools/probe_guest_word.py` answers "which instruction reads or writes this guest word" over a
mapped image, because the word is reached by `lui`+displacement and a literal scan reports nothing
while reading as "no accesses".

**It reported stores that were not stores.** With the same tool, `0x800682E8` first came back with
five access sites; three of them are `sll $t3,$t2,9` (`0x80022FC8`), `bltz $a3` (`0x800230B8`) and
`sll $t3,$t3,0xe` (`0x800231C0`) — instructions that cannot store. The cause was a clobber model
that cleared only `rd` and `rt` and had no barrier for a COP2 instruction, so a `lui`-built base
survived an `mtc2`/`cfc2` block that writes `r1..r15` itself. A second defect went the other way: a
store was treated as clobbering `rt`, so two stores through one base reported once. Both are fixed
(COP0/1/2, every branch and every undecoded SPECIAL encoding are whole-file barriers; a store writes
memory and no register), and the selftest now carries the GTE case and the undefined-encoding case
as negatives, plus a two-stores-one-base case as the positive that a store-clobbers-`rt` probe fails.

That is the second time in this repository that a probe reported a confident measurement about the
wrong subject, and the rule is the same one the CD frontier above follows: **name the feeder, and
check the site's own bytes before believing the site.**

## 6. What is NOT claimed

- No Spyro 2 logo, title screen, attract mode or gameplay frame exists yet: the loader chain stops
  at the CD completion, which is before any of them.
- The captured frame is a boot/clear frame, not a logo frame. It is reported as what it is:
  `scratch/screenshots/f120.png` and `f400.png` are both **0.00% non-black of 512x240 = 122,880
  px**, against the capture path's own control, a Spyro 1 gameplay frame at `f6301.png` at
  **93.33% non-black**. Two captures at different frames and one at a different title, so the zero
  is a measurement of this run and not of the capture.
- `0x80077374` is never dispatched, and nothing outside the resident text is ever treated as code.
- The measured 484 fields, 481 product steps, 23,373,821 executed blocks and 135,585,691 executed
  instructions were produced with a raised fallback budget
  (`PSXPORT_LIGHTREC_FALLBACK_BLOCK_LIMIT=2000`, 37 fallback blocks, all `self_modifying_code`, all
  from the one site in section 4). The shipping default's run is the 12-block refusal in section 4.

### Note (2026-10-01)
2026-10-01: the loaded module's ambiguous identity at 0x80077374 was a missing image publication, fixed in issue 0156; the boot prefix now returns (43 steps / 133 fields).
