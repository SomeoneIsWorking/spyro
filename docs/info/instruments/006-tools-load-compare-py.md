---
id: I006
kind: instrument
status: trusted
created: 2026-10-01
---

## Instrument

`tools/load_compare.py` — M1 (payload) and M2 (terminal state) of `docs/issues/0155` §7, and the R6
XA-gate question, for Spyro 1's loading. It drives the title's own oracle route (`tools/oracle_spyro1.py`,
the `artisans` policy: save picker → `GS_Playing` → settled play → twelve gameplay segments) on BOTH
cores at once and then compares what each did: the port leg is the `LoadLedger` report
(`PSXPORT_LOAD_LEDGER`), the reference leg is the pinned full-console core's CD loader entries read by
its bounded PC observer, whose payload digests this tool computes itself from the disc's WAD archive
rather than from either core.

It reuses `external/psxport/tools/oracle/compare.py` and `compare_cores.py` for the driving and
`tools/ram_compare.py` for the field-list comparison; it forks neither. The site denominator is read
from the ledger's own coverage lines, so the 31-site census table is not restated here.

Two facts about the harness it depends on, both learned the hard way and worth keeping:

* `compare_cores.NativeReplSession.close()` sends `quit`, and `quit` **detaches** the REPL and lets the
  game keep running (`runtime/psx/repl.cpp` says so in as many words). The harness then SIGKILLs the
  product and every run-end reporter is skipped — including the LoadLedger report M1's port leg IS.
  `end` is the request that ends the run. Losing the ledger this way looks exactly like "the run
  issued no reads".
* The reference is launched with the framework checkout as its working directory, so a relative
  `--bios` path resolves against the pinned worktree, not against the port repository.

## Validated by

CTest `load_compare_selftest`, which plants every answer:

* the ledger-report parser reads three rows, the coverage denominator and the unreached list, and
  **REFUSES** a report with no per-operation header, no coverage line or a zero denominator — an
  unreadable report would otherwise compare as zero operations on both sides and report M1 as passing;
* the observer-record parser reads the load registers and ignores a non-loader target, and refuses a
  record with too few registers rather than reading it as zeros;
* the payload multiset compares equal (order not compared), and unequal four ways: one differing
  digest, one missing operation, one extra operation, and **the archive truncated by one sector**,
  where the sectors before the truncation still hash identically and the ones past it are refused;
* M2's field list goes through `tools/ram_compare.py`, and a capture pair differing in one clock and
  one hand-off field must report exactly those two, classified differently;
* the destination-range hash changes when one word of the range changes.

On the product and the reference over one route (2026-10-01, `docs/issues/0155` "M1 and M2 measured,
R6 decided"): **28 of 28 payload operations identical including every SHA-256**; **0 hand-off field
differences at all four load terminals**; **23 of the 31 census issuer sites exercised, 8 named
unreached**; the archive-truncation negative reporting four operations UNEQUAL and exiting 1.

## Known failure modes

* **A destination range is only a payload test while it still holds its payload.** At the first
  terminal all 28 are byte-identical; at later terminals both cores have consumed their own staging
  buffers at their own rates, and a bare "the ranges differ" line would read as a loading defect. The
  tool therefore hashes each range in both cores AND against the medium, and prints which core still
  holds the payload.
* **§7's strict tuple includes the issuer site, and the port cannot fill that column in for a read
  its own native boot owner dispatched** (`$ra - 8` names a `jal` only when the caller was guest
  code). Six of 28 rows differ in that column alone, with byte-identical payloads, and the reference
  names the five census sites the port records as `0xDEACFFF8` / `0x8002D4A4`. The verdict is the
  payload verdict and the site gap is printed as its own line.
* It compares RAM, not pixels, and it shares the pinned Beetle device lineage with the product, so
  agreement does not independently validate a device bug both cores share
  (`external/psxport/tools/oracle/CONSOLE.md`, "Comparison validity").
* The live legs need the disc, the full-console reference's activity lock and exclusive use of the
  machine's one `spyro_port`, so they are maintainer-run and cannot be part of an unattended gate.