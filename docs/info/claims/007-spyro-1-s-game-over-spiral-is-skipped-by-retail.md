---
id: C007
kind: claim
status: holds
created: 2026-09-30
tags: skip,re,input,census
depends: titles/spyro1/core/spyro1_field_scheduler.cpp#presentationSkipPressed
reconfirmed: 2026-09-30 22:16:34
verified_at: 2026-09-30 22:16:34
---

## Claim

**Spyro 1's game-over spiral is skipped by RETAIL on held START alone, so a census for the
Start-or-Cross mask `0x840` cannot answer whether a screen is skippable.**

`func_8002EDF0` — the update for both `GS_Respawn` (4) and `GS_GameOver` (5) — reads the game-over
arm's own input at:

```
0x8002F314  lui   $v0, %hi(g_GameOverTicks)
0x8002F318  lw    $v0, %lo(g_GameOverTicks)($v0)
0x8002F320  slti  $v0, $v0, 0x169        ; still counting -> no skip yet
0x8002F324  bnez  $v0, 0x8002F3A4
0x8002F32C  lui   $v0, %hi(g_Pad + 0x8)  ; m_Held
0x8002F330  lw    $v0, %lo(g_Pad + 0x8)($v0)
0x8002F338  andi  $v0, $v0, 0x800        ; <-- PAD_START alone
0x8002F33C  beqz  $v0, 0x8002F3A4
0x8002F344  jal   func_8003FDC8
0x8002F348  addu  $a0, $zero, $zero
0x8002F350  lui   $at, %hi(D_80075940)
0x8002F354  sw    $v0, %lo(D_80075940)($at)   ; v0 = 2, set at 0x8002F34C
0x8002F358  lui   $at, %hi(g_GameOverTicks)
0x8002F35C  sw    $zero, %lo(g_GameOverTicks)($at)
```

`0x800` is `PAD_START` (`external/spyro-1/include/gamepad.h:17`, `1 << 11`), and the whole screen
update is 373 instructions with **no** `0x840` site in it.

## Why this is a claim about METHOD and not about this one screen

`docs/findings/start-skip-map.md` established the game's skip mask by finding `andi …, 0x840`, and
every "no guest acceleration route" statement in this repository descends from that census. This
screen is the counter-example: a real, complete, guest-owned skip that the census cannot see, because
the game chose one button where the census looked for two.

The general form, which this repository has now been bitten by in both directions: **a census for one
mask reports "no skip" for a screen the game skips with a different button, exactly as a counter that
reads a tap nothing writes reports "nothing happens" for something that does.** The number is
identical in shape and opposite in meaning, and neither is distinguishable from the other without
naming the mask.

## What this does NOT claim

- That the port needs a native skip arm here. It does not: the route is guest-owned and complete, and
  a second mechanism would fight the guest's own. The port's only obligation is that a held Start
  reaches `g_Pad.m_Held`, which is the framework's pad delivery.
- That the respawn spiral (gamestate 4) shares it. It does not: that arm tests `g_Gamestate == 4` at
  `0x8002EE74` and leaves through `0x8002C8A4` + `0x800144C8`, and the held-Start site is on the
  game-over arm only.
- Any runtime observation. No drivable route reaches a death yet, so this is recovered from bytes and
  unobserved — which is stated here rather than implied by the tables in issue 0149.

## What would falsify it

- A live run that holds Start in `GS_GameOver` past `g_GameOverTicks == 0x169` and the spiral does not
  end early. That would mean the port's pad path is not delivering a held Start as the guest's
  `m_Held & 0x800`, which is a delivery defect rather than a claim failure, and it would be visible
  as the run reaching the respawn/level the slow way.
- An `lui`/`lw` pair forming `g_Pad` inside `func_8002EDF0` that the listing does not carry. The
  listing is the decomp's own `asm/nonmatchings/gamestates/update/func_8002EDF0.s` and the address
  range is `0x8002EDF0..0x8002F3C0`; `tools/probe_guest_disasm.py` over the admitted image is the
  independent read.

## Re-confirmed 2026-09-30 22:16:34

Re-verified 2026-09-30 against the admitted image after the origin/main rebase: probe_guest_disasm.py --address 0x8002F324 --count 18 prints 0x8002F32C lui $v0,0x8007 / 0x8002F330 lw $v0,0x7380($v0) (g_Pad.m_Held) / 0x8002F338 andi $v0,$v0,0x800 (PAD_START alone) / 0x8002F344 jal 0x8003FDC8 with a0=$zero / 0x8002F350+0x8002F354 D_80075940=2 / 0x8002F35C g_GameOverTicks=0, and the same 62183-instruction listing agrees with file_offset = 0x800 + (addr - 0x80010000) with 0 disagreements. The rebase moved the C++ dependency (spyro1_field_scheduler.cpp) but not a single guest byte, which is why this is a re-verification rather than a new argument.
