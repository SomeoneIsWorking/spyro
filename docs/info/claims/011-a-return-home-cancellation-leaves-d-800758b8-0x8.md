---
id: C011
kind: claim
status: holds
created: 2026-09-30
tags: skip,re,state
depends: titles/spyro1/core/spyro1_transition_skip.cpp#TransitionSkip::observe
---

## Claim

**A return-home cancellation leaves `D_800758B8` (`0x800758B8`) one step behind the natural route, and
no write inside a skip can fix it — so the arm is kept and the divergence is stated rather than
smoothed over.**

`func_8002E084` (42 instructions, `0x8002E084..0x8002E128`) is the glide:

```
0x8002E09C  lui   $v0, %hi(D_8007568C)
0x8002E0A0  lw    $v0, %lo(D_8007568C)($v0)
0x8002E0A8  addiu $v1, $v0, 1
0x8002E0AC  slti  $v0, $v1, 0x20        ; wrap at 32
0x8002E0B4  sw    $v1, 0x568c($at)      ; D_8007568C = v1
0x8002E0B8  bnez  $v0, 0x8002E0F8
    ... on the wrap: D_8007568C = 0, D_800758B8++ ; == 2 -> jal 0x8002C664
```

`func_8002C618` — the pause-menu Quit that ENTERS the glide — zeroes both counters.
`tools/re_globals.py 0x8002C618` reports exactly three stores over its 19 decoded instructions:

```
0x8002C630  0x800757D8  g_Gamestate = GS_ExitLevel
0x8002C638  0x8007568C  D_8007568C = 0
0x8002C640  0x800758B8  D_800758B8 = 0
```

So the natural route reaches the terminal on the **second** wrap, with `D_800758B8 == 2`.
`TransitionSkip::observe` dispatches `0x8002C664` on the press, at whatever point of that chain the
press lands, so it leaves `D_800758B8` at 0 or 1.

**It is bounded, and the bound is a count rather than an impression.** `D_800758B8` has 26 references
in the decomp and every writer is one of three places: the pause-menu/inventory draw `func_8001A40C`
(gamestates 2 and 3, `draw.c:1550 ++D_800758B8`), the pause-menu enter and exit
(`init.c:61`, `:69`, `:127`, `:176`), and this glide's own chain. During `GS_ExitLevel` only the
glide writes it. Its readers are `func_8001A40C` (menu text rotation, plus a `== 0` one-time setup at
`draw.c:911`) and `func_8001C694` (the gamestate 10 draw, which selects one of three full-screen
cases on it).

**It cannot be repaired inside a skip.** Making the word agree would mean writing it, which is the
phase write the skip rules forbid; and deferring the cancellation until the guest's own wrap is not a
skip, it is the screen finishing on its own.

## What would falsify it

- A RAM comparison of a skipped and an unskipped return home showing `D_800758B8` EQUAL. That would
  mean some owner re-zeroes or re-advances the word between the glide's terminal and the comparison
  point, and this claim's mechanism would be incomplete. `tools/ram_compare.py` carries the field for
  exactly this.
- A fourth writer of `0x800758B8` outside the three named above — in particular one in a WAD overlay,
  which the decomp's main-image listing cannot show. The overlays reuse these addresses, so a store
  there would be invisible to a scan of the resident text and would change who owns the counter's
  phase.
