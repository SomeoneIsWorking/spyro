---
id: 169
title: Spyro 2 and 3 title routes reached gameplay at different fields run to run because every run shared one memory card, not because of host time
symptom: Spyro 3's route reached gameplay at field 4740 in two of three runs and 4890 in one (one 150-field press period later); Spyro 2's at 3340 once and 3190 after
tags: spyro2,spyro3,determinism,memory-card,drive,title-route
created: 2026-10-01
updated: 2026-10-01
---

## Answer

The memory card is an input the guest reads at boot and a file the run writes. An agent launch set no `PSXPORT_CARD`,
so every run of every title opened the one `scratch/saves/card.mcr` (psxport `memcard.cpp`'s last-resort path) and
left it changed: the previous run's writes were the next run's boot state. Nothing host-timed was involved, and the
route's input timing was already keyed to guest fields (`Port.run` returns at a REPL field boundary and `tap`/`press`
apply there).

## Evidence

**Before (stock tooling, shared card, alternating titles, 16 runs, one card started from the 2026-09-20 checkout's):**

| # | title | card before | fields to gameplay |
|---|---|---|---|
| 1 | Spyro 3 | 295df1af | 4890 |
| 2-6 | Spyro 3 | dd9501fe | 4740 x5 |
| 7 | Spyro 2 | dd9501fe (Spyro 3's) | 3340 |
| 8-12 | Spyro 2 | 995ee036 (Spyro 2's) | 3190 x5 |
| 13, 15 | Spyro 3 | 995ee036 | 4740, 4740 |
| 14, 16 | Spyro 2 | 995ee036 | 3190, 3190 |

The two odd runs (#1 and #7) are exactly the two where the card was not the one the previous run of that title had
written. The card file changed only during those runs (md5 in the table).

**The divergence point.** `PSXPORT_DEBUG=fielddigest` (psxport `runtime/psx/field_digest.h`) prints one line per display
field: emulated CPU tick, a hash of all 2 MB of main RAM, I_STAT, I_MASK, pad. Two Spyro 2 runs that differed only in
their starting card are byte-identical for fields 0..244 and differ in RAM at field 245, where the guest first reads the
card; Spyro 3 on a blank card against Spyro 3's own card first differs at field 913.

**Not host time.** Six unloaded runs and five runs with the host saturated (18 busy loops on 16 cores) printed
byte-identical digests on the same card. The wall-clock reads in the runtime (`cd_override.cpp` host stream pacing,
`gpu_perf`, `gpu_vk` fence budgets, SPU profiling, `FieldOwner::reportField`) do not reach guest state on the headless
unpaced path; the CDC deadlines, the Lightrec segment cap and the display clock all run on `EmulatedTime`.

## Fix

The Spyro 2/3 route launcher (`title_route.open_port`, which `title_conversation.py` and `determinism_check.py` also use)
now passes `card=drive.ROUTE_CARD` to `drive.environment()`: a private image deleted before each launch, through the
framework's `agent_environment(card=...)` (psxport issue 0146), so the product creates a blank card as for a first run and
no route writes the shared one. Tests: `tests/test_drive_environment.py` (the route card is private and is not the shared
default; the title routes launch on it; a card left behind is removed; an inherited `PSXPORT_CARD` does not override it;
with no card passed the operator's card stays in force and on disk).

**Spyro 1 is deliberately not moved.** `tools/drive.py gameplay` answers the save picker of the existing card and on a blank
one stalls (`never reached the save picker (TSM_Loading) within 12000 frames`, title mode 1 state 1 sub-state 10), and its
route never writes that card (md5 unchanged across 5 runs). Measured: arrival frame 6360 on 5 of 5 runs, card `295df1af`
before and after each. Its start state is still an unpinned input (the operator's card), and it was only vulnerable to the
Spyro 2/3 routes scribbling on it, which they no longer do.

**After (blank card each run): Spyro 2 reaches gameplay at 3340 on 8 of 8 runs and Spyro 3 at 4890 on 8 of 8**, with the
shared card's md5 unchanged across all 16. The numbers recorded in issue 0167 (4740 and 3190) were the second-run values on a
card the first run had written; S024 and S025 now record 3340 and 4890.

## The determinism check

`tools/determinism_check.py --title spyro2|spyro3` runs the title headless three times: two identical legs and a third
with one pad tap at field 300. It refuses unless the first two have the same digest in every field, and unless the third's
guest RAM diverges (and not before field 300); a divergence that is only the pad column echoing the tap is refused as
blind. `--selftest` (ctest `determinism_check_selftest`) needs no disc and carries both answers: identical against
drifting runs, a perturbation the digest sees against one it cannot. Measured on Spyro 2: baseline vs repeat identical
over 1201 fields; baseline vs perturbed differs in `pad` at 300 and in guest RAM at 301.

Its limit, stated plainly: two legs that start from the same card cannot show a card that CHANGES between runs. That
cause is held by `drive_environment_card`; the check catches whatever else makes two same-input runs differ.

## Found on the way

Rebasing the framework pin onto psxport main (Toy Story 2's `c42a9f46`) broke both routes: Spyro 2 faulted at guest pc
`0x8007EF78` ("invalid load/store at address PC 0x10000000", field 3306) and Spyro 3 never left its loading state.
Bisected on psxport: `996a400d` good, `c42a9f46` bad. Cause: `cd_read_stock_sync` stores a sector unnotified and reports
the range once per sector, but reported `{buf, buf + bytes}` for every sector, so translated code in sectors 2..n of a
module load was never invalidated (the per-byte stores it replaced had hidden it). Fixed in the framework with a test
that is red on the old range (psxport `64b35402`).
