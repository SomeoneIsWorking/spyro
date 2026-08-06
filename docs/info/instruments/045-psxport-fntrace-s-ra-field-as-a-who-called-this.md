---
id: I045
kind: instrument
status: trusted
created: 2026-08-06
---

## Instrument

PSXPORT_FNTRACE's ra FIELD as a 'who called this' discriminator — the cheapest way to prove WHICH code owns a call site, guest or port. fntrace records r[31] at the first call of each traced address; a guest caller leaves its own return address (Spyro's main leaves 0x80012238 for the update 0x8003385C and 0x80012284 for the render 0x8001ED5C) while a port caller leaves the top-level sentinel DEAD0000 that load_exe wrote. One env var, no rebuild. USE: PSXPORT_FNTRACE=0x<addr>[,...]; read the '[fntrace] 0x... REACHED — first call at frame N from ra=XXXXXXXX' line and the exit summary, which prints 'NEVER CALLED' explicitly so a zero is a real answer and not an empty log. BLIND SPOTS: fntrace CLAIMS the override slot, so tracing an address whose override does real work REPLACES it for the run; recursion is not counted (the hook uninstalls itself around the body); MAIN-module entries only, no overlay addresses; ra is captured on the FIRST call only, so a site with two callers reports just one of them.

## Validated by

RUN AGAINST BOTH CLASSES ON ONE BINARY, 2026-08-06: with PSXPORT_SPYRO_FRAME_LOOP unset it printed ra=80012238 / 80012284 (guest-driven); with it =1 it printed ra=DEAD0000 for the same two addresses (port-driven), same first frame 436 and matching call counts. scratch/logs/frameown/A_off.log vs scratch/logs/frameown/B_on_psx.log.

## Known failure modes

(none recorded yet)
