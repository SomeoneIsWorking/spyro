---
id: C005
kind: claim
status: holds
created: 2026-07-28
tags: cd
---

## Claim

Spyro's CD command path is resolved: all CD timeouts are gone from the boot

## Evidence

Wired cfg->cdCommand=0x80064CEC (CD_cw) after CONFIRMING its signature by reading the recompiled body: prologue keeps a0 in r16 and uses (r16 & 255) to index the command tables at 0x800750xx, a1 in r17 (tested against 0 = no param), a2 in r21 (result), a3 in r18 — i.e. CD_cw(com,param,result,mode), matching the framework's cd_command handler. Also added the missing game->cd.overridesInit() call in main.cpp, without which the whole cd* group never installed. Boot log boot7: 3 plat-hle primitives installed, ZERO CD timeout lines (previously CdlSetmode x12 + CdlSetloc x4).

## What would falsify it

if a CD timeout line reappears in a boot log, or a later disc read returns wrong data, the command path is not actually serving reads correctly - note that ACKing commands is not the same as reads returning correct bytes, which is untested so far
