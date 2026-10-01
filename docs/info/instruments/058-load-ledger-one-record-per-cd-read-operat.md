---
id: I058
kind: instrument
status: trusted
created: 2026-10-01
---

## Instrument

`spyro::load_ledger::Ledger` (`game/core/load_ledger.{h,cpp}`), one member of `SpyroContext`, fed by
the two CD overrides in `game/core/cd_queue.cpp` and reported at run end by
`reportRuntimeRun`. It records one entry per CD read operation: the issuer site (`$ra - 8`), the
guest PC the override was entered with, LBA, byte offset, length, destination, the SHA-256 of the
payload, blocking or streaming, `g_LoadStage` at issue and at completion, whether `[0x800774B4] &
0x40` was clear at issue, and the field at issue and at completion. The one-line summary is always
logged as `[load-ledger]`; the full per-operation table is written to the path named by
`PSXPORT_LOAD_LEDGER`.

It exists because M1-M3 of `docs/issues/0155` §7 need a RECORD, not a counter, and because the
coverage denominator has to be the 31 issuer sites the census enumerates rather than the operations
a route happened to issue. It is diagnostics only: nothing reads a pending operation to make a
decision, and no guest word is written.

## Validated by

CTest `load_ledger` (`tests/test_load_ledger.cpp`), which runs the real class over a stage machine
built to the shape `docs/issues/0155` §2.3 measured: one loader call per field, the stage advancing
only on completion.

* **Positive.** One blocking read plus one streaming read per stage 2..12, each completed in the
  field it was issued: 12 operations, every one complete, every latency 0, 0 pending, and the report
  says `issuer sites exercised 2 of 31` and names `S09@0x8002EEC4 UNREACHED` — so the denominator is
  the census and not the run.
* **The other answer.** With the completion withheld — the same machine, except that the guest never
  calls the retry step that dispatches `0x80016490` — the machine stops after the first streaming
  read: 2 operations, one pending, and the stalled operation's latency is `kPendingFields`
  (`~0ull`), deliberately not a small number, because a report that printed 0 for an unfinished
  operation would claim the removal is free.
* Also covered: a 3-field measured latency, a refusal staying visible in the record, and an
  operation issued before any field owner existed being excluded from the latency maximum and
  counted in the report.

**On the product** (`tools/drive.py gameplay` to `GS_Playing`, `PSXPORT_LOAD_LEDGER=...`, 2026-10-01):
26 operations, 0 pending, worst measured latency 2 fields, 16 of the 31 named sites exercised and
the other 15 named as unreached. `docs/issues/0155`, "M3 measured".

## Known failure modes

* **`$ra - 8` names a `jal` site only when the caller was guest code.** The port's native
  `BootSequence::loadAssets` dispatches `0x80016500` directly, so those operations record a host
  return address; and one guest PETE load records the return address of the call to the *outer*
  loader function. The ledger reports these as `outside the 0155 census` with the guest PC beside
  them rather than putting a plausible-looking guest address in the site column. In the measured
  run 6 of 26 operations were unattributed, so coverage read `16/31` for a route that in fact
  exercised 20 of the census's sites. **Measured from the other side (2026-10-01, I006): the
  reference's own guest boot issues those four reads from S02-S05 with byte-identical payloads, and
  issues the PETE read from S06 `0x8005B83C`, which is what `0x8002D4A4` is a caller of.**
* It records what the overrides did. **Since 2026-10-01 those records ARE compared against retail,
  by instrument I006 (`tools/load_compare.py`): 28 of 28 payload operations identical including every
  SHA-256, and 0 hand-off field differences at four load terminals. What the ledger itself cannot do
  is attribute a read its own native boot owner dispatched — see below — and that gap is now measured
  from both sides rather than inferred.**
