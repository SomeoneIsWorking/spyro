---
id: 5
title: The read-wait exit condition: CD status bit 0x40 at 0x800774B4 has no producer
status: resolved
symptom: func_80016500 polls forever after ReadN. The cd_sync override IS firing (gdb breakpoint confirms, called from gen_func_80016500), and returns 2 as the loop wants — but the loop still never exits.
tags: cd,boot,blocker
created: 2026-07-28
updated: 2026-07-28
---

## The loop, decoded

    r16 = 2;  r18 = 0x80076BB8
    L_8001654C:
      if ([0x80076BB8] != 0)        -> retry
      if (CdSync(1,0) != 2)         -> retry      <- satisfied by our cd_sync override
      if ([0x800774B4] & 0x40)      -> SUCCESS (a0=14, exits to the read path)
    retry:
      func_800163E4(); func_8005637C(); func_8002BBE0(); goto L_8001654C

So two of the three exit conditions already hold. The one that never does is **bit 0x40 of the CD status byte at 0x800774B4**.

## Why it never gets set

On hardware that status is refreshed by libcd's CD **interrupt callback**, from the controller's response bytes. This runtime raises no guest interrupts, so nothing ever updates it.

The framework already has the seam for exactly this: `GameConfig::cdCallbackTable` (the four guest-RAM slots libcd's init writes its event callbacks into) and `cdCallbackFn` (the fn-ptr values), consumed by `Cd::hleInit()` — which leaves RAM in the state libcd's own success path would leave it. **Spyro's are all still 0**, because that group has not been RE'd.

## The wrong fix, named so nobody reaches for it

Poking `[0x800774B4] |= 0x40` to make the comparison pass is a magic constant that makes the symptom disappear without a producer for the value — the exact bandaid shape the project rules ban. The status would then be a lie the moment anything else reads it.

## The right next step

RE Spyro's libcd callback installation (the CdInit success path, around func_800653B4 / func_8006397C) to find:
- the four callback table slots -> `cdCallbackTable`
- the callback fn-ptr values -> `cdCallbackFn`
- what the callback writes into 0x800774B4, and from which response bytes

Then `Cd::hleInit()` can leave the same state the real init would, and the native CD path can update the status the way the IRQ would — which is how the reference consumer does it, and keeps ownership top-down rather than poking flags.

### Resolution (2026-07-28)
SUPERSEDED / WRONG — see issue #7. The premise was false: the CD status bit 0x40 at 0x800774B4 is NOT missing. In-process logging shows it set on every iteration from the first. The boot is blocked by the GATE at 0x80076BB8 (==1), which the wait loop tests BEFORE the status, so the status test is never even reached.
