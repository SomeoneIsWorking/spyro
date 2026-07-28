---
id: I001
kind: instrument
status: trusted
created: 2026-07-28
---

## Instrument

out-of-text jal/j scan over the resident image (ad-hoc, this session)

## Validated by

NOT VALIDATED — failed its own sanity check. Output is dominated by false positives: targets scattered across the whole 32-bit space (0x88888888, 0x8C040404, 0x83B7B7B4) originating mostly from 0x8006C000+, i.e. the .data tail of the text image decoded as instructions. It cannot separate a real overlay call from misdecoded data, so neither a null nor a positive result from it means anything. DO NOT CITE IT for the overlay question.

## Known failure modes

(none recorded yet)
