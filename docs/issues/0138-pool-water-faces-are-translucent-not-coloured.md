# 0138 — the pool water is a translucent world-chunk face, and the port was drawing it with a per-block colour the guest throws away

`scratch/screenshots/field-16x9-interp.png` (684x240) shows the Artisans pool rendering as
per-block colour noise while the actors, hedges, towers and buildings are all clean, with a blue
rectangle outlining the pool. A per-block COLOUR fault with every other surface clean is a per-block
DATA fault, so the question was never "which shader" — it was "what colour does a water face
actually carry".

**It does not carry one.** The guest computes the authored per-face colours for every world face and
then, for the water faces, discards them and writes a constant instead. The port was reading them.

## The mechanism, read out of the image

`SCUS_942.28`, guest renderer `func_800258F0` (`0x800258F0`, the S_World renderer, the producer the
census names `world:static`). Every instruction below was read with `tools/probe_guest_disasm.py`,
which reports 62,183 of 62,183 listed main-image instructions agreeing with
`file_offset = 0x800 + (addr - 0x80010000)`.

**The chunk header and the per-face payload** (the same walk the native codec implements):

| guest | instruction | meaning |
|---|---|---|
| `0x8002623C` | `lw $at, 0x8($t7)` | chunk origin word, +0x08 |
| `0x80026240` | `lhu $s6, 0xE($t7)` | chunk origin Z (u16), +0x0E |
| `0x80026258` | `lw $t5, 0x10($t7)` | **descriptor**, +0x10 |
| `0x8002625C` | `addi $t7, $t7, 0x1C` | payload base, +0x1C |
| `0x8002627C` | `andi $s0, $t5, 0xFF` | vertex count = descriptor & 0xFF |
| `0x80026364` | `addi $s4, $s0, -0x8` | **colour array base** = payload + vertexCount*4 |
| `0x80026368`–`0x80026380` | `srl`/`andi 0x3FC`/`add` | **face array base** = colour base + colourCount*4, colour count = (descriptor >> 8) & 0xFF |
| `0x80026374`–`0x80026380` | `srl 13`/`andi 0x7F8` | face array end = face base + faceCount*8, face count = (descriptor >> 16) & 0xFF |
| `0x80026394` | `lw $t6, 0x4($s5)` | the face's SECOND word is the material word |

`game/render/world_chunk_codec.cpp:46-85` implements exactly this and is therefore **correct**; that
was checked field by field and is a negative result worth recording, because it is the layout the
water reading was suspected of and it is right.

**The per-face colours.** The material word carries four 6-bit colour indices, and the guest loads
the four corresponding 4-byte words out of the chunk's colour array:

| colour | guest | field |
|---|---|---|
| colour 0 | `0x800264D4` `add $a3,$a3,$s4` + `0x800264D8` `lw $t1,0($a3)`, index from `srl $a3,$t6,24` + `andi 0xFC` | material bits 26–31 |
| colour 1 | `0x800264DC`–`0x800264F0` | bits 20–25 |
| colour 2 | `0x800264E8`–`0x800264FC` | bits 14–19 |
| colour 3 | `0x800264F8`–`0x8002650C` | bits 6–11 |

`game/render/world_lq_recipe.cpp:36` already decodes those four fields for both the vertex indices
and the colour indices, and both agree with the guest.

**The water arm, and the whole defect:**

| guest | instruction | meaning |
|---|---|---|
| `0x8002651C` | `andi $a3, $t6, 4` | **material bit 2 is the translucent/water decision** |
| `0x80026524` | `beqz $a3, 0x8002657C` | bit 2 clear → the gouraud store at `0x8002657C` |
| `0x80026534` | `sw $a1, 0($fp)` where `$a1 = 0x02000000` | command word |
| `0x8002653C`–`0x80026544` | `sll $a1,$t6,5` / `andi $a1,$a1,0x7F` / `ori $a1,$a1,0x600` | low half of the colour |
| `0x8002654C` | `ori $a1, $zero, 0xE100` | high half of the colour |
| `0x8002657C`–`0x8002659C` | `sw $t1,4($fp)` … `sw $t4,0x1C($fp)` | **the four authored colours, stored ONLY on the bit-2-clear path** |

So the colour word is `0xE1000600 | ((material & 7) << 5)`, whose code nibble (bits 24–27) is `1`:
a **50/50 semi-transparent black**. That is Spyro's pool water — a tinted sheet over the pool floor,
not a surface with a colour of its own. The same sequence appears for the triangle form at
`0x800266D4`–`0x80026704` (command `0x32000000`), and `0xE100`/`0x600` appear **nowhere else in
`r_environment.s`** — grep for `00E10534` returns exactly two hits, both inside `& 4` branches. The
HQ section (from `0x80026788`) contains neither, so this is an LQ-path fact.

**Why the symptom is per-block colour noise and nothing else.** The pool is a grid of such faces.
The console blends one constant 50% tint over the floor across all of them; the port blended each
face's own colour-array entry, so every block of the surface got its own tint. Every other surface
in the game takes the `bit 2` clear arm, which the port already implemented correctly — hence clean
actors, hedges, towers and buildings, and one broken material.

## The fix

`game/render/world_lq_recipe.cpp:appendFace` now derives the constant from the material word and uses
it for the translucent arm:

    const bool translucent = (source.materialWord & 4u) != 0u;
    const uint32_t translucentColor = 0xe1000600u | ((source.materialWord & 7u) << 5);
    face.vertices[i].rgb = translucent ? translucentColor : chunk.colors[colorIndices[i]];

The colour-index bound is still checked on both arms, because the guest performs those four loads
before discarding them.

`tests/test_world_scene_prepare.cpp` pinned the OLD behaviour — its fixture's face has
`materialWord = indices | 7`, so bit 2 is set, and the assertion read
`vertices[0].rgb == 0x00123456u`. That assertion was encoding the defect. It now pins the constant,
and a second case clears ONLY material bit 2 in a RAM copy and requires the authored colour back, so
the gouraud arm is covered too. A fix that pinned only the new answer would pass with the arm every
other surface uses broken.

## What this does NOT establish

- **Not verified on the screen.** No product run was made (no slot), so the pixel result is a
  prediction from the bytes, not an observation. The predicted picture is a uniformly tinted
  translucent pool.
- **The blend MODE is unverified.** `0xE100`'s code nibble says 50/50. What the port's
  `semiTransparent ? 1 : 0` and `tp_blend` actually do to the rasterizer was not read, and it is a
  second, independent candidate for residual water error.
- **The pool's blue outline is unexplained.** It is drawn by something and no producer in
  `scratch/logs/drive.log`'s 13 rows is named for it. Not touched here.
- **The HQ world path was checked for this shape and does not have it** (`0xE100` is absent from
  `0x80026788` onward), but `world_hq_recipe.cpp` derives colour through
  `world_material_codec::fogColor`, which was not audited against the HQ bytes.

## The decompilation pipeline this came out of

`tools/decomp.py` (+ `decomp_image`, `decomp_targets`, `decomp_manifest`, `decomp_ghidra`,
`decomp_prescript`, `decomp_postscript`), registered as CTest `decomp_selftest`. The finding above
was reached by reading the bytes with `tools/probe_guest_disasm.py` and the vendored listing, and
then **confirmed independently by the Ghidra decompile**: `scratch/decomp/spyro1_text/out/800258f0.c`
contains `*(undefined2 *)((int)DAT_800757b0 + 6) = 0xe100;` twice and the constants `0x3a000000` and
`0x32000000` once each, at the two `& 4` sites and nowhere else.

**The control FAILED and the pipeline says so.** `0x800258F0`'s decompile is missing the
environment-animation phase (0x800259FC–0x800261A0): `g_EnvironmentAnimations` `0x80078560`, the GTE
`INTPL` word `0x1100984A` and the GTE `DPCS` word `0x1000784A` are all absent from the C, and Ghidra's
own header says why —

    WARNING: Instruction at (ram,0x80025b74) overlaps instruction at (ram,0x80025b70)

`0x80025b70` is a branch and `0x80025b74` its delay slot. `decomp_manifest` rule 6 refuses the body
for missing those three anchors, and the CLI reports it. The decompiled disassembly of the SAME
function is complete (4,994 instructions, `0x800258F0`–`0x8002A6F8`, ending `jr ra` / `_nop`), so the
bytes are right and the loss is in the analysis, not the import. `tools/decomp_prescript.py` names
the four analyzers that guess and is where the fix belongs; **it currently disables none of them**
(`GhidraScript.setAnalysisOption` raises "No matching overloads found" for both the `(str, bool)` and
`(str, str)` forms under PyGhidra), and it prints that it did nothing.

**So the water finding does not rest on the Ghidra output.** It rests on
`tools/probe_guest_disasm.py`, which validates 62,183 of 62,183 recorded instructions against the
offset formula and prints every instruction word, and on the vendored listing at
`external/spyro-1/asm/renderers/r_environment.s`, whose printed words the same tool reproduces. The
Ghidra run is corroboration for the water constant specifically and is **not** trusted for anything
else.
