---
id: 164
title: Spyro 3's loader stalled in state 7 because every stock CdRead completed instantly, and its CD completion was lost behind a VBlank claim
status: resolved
symptom: after New Game the loading screen (gamestate 5, loader state 7) never advanced; later, with that fixed, completions never reached the ready callback
tags: spyro3,cd,anti-tamper,interrupts
created: 2026-10-01
updated: 2026-10-01
---

## Answer

Two framework defects, both fixed in psxport `s23-play` commit `2bb8d2f6` (local; not pushed, so
`psxport.pin` is not bumped, see S025):

1. **Instant completion.** `cd_read_stock_sync` queued the data-ready completion the instant the read returned. Spyro 3's
   loader (`0x8002CA50`, called each frame from gamestate 5 sub-state 1) enters its anti-tamper scan only while the
   busy check `0x800503F8` returns nonzero (`[0x8006E48C]` set, or `[0x8006E484] < [0x8006E488]`, the bytes-so-far
   against the total the ready callback advances). The scan cursor `[0x8006C6B8]` walks `0x8000D8CF .. 0x8007B6E4`
   (about `0x7000` per ten busy fields; measured live 2500 to 2670), and loader state 7's handler
   `0x800750CC..0x800750F8` leaves for state 8 only when the cursor equals `0x8007B6E4` (`bne $v1,$v0` at `0x800750EC`).
   With no busy field the cursor stopped at `0x8007660E` and the screen froze. A diagnostic write of `0x8007B6E4`
   into the cursor let the game continue (the falsifier, then removed). The completion is now owed to the
   controller's drive clock: the existing deterministic seek model from where the last read left the head, then one
   sector period per sector at the read's CdRead mode (`cdc_post_data_ready_after_read`, serial, at most four
   outstanding; `cdc_next_deadline_ticks` includes it so the Lightrec segment cap ends there).
2. **Claim blocked the CD entry.** `Hle::irqPoll` skipped the framework's CD entry when any guest element claimed
   the interrupt. Spyro 3's only element has verifier `0x8005FC90` (tests `I_STAT&1` through the pointer at
   `0x8006B548 = 0x1F801070`, so VBlank) and handler `0x8005FCF8`, which writes I_STAT with `0x77F` and leaves bit 2.
   Every CD completion that arrived with a VBlank was never delivered. The entry now also runs when the CD bit is
   still set after the claim; a handler that serviced CD acknowledged it (negative test in
   `tests/test_cd_ready_delivery.cpp`).

## Consequence for the boot facts

Real drive time lengthens the boot: Spyro 3's boot prefix now returns after 887 fields in 669 steps (it was 546 and
336 with instant reads), over the shared 480-step bound. `titles/spyro3/core/spyro3_boot_facts.h` declares its own
1024-step bound beside that measurement (`tests/test_boot_prefix_facts.cpp` pins it). Spyro 2 returns after 606 fields
in 359 steps and keeps the defaults.

## Result

Spyro 3 reaches Sunrise Spring gameplay (S025, issue 0167).
