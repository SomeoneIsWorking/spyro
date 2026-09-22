---
id: 128
title: "The crash after THE ADVENTURE BEGINS is the particle producer refusing type 3 — and the refusal path killed itself before saying so"
status: open
symptom: the product renders the "THE ADVENTURE BEGINS..." flyby card and then dies. REPRODUCED 2026-09-22 on the player launch path: the field particles producer refuses particle type 3, and abortUnimplemented then segfaults on a null Core::cfg two lines into its own report
state_items: S011
tags: crash,render,producer,transition,repro
created: 2026-09-19
updated: 2026-09-22
---

## The observation outranks the runs

Reported again 2026-09-19 with a screenshot of the card. The same observation falsified claim C228
on 2026-08-27, where it was recorded as "renders THE ADVENTURE BEGINS transition card and then
crashes" — so this is a standing, unresolved user-visible defect, not a new one.

Every agent run below reaches `GS_Playing at frame 6360` and exits 0. That does not weigh against
the report; it means the runs do not cover it. C228's own falsification says exactly this about the
run that preceded it.

## What the card is

`GamestateCutsceneTransition` (`external/spyro-1/src/gamestates/update.c`) — the flyby where Spyro
arcs across a black screen while the level loads, drawn by `func_8001E6B8` with the string chosen
from `g_TitlescreenState.m_DemoType` and `g_VisitedFlags[0]`. It runs at `g_Gamestate == 13`
(GS_TitleScreen) with `m_Mode == TSM_Demo`, `m_State == TSS_Active`. "BEGINS" rather than
"CONTINUES" means a fresh save.

## Reproduction matrix — all NEGATIVE

| varied | value | result |
|---|---|---|
| build | `build/bin` (agent) | reached GS_Playing |
| build | `build/player/bin` (the one run.sh launches) | reached GS_Playing |
| window | `PSXPORT_VK_WINDOW=1` under Xvfb (swapchain up, windowed present confirmed) | reached GS_Playing |
| pacing | `PSXPORT_NOPACE=0`, real-time | reached GS_Playing |
| settings | user's `psxport_settings.ini` (`aspect=3 fps60=1`, both confirmed active in the log) | reached GS_Playing |
| overrun | 900 and 3000 frames past GS_Playing | reached GS_Playing |
| all of the above together | windowed + paced + user settings + 900-frame settle | reached GS_Playing |

## What is NOT yet covered, and is where it must therefore be

- **Audio.** Every agent run keeps `PSXPORT_NOAUDIO`; the player path removes it. Untested here
  deliberately — an unattended run must not seize the audio device.
- **Real SDL input and the hand-navigated menu route.** The driver injects pad state through the
  REPL and takes its own path through the save picker. The user's route through the memory-card
  screens, and what is on their card, may differ.
- **Their GPU.** These runs used llvmpipe under Xvfb, not the real Vulkan device.
- `PSXPORT_DEBUG_SERVER=1` (player) versus `PSXPORT_REPL=1` (driver).

## The likely shape, from the code

`SpyroRenderer::renderScene` aborts by design on a stage with no producer or a producer that refuses
its recipe, printing:

```
[render:error] NATIVE RENDER NOT IMPLEMENTED — stage selector = N (...)
[render:error]   fatal boundary: guest pc=... stage=N/x/y load_stage=... state_switch=...
```

Stages 1, 8, 9, 13, 14 and the field stages all have producers now, so an unregistered stage is
unlikely; a producer REFUSING a recipe is the candidate. Issue 0103 is the same abort on stage 8.

**That line names the stage and the refusing producer.** It is the whole diagnosis, and one line of
it is worth more than another day of negative runs.

## REPRODUCED 2026-09-22, and why nine runs had missed it

Not by driving harder. By running the product the way the PLAYER launches it — `player_environment`,
no pad input at all — and letting the attract demo play itself into gameplay. Every previous run
drove to `GS_Playing` through `tools/drive.py`, which takes its own route and then stops; the demo
keeps going and reaches a scene with type-3 particles in it. The dimension that mattered was not
audio, window, GPU or pacing. It was *not steering*.

```
[render:error] NATIVE RENDER NOT IMPLEMENTED — stage selector = 0 (particles — particles producer
  0x800573C8 refused its atomic type-0/type-2 recipe: status=unsupported_type why=particle_type
  type=3 slot=801C3C38 records=5 points=0 lines=0 type2=0)
[render:error]   fatal boundary: guest pc=0xDEAD0000 ra=0xDEAD0000 sp=0x801FFFF8 stage=0/3/2
```

Deterministic: same refusal, same slot, on every run of the route.

### Why the diagnosis never reached anyone

`abortUnimplemented` printed those two lines and then **segfaulted**, in the one function whose
whole job is to explain itself. `mC->cfg->overlaySlots`: a title need not declare a legacy
`GameConfig` and Spyro never does, so `Core::cfg` is null for the entire run. The arm, the ARMED
field backlog, the projection state and the 2 MB RAM snapshot below it were never written, and the
operator saw a segfault rather than a named refusal. Same class as issue 0090.

Fixed here: the null case is reported as the defined answer it is ("this title declares no
GameConfig"), not skipped. The full report now prints, ending at the intended `abort()`.

### The run log is no longer thrown away

A player's run wrote its diagnostics to a terminal and nowhere else, which is the only reason this
took a month: the abort text existed on the operator's screen every time and was never capturable.
`player_environment` now defaults `PSXPORT_LOG_FILE` to the host's user-state location
(`$XDG_STATE_HOME/psxport/<title>/last-run.log`, `~/Library/Logs/...` on macOS,
`%LOCALAPPDATA%\Logs\...` on Windows), creating the directory and clearing the previous run's file.
It is a default: a caller that sets its own path keeps it. Gated by
`external/psxport/tools/port/test_launch_environment.py`.

## What type 3 is

From `external/spyro-1/asm/renderers/r_particles.s` (the renderer is still hand-written assembly,
not decompiled). `func_800573C8` dispatches on the record's type byte at +1: 0 points, 1 lines,
2 rotated textured quads, **3**, 4, 5, and a sixth default arm — the port implements 0, 1 and 2 and
refuses the rest.

Type 3 (`.L80057A74`) emits the same 0x28-byte `POLY_FT4` as type 2, from the same texture table
(`0x80076278[byte0] -> +8*(halfword@+0x10 & 0xff)`), the same colour word at +0x0C, the same
semi-transparency bit, the same UV mapping and the same OT depth and clip tests. It differs in
exactly one thing: the corners. Type 2 rotates a single `size` through the sine table and
re-projects four model-space offsets; type 3 is **axis-aligned in screen space** with two
independent half-extents, `sizeX = (word@+8 >> 16) & 0xff` and `sizeY = (word@+8 >> 24) & 0xff`,
scaled by the RTPS depth factor (`IR0 = MAC0 >> 12`, then `GPF 0` and `>> 12`), placed as

```
xy3 = center + (hw/2, hh/2)    xy2 = xy3 - (hw, 0)
xy1 = xy3 - (0, hh)            xy0 = xy1 - (hw, 0)
```

so the vertex order is TL, TR, BL, BR, which is what makes the shared UV mapping correct unchanged.

## What the run hits next, and what it took to read it

With type 3 ported the demo route advances from frame ~2,475 to **frame 5,382**, where the regular
actor layer refuses. That refusal named an address and nothing else, because `spyro_actor_submit`
returned a bare `bool` and its reason lived on the `actordirect` debug channel — the exact shape
issue [0113](0113-secondary-actor-per-face-color-program.md) cost four days to. It now returns a
`ProducerRefusal` like every other layer, and three rounds of reading it moved the answer:

| what the abort said | what it actually was |
|---|---|
| `refused its atomic recipe` | no reason at all; the producer returned `false` |
| `recipe=3 reason=7` | `Unsupported` / `Malformed` — one value for five different defects |
| `reason=color-offset slot=1 offset=424 limit=1` | a colour array the failing arm never indexes |
| `reason=ft4 words=80000004,0001A83F` | **the unported billboard quad program at `0x8002256C`** |

Three causes behind that, all fixed:

- `Reason::Malformed` was one value covering a short primitive, a bad vertex offset, a bad colour
  offset, and a non-advancing evaluator. It is now four named reasons, and the refusal carries the
  slot, the byte offset asked for, and the size of the array it ran off.
- One of those five branches — the source cursor past the end of the stream — was unreachable: the
  compose loop's own `while (source < size)` guarantees it. Removed rather than named.
- `populate()` decoded material colour offsets for a quad with bit 2 set, which is the separate
  billboard program whose colours do not come from there. It refused as `color-offset` on an array
  the arm never touches, hiding the real gap. `evaluate()` owns that refusal and names it `Ft4`.

`0x8002256C` is the camera-facing textured billboard issue 0113 already describes: one projected
vertex, a depth-cued half-size. Record 5 of that scene is one, and it is the next arm to port.

## The billboard arm, ported

The regular renderer's own copy is at `0x800205C4` (`.L8002256C` is the secondary renderer's). It is
not a four-vertex face at all: it projects ONE model vertex, re-runs the GTE divide with DQA forced
to `0x100` and the rotation zeroed so SZ3 is that vertex's stored depth, takes IR0 from MAC0 through
a 16-bit register write, and scales two half-extents packed in the material word — `(material >> 10)
& 0x1FF` horizontal, `(material >> 1) & 0x1FF` vertical. The far edge is the centre plus half the
extent and the near edge is that minus the whole extent, so the box is deliberately asymmetric on an
odd extent. Its depth is `(depth << 2) + 4 - origin`: one vertex scaled into the range the other
arms reach by summing four, with no material depth bias and no NCLIP. The packet is a ten-word
POLY_FT4, command `0x2C`, one colour from the material's single table offset, and the three stream
words after the material as UVs with the last serving two corners.

`spyro::actor_billboard::extents` owns the geometry, `Family::Billboard` carries it through the
recipe and the face submitter, and the arm's tests move one half-extent at a time and check the
sprite shrinks with distance, because a test that restated the shifts would agree with a transposed
implementation.

With it the demo route reaches **frame 9,346**, where the secondary actor producer refuses on
`reason=face-light lighting=0x01000000/Additive` — the second per-face colour program at
`0x80021FE0`. Issue [0113](0113-secondary-actor-per-face-color-program.md) describes it and records
that nothing had ever been observed reaching it. Something has now.

The additive per-face colour program `0x80021FE0` that frame 9,346 refused on is ported as well;
issue [0113](0113-secondary-actor-per-face-color-program.md) holds it. The route now reaches
**frame 15,210**, refusing on particle type 6 — the default arm of `0x800573C8`, the same producer
whose type 3 started this.

## The particle default arm, ported

`0x800574F8` is not "type 6": it is the fallthrough of the dispatch chain at `.L800574C0`, so it
owns EVERY type from 6 up. It is also the only emit-list arm that orients its quad in the world.
One size byte and one sine-table index rotate a square about the particle's own position —
`across = size*sin >> 12`, `along = size*cos >> 12`, each corner taking one on the first projection
axis and the other on the third — and all four corners go through RTPT plus one RTPS, where types 2
and 3 project one centre and place their corners in screen space around it.

Having no centre, it sorts and clips differently, and both differences are the arm's own:

- the ordering-table key is the SUM of all four SZ values, rejected below `0x200` and at `0x8000`
  and above, then `>> 7` minus the texture's depth bias;
- the screen test is a bounding-box overlap, each side tested independently — some corner below the
  top edge, some corner above the bottom, and so on. A quad larger than the screen passes it with no
  corner on screen at all, which a per-corner test drops. Two of the four sides are comparisons on
  the whole packed SXY word, so the top edge depends on the column: row 1 at column 0 is above it
  and the same row one pixel right is below it.

Everything else — the texture table, the colour/command word, the corner UV mapping and the packet
— is what the other textured arms already do, so `field_particles::emit` was split out of
`field_particles::submit` and all three now share it; `submit` is the centre-placed clip policy the
other two share. The new arm passes real per-vertex host depths rather than one, because its quad
genuinely stands at four distances.

Two things were wrong nearby and are fixed with it. The scan skipped only type `-2` as a free hole,
where the guest's `bltz` sends every negative type to one handler that returns to the scan unless
the type is exactly `-1`. And `preflight` counted points, lines and type-2 quads against the queue
budget but not type-3 ones, so the producer promised room it had not checked for.

With it the route reaches **frame 21,034**, refusing on the paired actor `0x80023AC4`.

## The paired actor's "alternate parser" was a colour fade

`0x80023AC4` refused with `alternate/status-plane parser is active` whenever the high byte of
`g_Spyro + 0x28` was set, and the name is why it read like a large unported arm. It is not a parser.
At `.L80024B60` the renderer runs EVERY entry of the model's colour table through one GTE `INTPL`
toward a far colour packed in the same control word, writes the results to a scratch table at
`D_8006FCF4 + 0x1D00`, and points the ORDINARY parser at that copy by changing the `HI` register it
reads its colours through. Same stream, same offsets, same commands, different colours.

The control word is one packed colour plus its strength: far colour in bits 0..23, a byte per
channel, and the interpolation factor in bits 24..31, every field shifted left four on the way into
the GTE. The strongest fade a word can encode is `0xFF0` of 4096, so it approaches the far colour
without reaching it.

The arithmetic is `DPCS`, which this port already derives for the world's interpolated animation
channels. Rather than write a second copy, `intpl` and `dpcs` moved out of `world_animation` into
`gte_color_ops`, which both consumers now share; `paired_actor_color_fade` applies the transform to
the material table where the guest does, before the parser sees a primitive. `MaterialTables` no
longer carries a mode at all, and `SpyroPairedFrame::override_control` is gone — two frames with
different control words already differ in `materials`, which frame compatibility compares.

## Where the route ends now

It does not. OBSERVED 2026-09-22: the product ran the tool's whole 900-second clock and was still
going when it was killed — 101,970 drawn fields, zero native-render refusals, zero snapshots, no
`[render:error]` line of any kind. Every earlier run on this route died, the last of them at frame
21,034.

This is an observation and not a gate, and it is not this issue's close. The user's report is from
`./run.sh`: real SDL input, real audio, a real GPU, and a route a person takes rather than the one
the attract demo takes. Four causes on this route are fixed at their causes; whether the crash the
user saw was one of them is not established by an agent run that no longer reproduces it. See C228.

`tools/demo_run.py` printed that survival as `[demo] REFUSED: ... timed out after 900 seconds` and
nothing else, which is a diagnostic lying in the other direction — the outcome the route exists to
produce, reported as a tool failure. It now reports both endings with the same census.

## Next

1. Particle types 4 (`.L80057954`) and 5 (`.L80057750`) are still unported and will refuse the same
   way when a record reaches them.
2. The paired actor's fatal boundary still prints `refused its atomic recipe` with no reason; the
   reason is on the `pairedactor` channel. `SpyroPairedActorFrameState` already carries it, so the
   abort should say it, the way `0x8001F798`'s now does.

### The superseded plan

## Next

1. Capture the abort text from a real `./run.sh` session — the last ~20 lines of the terminal.
2. Failing that, reproduce with audio enabled and real SDL input on the actual GPU.
3. Do not close this on the strength of green agent runs. See C228.
