# Every level authors a nonzero environment-animation blend factor — the census 0134 could not make

`docs/issues/0134-no-live-frame-exercises-the-blended-environment-animation.md` recorded, for
Artisans, that "all 24 of their keyframes carry blend factor `0`", and named the unblocker as "a
level that authors a nonzero blend factor byte". This is the measurement that finds them, across
the whole game, statically, from the disc.

**Answer: yes, and overwhelmingly. 26 of the 35 level-data entries in `WAD.WAD` author at least
one nonzero blend factor; 47,932 of 51,042 authored keyframes carry one.**

The correction matters, because 0134's number is not wrong — it is a census of a *different set*.
It read **one** keyframe per animation, the one the runtime index byte `animation[2]` selects at
the instant of a live capture. The authored form of an animation is its whole 8-byte-strided
keyframe table, and the table is 120 or 240 slots long. Slot 0's factor is `0` in every one of
the 739 animations in the game, which is why a census of the selected slot finds nothing anywhere.

## The two metrics, and why they differ

The renderer takes `keyframe = animation + 12 + animation[2] * 8` (`0x80025BE4`/`0x80025BE8`/
`0x80025BEC`/`0x80025BF0`), reads the factor at `0x80025BF4 lbu $a1,4($v0)`, and branches on
`0x80025C00 bgtz $a1`. The length of the authored table is not authored anywhere: it is bounded by
the record's own `base offset` word at `+8`, which is where the payload starts —
`(base - 12) / 8` slots.

| metric | what it reads | Artisans | the whole game |
|---|---|---|---|
| **SELECTED** | the single slot `animation[2]` names — 0134's metric | 24 slots, **0 nonzero** | 739 slots, **0 nonzero** |
| **AUTHORED** | every slot in the record, bounded by its base-offset word | 3,360 slots, **3,312 nonzero** | 51,042 slots, **47,932 nonzero** |

Artisans' own channel-0 record 0, 120 slots, factors in order:

    0  4  8 12 17 21 25 29 34 38 42 46 51 55 59 63 68 72 76 80 85 89 93 97 102 ...
    119 123 127 131 136 140 144 148 153 157 161 165 170 174 178 182 187 191 195 199 204
    208 212 216 221 225 229 233 238 242 246 250 | 0  4  8 12 17 ... (a looping saw ramp)

That is a linear tween: every slot names payload source A index 0 and source B index 1, the record
carries exactly two 52-byte payload copies, and the weight ramps 0 → 250/256 and snaps back. A
moving platform morphing between two authored positions — which is exactly what the GTE
interpolating path is for.

## The method, and the four ways it can lie

`tools/census_level_blend.py`, run against the `WAD.WAD` extracted from the disc with
`external/psxport/build/tools/discdump get WAD.WAD <chd> <outdir>`.

The archive format is read out of `external/spyro-1/src/loaders.c`, not guessed. A component is
`[size][count][count fields]`, `COMPONENT_END` advances by the size word, and
`PATCH_POINTER_RELATIVE_TO_COMPONENT` puts a record at `count word + stored`. `LoadLevelScene` then
reads **seven** of these back to back, in this order, which is what attributes them:

    1 texture animations   2 scrolling textures   3 HighPoly (channel 2)   4 LowPoly (channel 0)
    5 collision animations 6 HighColor (channel 3) 7 LowColor (channel 1)

so members 3, 4, 6 and 7 of a seven-component run are the renderer's four channels.

Each record's denominator is its own base-offset word, and each channel's is its own count word:

- **A count of 0 scans nothing.** The fixed 128-index scan 0134 records reading 6 fabricated
  entries is reproduced as a mutation and turns the selftest red.
- **A record resolving outside its entry is NOT READ, never factor 0.** It is a distinct outcome
  from a zero, it carries a reason, and an entry with an unreadable member is REFUSED whole rather
  than censused on its readable members.
- **A record whose payload claims more than its component holds is flagged**, because the
  component's size word is the only thing that says where a level's animation data stops.
- **A run shorter than seven is a REFUSAL**, not a census: without seven there is no channel
  attribution, and an instrument that quietly censused four of them would be inventing the
  attribution it needs.

## The control, and what it is worth

`--control --ram <a live 2 MB capture>` anchors the capture in the archive **by content** — the
record's 12-byte header *and* its keyframe bytes, because a level's LowPoly and HighPoly copies of
one animation share their keyframe table byte for byte and differ only in the header's payload-size
word, so a keyframes-only window matches twice. The anchor must be unique or the control refuses.

It then requires three things of the static parse of that entry:

1. the capture's own four authored counts — **12 / 0 / 12 / 0**, which is what 0134 measured live;
2. the **24** selected keyframes at factor **0**, which is 0134's headline number; and
3. that the capture's own LowPoly set pointer, carried across by the same constant that carries
   its record 0, lands on the component the static parse calls LowPoly. This is the only check that
   can see a reversed attribution rule, because the fixtures are symmetric in channels 0 and 2.

Measured against `scratch/live10.bin`:

    capture anchors at archive 0x9CE388, inside entry 10 (0x800800..0xB83800)
    capture's authored counts (ch0/ch1/ch2/ch3): 12/0/12/0
    control: the capture's LowPoly set maps to archive 0x9CE350, exactly the static parse's
             member 4; the attribution rule holds
    static parse's SELECTED keyframes: 24 read, 0 at a nonzero factor (0134 recorded 24 and 0)
    control PASSED

The control caught a real 4-byte error in its own first draft — it reported the capture's LowPoly
set at `0x9CE354` against the parse's component at `0x9CE350`, and the tool was right: the set
table sits 8 bytes into its component, not 4.

## Denominators

    79 archive entries scanned
    35 carried a seven-component environment-animation run
    44 carried none, and are reported UNKNOWN rather than zero
       37 of those 44 are the small code overlays (entries 2, 7, 9, 11 ... 77), 36 of which
          tools/wad_index.py independently scores above 90% valid opcodes
        7 are larger data entries this census did not identify (0, 1, 3, 4, 5, 6, 8)
    739 authored animations across the 35 levels; 739 records read, 0 NOT READ, 0 counts refused
    9 of the 35 author nothing on any of the four channels
    26 of the 35 author at least one nonzero blend factor
    15 of the 35 author a colour channel (1 or 3)
    0 records had a payload running past its own component

Per channel, over the whole game:

| channel | animations | nonzero authored factors |
|---|---|---|
| 0 LowPoly (LQ vertices) | 68 | 5,452 |
| 1 LowColor (LQ colours) | 115 | 1,233 |
| 2 HighPoly (HQ vertices) | 163 | 16,732 |
| 3 HighColor (HQ colours) | 393 | 24,515 |

**The colour channels carry more than half of them**, which is the guess 0134 left open and called
"the most likely place for one — a colour fade is what a blend is for". Entry 18's four HighPoly
records are the cleanest example: 80 slots each cycling `0x00, 0x33, 0x66, 0x99, 0xCC`, a five-step
fade with a top weight of 204/256.

## Why a live frame did not see it, and what closes S020

`func_8002A6FC` at `0x8002A6FC` is the update loop, and it walks all four channels' sets
(`lw $t7,0x10($t9)` / `0x8002AA18`, `0x18/0x1C` at `0x8002AAE8`, `0x20/0x24` at `0x8002ABB8`,
`0x28/0x2C` at `0x8002AC88`, plus the texture, scrolling and collision pairs). For each record it
**writes the index byte the renderer reads**, `0x8002A7B0 sb $v1,2($t5)` — unless the record's byte
1 has bit 1 set:

    0x8002A724  lbu  $at, 1($t5)
    0x8002A72C  andi $v1, $at, 2
    0x8002A730  bgtz $v1, <next record>       # bit 1 set -> the index byte never moves

All 24 of Artisans' records carry byte 1 = `0x02`, so its index byte is pinned at 0 and the
renderer can only ever read slot 0. **That is the real reason a live frame did not see the form,
and it is a property of the level, not of the metric.** `func_8002B390` can rewrite that flag byte
at runtime and `func_8002B444` can set `animation[2]` directly, so the index byte is not pinned by
the format — it is pinned because these particular records ask to be skipped.

The advance rule itself is **not** re-derived here: `func_8002A6FC` reads its next-keyframe byte
through a 4-byte-strided view of the same table the renderer walks at 8, and this investigation
did not finish resolving that mapping. What is established is that a record whose skip bit is clear
gets its index byte written every frame, and that 47,932 of the 51,042 authored slots the write
could select carry a nonzero factor. Which value it lands on in a given level is a live question.

Levels whose records have the skip bit **clear** are the ones to drive to, because their index byte
is written every frame. Entry 52 authors 75 HighColor animations, every one of them with flag byte
`0x00`, 40- and 60-slot tables and 3,280 authored nonzero factors:

    [census] entry 52 (WAD 0x3D88000, 2738176 bytes)
      channel 3 HighColor (HQ colours): component@0x3F47960 authored count 75; read 75, NOT READ 0
        SELECTED: 75 read, 0 at a nonzero factor
        AUTHORED: 3580 keyframes, 3280 at a nonzero factor, 3280 at an entry the port's stride
                   check would accept
        factor histogram: 0x00:300, 0x12:116, 0x1C:184, 0x24:116, 0x36:116, 0x38:184, ...
      [  0] file 0x03F47A94 slots=60 size=16 selected=0 selected_factor=0x00
           authored_nonzero=56/60 flags=0x00

Entry 18 is the smallest such case: 4 HighPoly animations, 80 slots each, flag byte `0x00`, 256
authored nonzero factors, and a five-step weight cycle `0x00, 0x33, 0x66, 0x99, 0xCC`.

**This does not close S020 on its own.** It removes the stated unblocker and names the level. What
it does *not* do is move `fieldenv blended=0` off zero in a live run, and that counter is the
recorded evidence. S020 stays `partial` until a driven run in one of these levels reports
`blended=N` with `N>0`, or a census of a live capture in one of them shows a record's `animation[2]`
above 0. The honest one-line change to S020 is that the hole is **level choice**, not level
coverage: 26 levels author the form, and the two the route reached pin their index byte.

## Reproducing

    external/psxport/build/tools/discdump get WAD.WAD "<disc>.chd" scratch/wad_census/
    uv run --frozen python tools/census_level_blend.py --selftest
    uv run --frozen python tools/census_level_blend.py --wad scratch/wad_census/WAD.WAD
    uv run --frozen python tools/census_level_blend.py --wad scratch/wad_census/WAD.WAD \
        --only-entry 10 --control --ram scratch/live10.bin

The selftest is registered as CTest `census_level_blend_selftest`. The census and the control need
the archive, and `--control` needs a live capture: without them they print `REFUSED` and exit 2,
which is the correct answer on a machine with no disc, not a pass.
