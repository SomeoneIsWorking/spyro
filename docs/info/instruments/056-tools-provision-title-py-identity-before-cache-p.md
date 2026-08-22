---
id: I056
kind: instrument
status: trusted
created: 2026-08-22
---

## Instrument

tools/provision_title.py identity-before-cache publisher

## Validated by

tests/test_provision_titles.py preloads cached title data, proves requested SCUS_944.25 publishes without touching Spyro 1, proves selected SCUS_942.28 is refused for Spyro 2 without cache replacement, and proves identity disagreement publishes neither executable nor SYSTEM.CNF. A real Spyro 1 CHD then staged and matched SCUS_942.28 before publication.

## Known failure modes

(none recorded yet)
