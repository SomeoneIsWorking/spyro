---
id: 0145
title: Six of Spyro 1's thirty-five levels author a translucent low-poly face, so the water fix is live — and Artisans is one of the twenty-nine that do not
status: open
symptom: `c229e45` made `world_lq_recipe.cpp` take the guest's CONSTANT colour for faces with
  material bit 2 set, and issue 0143 measured 0 of 1783 low-poly faces carrying that bit over the
  RESIDENT level. Whether the arm can execute anywhere in the game was never asked. It is asked here,
  over the whole corpus.
tags: render,water,blend,census,reachability
created: 2026-09-28
updated: 2026-09-28
---

## 1. The answer, over every level in the disc

**Six of the thirty-five levels `WAD.WAD` carries author low-poly faces with material bit 2 set:
1,592 faces of 83,401 read.** The fix is live code. The census is
`tools/census_level_face_material.py` over `scratch/wad_census/WAD.WAD` (110,260,224 B, sha256
`7ba8961c…54a1`, LBA 37), and it reads the level DATA entries -- the ones `LoadLevelData` walks --
rather than the level SCENE entries `tools/census_level_blend.py` reads, because the sector tables
that hold the geometry are in the data entry.

| lvl | id | level | sectors | faces | bit 2 | in range | `material & 7` |
|---|---|---|---|---|---|---|---|
| 17 | 35 | MAGIC CRAFTERS / CRYSTAL FLIGHT | 219 | 3,341 | **494** | 494 | `{0:2847, 4:494}` |
| 24 | 50 | DREAM WEAVERS / HOME | 133 | 2,482 | **110** | 110 | `{0:2372, 5:110}` |
| 26 | 52 | DREAM WEAVERS / LOFTY CASTLE | 173 | 2,569 | **84** | 84 | `{0:2485, 5:84}` |
| 29 | 55 | DREAM WEAVERS / ICY FLIGHT | 202 | 4,113 | **493** | 493 | `{0:3620, 4:493}` |
| 32 | 62 | GNASTY'S WORLD / TWILIGHT HARBOR | 188 | 3,455 | **352** | 352 | `{0:3103, 5:352}` |
| 34 | 64 | GNASTY'S WORLD / GNASTY'S LOOT | 176 | 3,558 | **59** | 59 | `{0:3499, 5:59}` |

The other **29 read levels author none**: 6,424 authored sectors, 5,922 read, 83,401 faces, 0 with
the bit outside those six. **The level to drive to is level index 17, levelId 35, CRYSTAL FLIGHT:**
494 translucent faces, the largest population, spread over **95 of its 219 sectors** rather than
concentrated in one, so a camera anywhere in the level has a chance of one in frame.

**`material & 7` is a per-level CONSTANT across the whole translucent population** -- `0x04` in
levels 17 and 29, `0x05` in the other four, with no exceptions among the 1,592. That is the shape of
an authored material field, not of a mis-strided read, and it is the one control that a runaway
scan could not fake. The dry faces of the *same* levels span 1-3 distinct low bytes (`0x10`, `0x20`,
`0x30`), so the reader does distinguish values within one level.

## 2. What this settles about `c229e45`, and what it does not

**Live and verifiable.** The reconstruction is byte-faithful (`0x8002651C andi $a3,$t6,4`,
`0x80026544 ori $a1,$a1,0x600`, `0x8002654C ori $a1,$zero,0xE100`, all confirmed in
`external/spyro-1/asm/renderers/r_environment.s` against 62,183 of 62,183 verified listing
instructions), it now has 1,592 faces it can execute on, and all 1,592 pass the port's own
precondition -- their four colour indices are in range, which `world_lq_recipe.cpp:147` checks
*before* the translucent assignment, so an out-of-range face could never have rendered.

**It is not a regression, and 0143 was right that it is not the defect-frame's blue sheet.** The
sheet in the defect frame is a 50%-blended screen rectangle (0143 §1), and `0xE1000600 | (m & 7 << 5)`
has B = 0 for all eight values -- a blend with black, which desaturates. The measured sheet is
saturated blue at B = 192. **The two differ in sign on the channel that decides the picture.** The
arm is live and it is also not what produced the reported frame.

**Two of the 35 levels are not zeros at all.** Levels 18 and 20 (levelIds 40 and 42, BEAST MAKERS /
HOME and MISTY BOG) resolve **0 of 254** and **0 of 248** sectors to a decodable chunk: their
descriptors are zero in every sector, while their HQ layout words at `+0x14` are nonzero in 181 and
246. That is the guest's own doing -- `loaders.c:413-420` sets `g_SkipLowPolyWorld = 1` for exactly
`g_LevelId == 40 || g_LevelId == 42`, and `r_environment.s:800261F8 bnez $at,.L80026788` branches
past the entire low-poly section on it. The port reads the same word (`world_source.cpp:81`) and
returns before any face. **Reporting those two as "this level authors no translucent faces" would be
wrong about what the zero is**; the census reports them NOT READ, naming the skip.

The 36th level entry (index 35) has no WAD index entry at all, which is the header's own
`m_LevelEntry[36]; // Last entry is empty`. That is a level the disc does not carry, and it is
counted as UNKNOWN, not as zero.

## 3. Why 0143's zero was correct and still incomplete

Issue 0143 measured Artisans' Home over 178 resident sectors and got 0 of 1783. The census
reproduces that **exactly** -- same 178 sectors, same 1,783 faces, same six-value material histogram
`{0x00:150, 0x10:1160, 0x18:15, 0x20:245, 0x30:212, 0x40:1}`, same 0 with the bit -- and 0 is the
right answer *for that level*. It is Artisans that carries no water: its material histogram is
every value a multiple of 8, so bit 2 is clear in all six.

This is the same pattern the blend census already found and it is worth stating as one fact rather
than two coincidences: **the resident level is not representative, and that was already established
for the animation channels** (47,932 of 51,042 authored keyframes at a nonzero factor across 26 of
35 levels, from a resident level that authors none). The face-material question had not been asked
until now, and the answer is the same shape.

## 4. The controls, and the mistakes they foreclose

This repository has a documented history of instruments returning confident wrong answers: a census
that classified `lw` as a writer; a branch target OR-ed under `0xF0000000` instead of added to
`PC+4`, so loops vanished; a `jalr` filter on `rd` instead of `rs` (uniform zero); a `&`-binds-
looser-than-`+` slip that hid a 100% residency match as 0.03%; and a backward walk that reported
`0 of 14` then `0 of 44`. **A uniform result is the tell.** So:

- **THE LAYOUT IS THE SHIPPING CODEC'S, NOT A SECOND COPY.** `tools/world_chunk_layout.py` EXTRACTS
  the field layout from `game/render/world/world_chunk_codec.cpp` and refuses if that text stops having
  the shape it reads. Its selftest requires **four perturbed copies** of the codec -- descriptor
  moved, face stride changed to 16, material word moved to the record's first word, count masks
  split -- to each change or refuse the extracted layout. The material bit censused is read from
  `world_lq_recipe.cpp` itself (`& 4u`), so the census cannot be answering a different question
  from the one the port asks.
- **THE POSITIVE FIXTURE MUST BE FOUND.** A synthetic level entry with 3 bit-2 faces reports 3, and
  the selftest fails if it reports 0.
- **THE NEGATIVE IS OVER A CORPUS THE READER CAN TELL APART.** 0143's Artisans histogram is six
  values all multiples of 8, so a reader with a broken mask would reproduce its zero perfectly while
  reading nothing. The negative fixture therefore spans **four distinct low-bit combinations with
  bit 2 clear** and asserts a four-value histogram alongside the 0.
- **THE MIS-STRIDE DISCRIMINATOR.** Reading each sector's face table to the NEXT sector's address --
  the runaway 0143 measured at 27,365 faces with all 256 material bytes -- FINDS bit-2 faces the
  narrow walk does not. The selftest requires the narrow walk to find 0 where the runaway finds 2+, so
  a reported narrow zero cannot be a runaway read in disguise, and neither can a reported positive.
- **THE PATCH OFFSET, WHICH WAS THE REAL BUG.** `PATCH_POINTER_RELATIVE_TO_COMPONENT` is
  `componentStart + 4`, not `+8`. I wrote it as `+8` first; every sector then landed one word early,
  every descriptor decoded as zero, and **all 35 levels reported 0 faces** -- a uniform zero over the
  whole game, the exact signature. The selftest's `patch-offset` case exists to catch that, and
  `scratch/wad_census2/mutation_check.py` shows it going red.
- **EIGHT MUTATIONS, EIGHT DETECTED.** `+8` patch base, runaway face read, fixed 128-sector scan over
  an authored count of 0, unreadable-as-zero, material word at the record's first word, and the wrong
  translucent bit each turn the selftest red; the live control turns red when the capture's own
  sector count or its own face field is perturbed.
- **THE LIVE CONTROL PASSES.** `--control --ram` walks a real 2 MB main-RAM capture through
  `g_Environment` and reproduces 0143's Artisans census **exactly**, which is what says the offline
  walk and the shipping producer read the same bytes.

## What would falsify this

- A level DATA entry the walk did not enter. All 36 are reported with their reason, and 2 of 35 read
  levels contribute no faces *by the guest's own design*; a third source of a miss would be an entry
  whose `m_DataOffset` pointed somewhere this walk did not go. Every one prints its header.
- A chunk shape the codec's admission test rejects that the guest would have accepted. `decodeLow`
  admits 1..256 vertices and <=256 colours; a chunk with a zero vertex count is the levels 40/42
  case, and any *other* zero descriptor would be misreported as a guest skip.
- The material word not being the record's second word on some level. It is the second word in the
  listing (`0x80026394 lw $t6,0x4($s5)`) and the layout is extracted from the codec, but this census
  reads LQ faces only. The high-poly path reads a different field (`world_material_codec::classify`
  on the HQ layout), so a translucent HQ face would not appear here.
- **A live run that reaches level 35 and does not show the change.** That is the one thing this
  cannot substitute for, and it is the falsifier that matters: `g_NextLevelId` is at `0x800758B4`
  and is what a route would write, but reaching level 35 needs either a Magic Crafters portal
  traversal or that write, and `drive.py` records that the Artisans route cannot leave its level
  because the pad is camera-relative. **No product run was made for this issue.**
