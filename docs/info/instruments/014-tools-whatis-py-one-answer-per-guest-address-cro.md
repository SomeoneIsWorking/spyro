---
id: I014
kind: instrument
status: trusted
created: 2026-07-28
---

## Instrument

tools/whatis.py — one answer per guest address, cross-referencing image / resident RAM / recomp set / entry table / static refs / Ghidra / docs

## Validated by

Discriminates across four kinds of address rather than printing something plausible for anything: a MAIN function (0x80053C68 -> in MAIN text, recompiled, 1 direct jal from 0x80012434), an arena address whose resident overlay differs from four of the five images spanning it (0x8007CFB4 -> marks the four in red as 'would mislead' and the one match as resident), an address in no module (0x80500000 -> says so and points at overlay_scan), and a mid-image arena address that is not an entry. It also CAUGHT ITS OWN BUG on first use: it reported 'not recompiled' for a known-seeded address because the regex matched only MAIN's gen_func_<ADDR> naming and not the overlays' ov_<tag>_gen_<ADDR> — found because the answer contradicted something already established, which is the only reason a silent wrong answer surfaces. Fixed and cross-checked against the decl file directly.

## Known failure modes

(none recorded yet)
