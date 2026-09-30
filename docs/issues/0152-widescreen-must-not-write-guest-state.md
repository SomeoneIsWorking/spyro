---
id: 0152
title: Widescreen wrote the widened answer into guest RAM, so the aspect setting changed gameplay state
status: fixed
symptom: the same deterministic gameplay route, run at 4:3 and at 16:9, leaves guest main RAM
  differing in exactly 9 bytes — seven in the world sector broad-visibility table at 0x800771C8 and
  two in a world animation channel at 0x800B370C
state_items: S019
tags: widescreen,guest-state,projection,sectors,particles
created: 2026-09-30
updated: 2026-09-30
---

## The rule this issue establishes

**A presentation setting may change what the port DRAWS and nothing else.** `aspect=1` is a change to
the port's own projection; it is not a change to the simulation. The test that decides it is a byte
comparison of guest main RAM over one identical route, and the number it has to print is `0`.

This is the same rule issue 0124 found for one particle arm, generalised. 0124 fixed a single
`mem_w8` that wrote the widened answer into a guest visibility byte; the defect was never confined
to that byte, and the only reason it looked confined is that nobody had an instrument that could see
the whole of guest RAM at the end of a route. `tools/drive.py --dumpram` is that instrument.

## How it was found, and the measurement that has to come first

`tools/drive.py --dumpram` writes the 2 MB of guest main RAM (plus a 1 KB scratchpad sidecar) through
the live REPL, at the same point in the route as `--shot`. Two runs that differ in exactly one
setting, dumped the same way, then answer "did this setting reach guest state?" as a byte count.

Over the recorded route — `gameplay --hold RIGHT --hold-frames 400`, which reaches `GS_Playing` at
frame 6360 — the 4:3 and 16:9 dumps differ in **9 bytes**, and every one of them is a store the
widened view made that retail's 4:3 view does not:

| address | 4:3 | 16:9 | what it is |
|---|---|---|---|
| `0x800771D0` | `0x00` | `0xFF` | `D_800771C8` + 8 — a sector the widened plane admits |
| `0x800771E4` | `0x00` | `0xFF` | + 28 |
| `0x80077206` | `0x00` | `0xFF` | + 62 |
| `0x80077223` | `0x00` | `0xFF` | + 91 |
| `0x80077224` | `0x00` | `0xFF` | + 92 |
| `0x8007722F` | `0x00` | `0xFF` | + 103 |
| `0x80077247` | `0x00` | `0xFF` | + 127 |
| `0x800B370C` | `0x07` | `0xFF` | one word of a world animation channel |
| `0x800B370E` | `0x07` | `0xFF` | the same word, second halfword |

A control is what makes the 9 a measurement rather than a diff. The same 4:3 route run twice differs
in **0** bytes, so the route is deterministic and the 9 are attributable to the aspect setting
alone. (`cmp -l` numbers bytes from 1, so its output is one greater than the file offset; the RAM
file offset is `address - 0x80000000`. Reading the raw `cmp` line as an address puts every one of
these nine one byte low, which is how a table of "seven bytes in `0x800771C8..`" can come to list an
address outside the table it claims to be in.)

### The seven bytes in `0x800771C8`: the sector table, and why it is gameplay

`0x800258F0` (the world renderer `RenderWorldChunks`) writes one byte per world sector into
`D_800771C8`. The port replaced that routine, and its replacement computed the byte with the
**widened** horizontal width. The guest then read the widened answer back:

- `0x800521C0` drops every Moby whose category (sector) byte is zero from next frame's draw list, and
- `0x80051FEC`'s update-list builder gates on the same byte.

So a presentation setting was changing which Mobys the guest updates. That is the defect, and it is
not a rendering difference that could be argued about: the guest's own next frame reads the value
the port wrote.

### The two bytes at `0x800B370C`: the animation walk, and its cause

`0x800B370C..0x800B370F` is one 32-bit word. At 4:3 it holds `0xFF07FF07`; at 16:9 it holds
`0xFFFFFFFF` — the surrounding array (`0x800B36F0..0x800B370B`) is byte-identical in both dumps, so
this is a single 4-byte write into a world chunk's colour array, not a structural difference.

The writer is the same mistake in the second place it appears. `world_scene::animate` is the one
function allowed to write guest RAM on the native world path, because retail's renderer writes the
animation channels itself. It ran its cull at `renderWidth(core)`, so at 16:9 it decoded and
**committed** the animation channels of sectors retail's 4:3 cull does not keep. A margin-only
sector's channels therefore advanced in guest RAM at 16:9 and stood still at 4:3 — a second
gameplay-visible difference, in the geometry itself rather than in a visibility byte.

Supporting measurement: at 4:3 the word reads `0xFF07FF07` and stays there over route lengths of
300, 400 and 500 held frames, so this is not a channel that animates and happens to coincide — it is
a channel retail's own 4:3 cull never advances, and the widened run advances it because it was
asked to.

## The fix: two answers, one preparation, no globals

The shape is the same everywhere it applies, and it is worth stating once because it is the whole
change:

> A culling or on-screen test that the **guest owns** is evaluated at the native 512-px plane, and
> the guest's answer is what is written. The port's own readers get the widened answer, passed
> in-process beside it.

**One shared plane rule**, in `game/core/wide_clip_plan.h`, so there is no second copy of the
widening arithmetic to drift:

- `wide::viewHorizontalInside(extent, depth, width)` — the view-space plane every hand-written
  Spyro culler writes as `4*extent < 3*depth`. At `kNativeClipWidth` it is retail's own test.
- `wide::drawnHorizontalInside(extent, depth, width)` — the drawn answer, and it **ORs the native
  answer in** rather than scaling the depth term alone. That is not defensive coding: scaling a
  NEGATIVE depth term makes the plane *stricter*, so a raw wide plane would drop a behind-eye
  acceptance retail had. `tests/test_wide_clip_plan.cpp` pins that with the concrete pair
  `(-1025, -1024)`, which is inside native and outside 684, and then sweeps 6,041 (extent, depth)
  pairs asserting `drawn ⊇ native` and `drawn == native` at the native width.

**The sector table** becomes `spyro::sector_visibility::Split` — a `guest` half and a `drawn` half,
256 bytes each, produced by one `world_scene_prepare::prepare` walk and published together by one
`world_scene_submitter::submit`. Only `guest` reaches `D_800771C8`; `drawn` goes to
`SpyroContext::drawnSectors`, and the two native readers that used to read the guest address read
that instead:

- `actor_scene_builder`'s category filter (`kCategoryVisibility`), via
  `sector_visibility::categoryVisible`, and
- `cyclorama_scene_recipe`'s portal test.

**The animation walk** runs at `wide::kNativeClipWidth` at every aspect, so a margin-only sector's
guest channels stay exactly as retail leaves them. The port still has to DRAW that sector, so its
live channels are decoded into the captured `world_source::Source` by
`world_source_animation::animateDrawnOnly` — into the Source's own copy, never into RAM. That
function is the extracted form of the identical code `world_temporal` already used for interpolated
samples, so there is one implementation of "apply a decoded channel to a Source" and not two.

**Moby planes** (`actor_scene_builder::classify_view`) return both answers from one view:
`Visibility::horizontal` is the point at which `0x8001F158`/`0x800208FC` stage a shadow, so it has
to be answered for both. The Moby `+0x51` "was drawn" byte is gameplay state — `0x8001F158` clears
it on entry and sets it after every plane, and `0x80051FEC` admits a Moby to the update list on it —
so `Frame::wasDrawn` carries the 4:3 value and the record carries the drawn plane's flags.

**Moby shadows and glow** write no guest state, so they take the widened answer whole, and they
take it from the *same* list rather than re-deriving one. `moby_shadow_list::List` is the drawn half
of the shadow list at `0x800724F4`, reset and appended by the same three passes at the same points
as the guest cursor, so `moby_shadow_recipe::derive` reads `SpyroContext::drawnMobyShadows` instead
of guest RAM. `glow_recipe::derive` takes the drawn projection and the drawn right edge as
parameters instead of calling `gpu_vk_wide_engine` itself, which is the same window the world uses.

**Type-1 line particles** (`fx_field_particles.cpp`) had the 0124 defect in its second arm: the
`+3` byte got the widened answer. Both arms now go through one helper,
`field_particle_endpoint::classify`, which returns the guest byte and the drawn decision together —
and the helper exists precisely so the third arm that will be written cannot get this wrong.

## Verified

**1. The aspect setting no longer reaches guest state.** The recorded route, run at 4:3 and at
16:9, dumping guest main RAM at the same point in each:

    cmp -l scratch/aspect/a43.ram scratch/aspect/a169.ram | wc -l
    0

Down from **9**. Both runs reached `GS_Playing` at frame 6360 and exited 0, so the two legs are the
same route. The control is the one above: the same 4:3 route against itself differs in 0 bytes. The
scratchpad sidecar (`dumpram` also writes `0x1F800000`, which the main-RAM diff is blind to)
likewise differs in 0 bytes.

**2. The margin still carries a picture.** `tools/margin_coverage.py`, native width 512, the
anchoring Spyro's owner uses (the guest's 512-column box sits at the left edge, so the widening adds
columns 512..683):

| | drawn band | left margin | right margin |
|---|---|---|---|
| before (unmodified) | 684 px at x=0 | 0 columns | 172 columns, **38,508/41,280** non-black (93.28%), 778 distinct colours |
| after | 684 px at x=0 | 0 columns | 172 columns, **38,508/41,280** non-black (93.28%), 772 distinct colours |

Identical, so the fix cost the widening nothing. The margin is not uniform and not one repeated
column, so it is revealed geometry rather than a card.

**3. The gate passes.** `ctest --test-dir build`: **111 of 111 pass, 0 failed**, with
`oracle_compare_selftest` excluded — it refuses with `another console-oracle build/run owns the
activity lock` while another agent holds that shared exclusive lock, and passes standalone on this
tree (80.4 s, `save_picker: MATCH native f1811 … console vb3163`, and `selftest: seeded byte at
0x80078A58; comparator DETECTED it`). Including it the count is 111/112.

## The coverage count is identical BECAUSE the culling half changed nothing visible

The margin number being byte-for-byte the same is not a weak measurement; it is the only outcome the
fix can have, and classifying the differing pixels says so directly.

Comparing the before and after 16:9 captures pixel by pixel:

| class | pixels |
|---|---|
| black → drawn (geometry revealed) | **0** |
| drawn → black (geometry hidden) | **0** |
| drawn → drawn (moved or recoloured) | 2,206 (1.34%) |

**Not one pixel appeared and not one disappeared**, at either aspect. So no culling change revealed
anything, and none hid anything. The drawn set is what it always was, and that is why the coverage
count cannot move: it counts non-black pixels, and a pixel that was already non-black and is still
non-black contributes the same 1 to both totals no matter what colour it is.

That is the correct result rather than a disappointing one, and the code says why:

- **Sectors never changed at all.** The pre-change `broadCull` already computed
  `horizontalInside(…, kNativeClipWidth) || horizontalInside(…, width)` — the union of the native and
  the widened plane — and used that one answer for both the draw and the table. `drawnHorizontalInside`
  is that same union, so the sector draw set is identical and only the published copy differs.
- **Moby and shaded-queue culls did widen the draw** (`classify_view`'s `drawn` half is the union
  where the pre-change `evaluate_visibility` used the native plane alone), but on the recorded route
  nothing new passed both planes into the visible margin.

The 2,206 pixels that did change are the Moby shadow and glow owners now drawing consistently with
the widened window — the two consistency changes this task asked for — and they *move* existing
pixels rather than reveal new ones.

## The 4:3 leg is not a no-op, and that is a separate, in-scope store

The same comparison at 4:3 gives 1,731 differing pixels, all in rows 71..148, and again **0
black→drawn, 0 drawn→black**. The cause is not the culling and not the shadows: it is the Moby
`+0x51` store this change begins performing.

The pre-change port never wrote `moby+0x51` at all. It now writes it, with the guest's own 4:3
answer — which is exactly what the design asked for ("any guest byte such as moby +0x51 keeps the
4:3 answer"). Measured over the recorded 4:3 route, **24** `+0x51` bytes go `0x00 → 0x01` against the
pre-change build, inside the level Moby array at `0x8016D3E8` (for example `0x8016DDE0`). The guest
reads that byte at `0x80051FEC` to build its update list, so more Mobys update, and the ground band
moves. Fidelity improves; widescreen is not involved.

The decisive point is that the store is aspect-invariant: `a43.ram` and `a169.ram` agree on every one
of those bytes, which is why the acceptance diff is 0 rather than 9.

## Deliberately NOT changed here

`0x80022A2C` (`0x80022C30`) computes `addi $a0,$v1,-0x1100` and branches `bgez` past the shadow append.
View depth is positive, so the pair is unsatisfiable and the shaded pass stages **no** shadow — a real
fidelity defect against retail. It is not a widescreen one: it holds at 4:3, where this change must
change nothing. A draft of this change corrected the sign, and measurement showed it altering the
4:3 picture, so the negated limit is now passed through verbatim, pinned by
`test_shaded_pass_keeps_the_negated_limit_it_always_had` as a NEGATIVE case. Fixing it belongs in
its own issue.
