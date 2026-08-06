---
id: C161
kind: claim
status: holds
created: 2026-08-06
tags: memcard,bios,framework,blocker
depends: external/psxport/runtime/recomp/memcard.cpp#file_read
---

## Claim

psxport's BIOS card file API returns the WRONG value from read/write for a libmcrd consumer: Spyro retries B0:0x34 read and B0:0x35 write until they return 0 (transfer accepted; completion arrives as a card event), and memcard.cpp returns len instead — an infinite loop inside the vblank callback that stops the whole frame loop. FRAMEWORK FIX, not landed.

## Evidence

Guest loops read from the substrate (a second tool over the same executable): generated/shard_0.c gen_func_80066F34 L_80067018 'func_800684D4(c); if (v0 != 0) goto L_80067018' and generated/shard_2.c:8502-8509 'func_80062FC4(c); if (v0 != 0) goto L_800672D4'. Stub identity from the emitted thunks: gen_func_800684D4 sets r10=176 r9=52 (B0:0x34 read), gen_func_80062FC4 sets r10=176 r9=53 (B0:0x35 write). Framework side: runtime/recomp/memcard.cpp file_read line 324 and file_write line 351 both set c->r[V0] = len. Live gdb on the hung process, two card states: with a BASCUS-94228SPYRO save present the stack is '#2 file_read (memcard.cpp:321) #3 card_hle_b0 #4 Hle::dispatchBios(table=B, fn=52)'; with a blank card it is '#3 gen_func_800671F0 at generated/shard_2.c:8508' (the write call). Both runs stopped presenting entirely. Corroboration that 0 is the expected value: the state immediately after each loop waits on the memory-card completion EVENT, so the call only STARTS the transfer; and the game runs on real hardware, which it could not if the BIOS returned len.

## What would falsify it

read/write on a PSX memory-card fd shown to return the byte count on real hardware while Spyro still boots past this screen, or another psxport consumer that requires the byte-count return from the same handlers

## CORRECTION 2026-08-06 — the CONTRACT was right, the LOCATION was wrong for WRITE

The retry-loop RE and "0 means accepted, completion arrives as a card event" are confirmed and now
fixed. But this claim named `memcard.cpp` `file_write` line 351 as the code that returns `len` on the
write path, and **that function was never reached for B0:0x35 at all**. `Hle::dispatchBios`'s B-table
had its own `case 0x35` above the `default: card_hle_b0(...)` arm, which returned `len` for every fd
and only wrote to stderr for fd 1/2 — so every memory-card WRITE the framework ever saw was silently
discarded. Measured: `PSXPORT_DEBUG=card,bios` on the repro logs 3,986,996 `B0:0x35(fd=3, ...)` lines
and zero `[card]` lines (`scratch/mcfix/logs/fix2.log`). See C164.

The READ half of this claim stands as written: there is no `case 0x34` in that switch, so read did
reach `file_read`, and `c->r[V0] = len` there was exactly the defect described.

**Method lesson, recorded because it nearly repeated:** the first version of the hermetic test called
`card_hle_b0` directly and was GREEN against the broken build. A test must enter at the same door the
guest does (`Hle::dispatchBios`), or an earlier arm of a dispatcher is invisible to it.
