---
id: 28
title: Upstream CD change made the ack-advance path an unbounded loop — port went to 8 frames
status: resolved
symptom: millions of 'LBA <huge> out of range' / 'ReadN: no data' lines, port presents ~8 frames instead of ~18000, bytes-from-disc collapses to one sector
tags: cd,framework,regression,gate
created: 2026-07-29
updated: 2026-07-29
---

CAUGHT BY THE GATE, which is the point of it. Between two gate runs the numbers went: frames 18809 -> 8, bytes from disc 13178880 -> 4096, CD completions 63 -> 1, distinct overlays 7 -> 2, and 4416363 out-of-range lines against ZERO in the previous log.

CAUSE: psxport 5daf2fe4 'cdc: advance the sector on interrupt ack, as the hardware does' arrived via a rebase during an unrelated push, so it was never gated on its own. It moves sector advancement from the drain path into the ack path. That is correct for a streaming reader which reads only each sector's header and never drains one — but acking an INT1 raises the next INT1, which is acked, which advances again. Nothing bounds it. The head reached LBA 23476094 on a ~281k-sector disc.

BISECTED, not guessed: reverting 5daf2fe4 alone took out-of-range reads from 4.4M to 0.

FIX (upstream, not a revert): load_sector clears s->reading when the LBA is unreadable. A real drive cannot present sectors past the lead-out, so it stops raising INT1 — the loop ends on the hardware's own terms and 5daf2fe4's intent survives. Gate back to 14/14 with the commit in place.

TWO PROCESS POINTS WORTH KEEPING:
  * A rebase during a push silently pulls in upstream work that has never been gated against THIS consumer. The gate is the only thing that noticed. Run it after a rebase, not only after your own edits.
  * The failure LOOKED like 'the port got slow' — an early symptom was a REPL capture timing out where it used to finish. I initially blamed my own edit, then external machine load (which was real but incidental). The gate's honest per-check numbers are what separated the three.

OPEN CAVEAT recorded upstream: the fix treats any unreadable sector as end-of-media. If the streaming consumer must continue past an isolated bad sector, it needs to distinguish that from the lead-out.
