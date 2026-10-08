# Spyro 3

Measured bring-up for `SCUS_944.67` (USA). The 380,928-byte executable is authenticated by
`executable.json`; binary analysis identified 639 resident function entries without foreign-title
or guessed-overlay assumptions. `Spyro3Runtime` is a direct runtime: it binds no Spyro 1
`GameConfig`, hooks, context, renderer, or title behavior. It returns `PSXPORT_SPYRO3_DISC`,
declares a `PlatformHlePlan` of measured library leaves, and installs **no native overrides of its
own** — every boot service is a measured leaf or the framework's stock CD seam. That is a measured
claim, not a placeholder: the loader chain is guest code.

## The boot is the guest's

the shared `spyro::BootPrefixFrameDriver` (over `spyro3_boot_facts.h`) enters the retail boot prefix `0x8002AB38` and runs it. There is no
hand-transcribed display bootstrap and no hand-owned field sequence.

| | |
|---|---|
| crt0 | `0x80059444`, which clears `0x8006C4F4`–`0x800742D0` and derives `$sp` from `0x8006C3E4` |
| game main | `0x8001200C`, whose loop is the pair `0x80055400` (update) then `0x8001E638` (draw) |
| boot prefix | `0x8002AB38`, which does not return |
| display | `0x8002A834`, whose first wait is `jal 0x8005956C` with `$a0 = 0` at `0x8002A848` |
| CD | `0x8002A7B4`: `CdInit 0x8005DB1C`, `CdCommand 0x8005E0BC`, `CdReadyCallback 0x8005DB08` |
| geometry | `0x8002A99C`, which calls `SetGeomOffset 0x8005D35C` with `(0x100, 0x78)` |
| then | `jal 0x80074DEC` with `$a0 = 1` — an address **outside the resident text**, which this port never dispatches and never treats as code |

`psx::cpu::ResumableGuestCall` owns the finite call: the return address is captured before the first
dispatch and reused on every resume, and it refuses to begin when `$r[31]` would land inside guest
RAM.
Every guest `VSync` reaches the framework's measured libetc boundary — **the library body at
`0x8005956C` never executes**, measured by a store observer armed on all six of its store
instructions against a control store proven to run in the same run — and returns as a typed
frame-boundary exit the driver satisfies by delivering one field. One product step presents exactly
one field.

The resume and delivery machinery is **shared**, not copied: the resume contract is psxport's
`psx::cpu::ResumableGuestCall` and `game/core/field_owner.*` is this lineage's delivery owner, and
the frame drivers compose them with their own title's constants. `fieldsPerLogicFrame`, `fieldCounter` and `rootHandlerSlot` are declared with
their measured status stated: this title has no measured field counter, no measured vblank root
slot, and no measured field cadence, so nothing is invented for them.

## Where it stops, and what is not claimed

Measured on one disc-backed headless run (`heavy.py --kind run`, offscreen, silent, unpaced):

```
[disc] opened <media>/Spyro - Year of the Dragon (USA).chd (31742 hunks, 8 frames/hunk)
[boot-native] Spyro 3 enters the retail boot prefix 0x8002AB38; game main 0x8001200C is NOT
               dispatched and libetc VSync 0x8005956C stays a frame boundary
[frameloop:error] Spyro 3's boot prefix ran 480 step(s) without returning and delivered 483
        field(s) in total (step bound 480); it is polling rather than waiting, so the field bound
        cannot see it -- ending the run at resume 0x800504F0
[runtime] run complete: fields=484 product_steps=481 presentation_fences=481
        translated_blocks=429 executed_blocks=23373822 executed_instructions=135585657
        cache_hits=23373393 cache_misses=432 host_dispatches=506 invalidations=9365575 faults=0
[executor] fallback_blocks=0 fallback_instructions=0   (every reason 0, refused 0)
```

**480 of 480 boot steps, zero interpreter fallback, one real disc sector read.** A second,
independently armed run (`PSXPORT_REACH_REPORT=…`) reproduced every one of those numbers exactly —
the same 480 steps, the same resume `0x800504F0`, the same 23,373,822 blocks and the same 432 cache
misses — so the boot prefix is deterministic, not a race.

The stop is `0x800504F0`, `lw $ra,0x1c($sp)`, the epilogue the loader's wait returns through. The
guest spends its 480 steps in the spin at `0x80050650`/`0x80050658`:

```
80050620  sw    $v0,-0x1b80($at)     0x8006E480 = 1, arm the read
80050648  jal   0x8005D96C           CdRead(dest, src, 0x80)
80050650  jal   0x800503F8           the wait
80050658  bnez  $v0,0x80050650       spin until the wait returns 0
```

The wait returns "busy" while `0x8006E484` (bytes so far) is below `0x8006E488` (bytes wanted), and
only the `CdReadyCallback` at `0x80050504` — which the guest registers itself at `0x80050488` — can
advance that counter. The framework's synchronous stock `CdRead` reads the sector from the image and
returns without ever queueing a controller response, so INT1 never rises and the registered slot is
never dispatched. The exact required framework change is written out in
`docs/issues/0153-spyro-3-boot-prefix-runs-the-retail-boot-through-light.md` section 3; it is not
made here, because `~/repo/psx/psxport` is not this repository's to edit.

**Guest VSync did not execute, measured with a control.** `0x8005956C` appears in the run's
function-reach report, but that report records addresses *dispatched at*, not executed — reach
`observe()` runs before the host-dispatch classification. The store observer settles it: armed on all
six store instructions in the VSync body plus the control `0x80050620`, **6 of 6 VSync stores never
executed across 135,585,657 JIT instructions while the control executed**.

**Not claimed:** no gameplay (`game main 0x8001200C` is never dispatched), no picture (481
presentation fences is the frame contract, not a rendered scene), no `setGeomScreen`
interception, and nothing about the GPU-timeout arm/check pair.
