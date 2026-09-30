---
id: C010
kind: claim
status: holds
created: 2026-09-30
tags: skip,re,dispatch
depends: titles/spyro1/core/spyro1_transition_skip.cpp#classify
---

## Claim

**The card a NEW GAME flies before the intro cutscene (`TSD_Cutscene`) has a recovered terminal that
the port cannot dispatch, for two independent structural reasons — so its absence from
`TransitionSkip` is a measured decision and not an unfinished one.**

The terminal, read out of `SCUS_942.28` with `tools/probe_guest_disasm.py` (file offset
`0x800 + (addr - 0x80010000)`, cross-checked against 62,183 listing instructions):

```
0x8003300C  lw    $v0, 0x5864($v0)      ; g_LoadStage
0x80033010  slti  $v0, $v0, 7          ; still loading -> jal 0x80014564 (LoadCutscene)
0x80033024  lw    $v0, -0x7280($v0)    ; m_Tick (0x80078D80)
0x80033030  slti  $v0, $v0, 0x180      ; m_Tick < 384 -> not yet
0x80033038  addiu $v0, $zero, 7
0x80033048  bne   $v1, $v0, 0x80033170  ; g_LoadStage != 7 -> not yet
0x8003304C  addiu $a0, $sp, 0x20        ; the RECT, on the GUEST STACK
0x80033058  addiu $v0, $zero, 0x200     ;   .w = 512
0x80033060  addiu $v0, $zero, 0x1e0     ;   .h = 480
0x80033070  jal   0x8005F8F8            ; ClearImage(&rc, 0, 0, 0)
0x80033078  jal   0x8005F764            ; DrawSync(0)
0x80033098  jal   0x8005B6F8            ; AllocateBuffers(1)
0x800330A8  jal   0x80014564            ; LoadCutscene   } while (g_LoadStage < 10) {
0x800330B0  jal   0x8002bbe0            ; CDMusicUpdate  }
0x800330D0  jal   0x8002d338            ; StartCutscenePlayback
0x800330D8  addiu $v0, $zero, 1
0x800330E0  sw    $v0, 0x579c($at)      ; g_StateSwitch = 1  (0x8007579C)
0x800330E4  j     0x80033190            ; THE FUNCTION'S OWN EPILOGUE
```

**Reason one: the block ends in the enclosing function's epilogue.** `0x800330E4` branches to
`0x80033190`, which is `lw $ra, 0x34($sp)` — the frame `GamestateCutsceneTransition` pushed at its own
entry. A dispatch into the middle of the block therefore runs the block and then returns through a
return address read off the guest stack. This is the same mechanism `docs/issues/0138` measured for
`GS_EntranceAnimation`'s terminal (`0x8002E070`, `0x10` bytes before an epilogue that reloads `$ra`),
and it is read off `psxport/runtime/cpu/native_dispatch.cpp` plus
`lightrec_executor.cpp`'s `blockBoundary`, not assumed.

**Reason two: the block's first call has no address to be given.** `ClearImage` receives
`$sp+0x20`, a RECT built in the guest's own frame. All twelve `ClearImage` call sites in
`external/spyro-1` build the RECT inline (`src/gamestates/update.c:760`, `:808`, `:959`,
`src/gamestates/init.c:599`, `:722`, `src/gamestates/draw.c:1701`, `:1794`,
`src/initialization.c:74`, `:390`, `src/overlays/titlescreen.c:208`, `:1045`, and the one above), so
this game has no dispatchable full-screen clear to call instead.

**The blocking load loop is NOT the obstacle** and is worth saying, because "it loads, so it cannot be
skipped" is the wrong reason: running `LoadCutscene` to `g_LoadStage == 10` is required I/O that a
cancellation must perform, not bypass.

## What would falsify it

- A framework entry point that can execute a mid-function guest block with a synthesised frame — i.e.
  something that does not rely on `$ra` surviving the block. That is psxport work, outside this
  repository, and it would make reason one go away.
- A dispatchable guest full-screen clear in `SCUS_942.28` (a function that takes no RECT, or one that
  builds its own). None exists in the resident image; the twelve call sites above are the whole
  census.
- A RECT the port may legitimately own. The guest stack is not one, and the 1 KB scratchpad is
  contended by DMA, so neither is available without its own evidence.

## What this does NOT claim

That the screen is unimportant: it is 384 fields of authored flight the player watches on every new
game. It claims only that the port cannot end it correctly today, and names what would change that.
