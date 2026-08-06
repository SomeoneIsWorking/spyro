---
id: I046
kind: instrument
status: trusted
created: 2026-08-06
---

## Instrument

PSXPORT_DEBUG=scene — one line per DRAWN frame naming the scene the render seam was asked to produce (stage selector [0x800757D8] + the arm's description + which leg is active), plus the abort's backlog dump (SpyroRenderer::reportBacklog) which lists ALL ten FIELD layers with an ARMED / not-armed marker

## Validated by

RUN AGAINST BOTH CLASSES, 2026-08-06, one binary (scratch/bin/spyro_port.GATE). (1) Stage 13 — native leg aborts on the first drawn frame and prints the conditional-arm branch with its live discriminator ([0x80078D78]=0 -> 0x8007CEE4). (2) Stage 0 / FIELD — driven into the field on the reference leg and flipped live with the REPL 'renderpsx off' (scratch/logs/nativerender/repl_flip2.log): it printed the ten-layer backlog with a MIXED answer — 9 ARMED, 0x800190D4 NOT armed — so the gate evaluation is not a rubber stamp and the reporter can print both answers. (3) Denominator: the per-frame line is unconditional on both legs, so 'grep -c [scene]' IS the drawn-frame count (8905 in a 20 s reference boot: 6007 stage 13, 2898 stage 0). KNOWN BLIND SPOTS, stated by the classifier's own comment: it can distinguish the 16 selector values and the three runtime discriminators, it CANNOT name what most stages are (only stage 0 has an RE'd role), and it does not look below the stage — two different levels or menu pages are ONE identity to it.

## Known failure modes

(none recorded yet)
