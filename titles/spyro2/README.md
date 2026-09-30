# Spyro 2

Measured bring-up for `SCUS_944.25` (USA). The exact 358400-byte executable is authenticated by
`executable.json`; binary analysis identified 683 resident function entries without foreign-title or
guessed-overlay assumptions. The retired emitted-code bring-up kept this image separate from Spyro 1,
proving that identical guest addresses cannot select title behavior by address alone. The target
runtime instead keys Lightrec cache and overrides by complete image identity plus address.

`Spyro2Runtime` is a direct runtime: it binds no Spyro 1 `GameConfig`, hooks, context, renderer, or
title behavior. It returns `PSXPORT_SPYRO2_DISC`, declares a `PlatformHlePlan` of measured library
leaves, and installs **no native overrides of its own**: every boot service is a measured leaf
(`CdInit 0x800582B8`, the GPU timeout pair `0x80057B20`/`0x80057AF4`) or the framework's stock CD
seam. That is a measured claim, not a placeholder — the loader chain is guest code.

## The boot is the guest's

The retired finite display bootstrap — three hand-owned fields and a deliberate stop at boot-prefix
leaf `0x80011B1C` — is **deleted**, not extended. the shared `spyro::BootPrefixFrameDriver` (over `spyro2_boot_facts.h`) enters the retail boot prefix
`0x80011E9C` and runs it:

| | |
|---|---|
| entry | `0x80011E9C` (`addiu $sp,$sp,-0x18 ; sw $ra,0x14($sp)`), first leaf `jal 0x800548A4` at `0x80011EA4` |
| display | `0x80011BBC`, whose VSync is the `jal 0x80058EDC` at `0x80011BD0` |
| then | `0x80011B1C`, `0x80011B3C` (CD), `0x80012B84` (music), `0x80011D24` (geometry), `0x80013810` (module load), and `jal 0x80077374` — an address **outside the resident text**, which this port never dispatches and never treats as code |
| per frame | game main's pair: update `0x8001B140` (`80011AEC`) then draw `0x800156FC` (`80011AF4`) |

`Spyro2GuestCall` owns the finite call: the return address is captured before the first dispatch and
reused on every resume, and it refuses to begin when `$r[31]` would land inside guest RAM, because
the executor ends a call whose PC equals its return address. Every guest VSync reaches the
framework's measured libetc boundary — **the library body at `0x80058EDC` never executes** — and
returns as a typed frame-boundary exit the driver satisfies by delivering one field. One product
step presents exactly one field.

The framework's `PlatformHle::vsync` also answers this title's **negative** VSync query, which the
display bootstrap uses: the counter word is `0x1F801110`, named by the executable's own pointer at
`0x80066454`. It is **not writable and is not written by this port**: the framework serves it from
`Timing::hSyncCounter`, and its own comment says root counter 1 "has a value but no writable state
here". No guest word is repurposed to stand in for it, and no guest field-counter word is claimed:
`fieldCounter` and `rootHandlerSlot` are 0 because none has been measured.

## Where it stops, and what is not claimed

The run reaches the module load and wedges in the loader's CD completion poll. The driver ends that
run **by name** instead of spinning on it: a boot prefix that has neither returned nor asked for a
field in 480 steps is refused with its resume PC (`0x80013788`), and a boot prefix that asks for
fields forever is refused by the field bound where those fields are delivered. Both refusals call
`RuntimeRun::requestEnd()`, so the process still reaches its shutdown accounting. The wedge is
because the framework's synchronous stock `CdRead` never raises the controller
completion this guest's chained per-sector reader waits for: `cd_ready_delivered + declined = 0`.
The required framework change is named with the exact function in issue 0092 section 3.

Zero interpreter fallback is **not** achieved either: the shipping default refuses at
`0x8005FFFC`, which is a Lightrec self-modifying-block false positive (a `lui` base of `0x80060000`
falling inside the block's own address range), with the mechanism and the required fix in issue 0092
section 4. It is not worked around, and a title-owned reimplementation of a library printf is not
the fix.

Captures at two different frames are **0.00% non-black** (`scratch/screenshots/f120.png`,
`f400.png`), against the same capture path's control of 93.33% on a Spyro 1 gameplay frame
(`f6301.png`) — so the zero is a measurement of this run, not of the tool.

So: no Spyro 2 logo, title screen, attract mode or gameplay frame exists yet, and no widescreen,
fps60 or interpolation claim exists for this title. Title-specific work resumes only after Spyro 1
passes its migration gate.
