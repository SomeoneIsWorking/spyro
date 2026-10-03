---
id: C231
kind: claim
status: holds
created: 2026-09-28
tags: render,water,census,material,reachability
depends: tools/census_level_face_material.py, tools/world_chunk_layout.py, tools/census_level_blend.py, game/render/world/world_chunk_codec.cpp, game/render/world/world_lq_recipe.cpp
---

## Claim

**Six of the thirty-five levels in Spyro 1's `WAD.WAD` author low-poly faces whose material word
has bit 2 set -- 1,592 faces of 83,401 read -- so `c229e45`'s translucent arm in
`world_lq_recipe.cpp` is LIVE CODE, not an unreachable branch. The level carrying the most is level
index 17, levelId 35, CRYSTAL FLIGHT: 494 of its 3,341 faces, across 95 of its 219 sectors. All
1,592 have their four colour indices in range, which is the port's own precondition for the arm
(`world_lq_recipe.cpp:147` checks it before the translucent assignment).**

This is the extension `docs/issues/0143` could not make. 0143 measured **0 of 1783** over the
RESIDENT level and concluded the arm was dead *in Artisans*, which is correct and was not the whole
question. The same pattern the blend census already found for the animation channels -- a resident
level that authors none, against 26 of 35 levels that author plenty -- held here too, and nobody had
asked.

**What this does NOT claim.** It does not claim the fix explains the defect frame. Issue 0143 §1
established the frame's blue is a 50%-blended SCREEN RECTANGLE at RGB(0,56,192), while
`0xE1000600 | (m & 7 << 5)` has B = 0 for all eight values -- a blend with black, which darkens and
desaturates. The two differ in sign on the channel that decides the picture. The arm is live AND it is
not what produced the reported frame; both statements hold and neither weakens the other.

## Evidence

**The corpus.** `scratch/wad_census/WAD.WAD`, 110,260,224 B, sha256 `7ba8961c…54a1`, LBA 37, already
extracted and authenticated. 79 index entries; `tools/census_level_blend.read_entries` owns the
archive index and is imported, not reimplemented.

**The data is in the level DATA entries, not the SCENE entries.** `census_level_blend.py` reads
`LoadLevelScene`'s seven components and finds the animation tables in archive entries 10, 12, 14 …
The geometry is elsewhere: `LoadLevel` case 3/4 memcpy's a `LevelHeader` off the entry's first
`0x800` bytes, case 8 reads the level data from `m_DataOffset` for `m_DataSize`, and
`LoadLevelData`'s SECOND component is the environment one --
`g_Environment.m_SectorCount` plus `g_Environment.m_SectorPointer`, each pointer
`PATCH_POINTER_RELATIVE_TO_COMPONENT`'d. The level-N DATA entry is archive index
`8 + 2N + 1` (`external/spyro-1/include/wad.h`: eight `OffsetLength` before `m_LevelEntry[36]`,
each of which is `m_Overlay` then `m_Data`).

**The patch base is `componentStart + 4`, and getting it wrong is the bug this census nearly shipped.**
`PATCH_POINTER_RELATIVE_TO_COMPONENT(dest)` is `PATCH_POINTER(*(int *)dest, componentStart + 4)`, and
`COMPONENT_START` already advanced the cursor past the size word, so `+4` is the COUNT word. I wrote
`+8` first. Every sector then resolved one word early, every descriptor decoded as **zero**, and
**all 35 levels reported 0 faces** -- a uniform zero over the whole game, which is precisely the
signature this workspace has been bitten by. The selftest's `patch-offset` case now exists only to
catch that, and `scratch/wad_census2/mutation_check.py` shows it going red.

**The layout is the SHIPPING CODEC'S, extracted rather than restated.** `tools/world_chunk_layout.py`
parses `decodeLow` out of `game/render/world/world_chunk_codec.cpp` and refuses if the text no longer has
the shape it reads: descriptor `+0x10`, payload `+0x1C`, strides 4/4/8, counts `&0xFF` / `>>8` /
`>>16`, **material word at `+4`**. The material BIT is read from `world_lq_recipe.cpp` itself
(`source.materialWord & 4u`), so the census cannot be censusing a different bit than the port tests.
A second copy of a layout is a second unverified claim about the same guest bytes, and it drifts
exactly where a re-port drifts.

**The selector is the guest's, re-confirmed against the image.** `probe_guest_disasm.py --verify-only`
reports **62,183 of 62,183** listing instructions agreeing with
`file_offset = 0x800 + (addr - 0x80010000)`, 0 disagreeing, and
`external/spyro-1/asm/renderers/r_environment.s` carries, at those exact file offsets:

    0x80026394  lw    $t6, 0x4($s5)      the material word is the record's SECOND word
    0x8002651C  andi  $a3, $t6, 4        the selector
    0x80026524  beqz  $a3, .L8002657C   bit clear takes the per-face colour arm
    0x80026544  ori   $a1, $a1, 0x600
    0x8002654C  ori   $a1, $zero, 0xE100

**The translucent population is an AUTHORED material field, not a mis-strided read.** `material & 7`
is a single constant per level across all 1,592 faces -- `0x04` in levels 17 and 29, `0x05` in levels
24, 26, 32 and 34, with no exceptions. The DRY faces of those same levels span 1-3 distinct low bytes
(`0x10`, `0x20`, `0x30`), so the reader distinguishes values within a level. A runaway scan produces
a spread, not a constant.

**Levels 40 and 42 are a guest-authored zero, and reporting them as zeros would be wrong.** Their
descriptors are zero in **all 254** and **all 248** sectors while their HQ layout words at `+0x14`
are nonzero in 181 and 246 -- real sectors with no low-poly geometry. `loaders.c:413-420` sets
`g_SkipLowPolyWorld = 1` for exactly `g_LevelId == 40 || g_LevelId == 42` ("Beast Makers Home and
Misty Bog"), `r_environment.s:800261F8 bnez $at,.L80026788` branches past the whole low-poly section,
the port reads the same word (`world_source.cpp:81`), and `world_lq_recipe.cpp:239` returns before any
face. The census reports these NOT READ, naming the skip, and counts them as UNKNOWN.

**The instrument's controls, both directions.** Positive: a fixture whose faces carry bit 2 is
FOUND, and the selftest fails if it reports 0. Negative: a zero over a corpus spanning **four distinct
low-bit combinations with bit 2 clear**, so a broken mask cannot reproduce 0143's all-multiples-of-8
zero. NOT READ: a sector pointer resolving outside the corpus is reported NOT READ with 0 faces,
distinct from the 0-bit-2 case. Zero count: an authored sector count of 0 scans nothing, and the 6
translucent words planted behind it are never read. **Mis-stride:** reading to the next sector's
address -- the runaway 0143 measured at 27,365 faces with all 256 material bytes -- FINDS bit-2 faces
the narrow walk does not, and the selftest requires the narrow walk to find 0 where the runaway finds
2 or more.

**Eight mutations, eight detected.** `+8` patch base, runaway face read, fixed 128-sector scan over a
zero count, unreadable-reported-as-zero, material word at the record's FIRST word, and the wrong
translucent bit (0x08 instead of 0x04) each turn the selftest red. The live control turns red when
the capture's own sector count, or a sector descriptor's own face field, is perturbed.

**The live control PASSES.** `--control --ram` walks a real 2 MB main-RAM capture through
`g_Environment` (`0x800785A8`) and reproduces 0143's Artisans census **exactly**: 178 sectors, 1,783
faces, 0 with material bit 2, and the histogram `{0x00:150, 0x10:1160, 0x18:15, 0x20:245, 0x30:212,
0x40:1}`. The offline walk and the shipping producer read the same bytes.

**An independent re-walk agrees to the face.** A separate walk written without the tool's
`decode_sector`, reading the same 6,424 sectors straight from the file, reports the same per-level
face counts and the same **1,592** total.

## What would falsify it

- **A live run to level 35 that does not show the change.** This is the one measurement nothing here
  substitutes for. `g_NextLevelId` is at `0x800758B4` and is what such a route would write, but
  `drive.py` records that the Artisans route cannot leave its level -- the pad is camera-relative --
  so reaching Magic Crafters needs a portal traversal the maintained routes do not express. **No
  product run was made for this claim**, and no frame evidence is claimed.
- A level DATA entry whose component chain this walk entered wrongly. All 36 are printed with their
  header and their reason, so a miss is a row, not a silence.
- A low-poly chunk shape `decodeLow` refuses that the guest would have accepted. The admission test
  is the port's own; a sector with a zero vertex count is read as the guest's skip, and any *other*
  zero descriptor would be misreported under that label.
- The material word not being the record's second word for some level. The listing says it is, and
  the layout is extracted rather than restated, but this census reads the **low-poly** path only. A
  translucent HIGH-poly face would not appear here, and `docs/issues/0143` separately measured 0 of
  5,199 HQ faces in Artisans -- that question is open over the other levels, not answered here.
