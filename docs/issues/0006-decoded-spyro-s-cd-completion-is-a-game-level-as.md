---
id: 6
title: Decoded: Spyro's CD completion is a game-level async queue, not a libcd status poll
status: investigating
symptom: The read-wait in func_80016500 needs [0x800774B4] & 0x40. Nothing sets it, so the boot spins after ReadN.
tags: cd,boot,re
created: 2026-07-28
updated: 2026-07-28
---

## The state machine (decoded from the recompiled bodies)

Spyro layers its own asynchronous CD request queue on top of libcd. Three globals:

| addr | role |
|---|---|
| `0x800774B4` | CD status word — the wait loop tests bit `0x40` |
| `0x800776C4` | pending-event code |
| `0x800776C8` | queued-request slot |
| `0x800776B0` | request argument passed to the processor |

**`func_8002BBE0` — the service routine** (called every retry iteration):

    r16 = CdSync(1, &[0x800776BC])
    r3  = [0x800776C4]                       // pending event
    if (r3 == 0) {                           // nothing pending: drain the queue
        if ([0x800776C8] == 0) return;
        func_800567F4([0x800776B0], [0x800776C8]);   // process a queued request
        [0x800776C8] = 0;
    } else if (8 <= r3 < 10) {
        if ([0x800774B4] & 0x200) return;    // busy
        if (r16 != 2) return;                // needs CdSync == Complete  <- our override gives this
        [0x800774B4] = 0x40;                 // <-- THE BIT THE WAIT LOOP WANTS
        [0x800776C4] = 0;
    }

**`func_800567F4`** is the request processor — it reads and writes the pending-event code, i.e. it is what would set `0x800776C4` to 8/9.

## So the chain that must complete

    enqueue request -> [0x800776C8] != 0
      -> func_8002BBE0 drains it -> func_800567F4 sets [0x800776C4] = 8|9
        -> next func_8002BBE0 sets [0x800774B4] = 0x40
          -> func_80016500's wait exits

Two of the three wait conditions already hold; only the status bit is missing, and it now has a KNOWN producer rather than being a mystery.

## Open question for the next pass

A gdb breakpoint on `gen_func_8002BBE0` did NOT hit within 120s, which contradicts an earlier stack profile that showed it. Either the spin has moved, or the breakpoint missed the dispatch path (calls route through the `func_` wrapper, not `gen_func_` directly). Resolve that FIRST next iteration — instrument the three globals from inside the port (a cfg_dbg channel in the override) rather than via gdb, which has been unreliable here.

## Do not

Poke `[0x800774B4] |= 0x40`. The producer is now known; faking its output would discard exactly the understanding this decode bought.
