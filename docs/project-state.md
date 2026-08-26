# Project state

Factual capability coverage for Spyro 1's boot and native title presentation. Atomic work lives in
`docs/issues/`, ownership and placement in `docs/codemap.md`, and the ordered binary-evidence chain
in `docs/re-frontier.md`.

| ID | Capability / observable outcome | State | Dependencies | Goals |
|---|---|---|---|---|
| S001 | The verified Spyro 1 executable boots through the shipping runtime to stage 13's title overlay | verified | — | — |
| S002 | Stage-13 title modes 0 and 1 are presented through game-owned native sprite commands | partial | S001 | — |
| S003 | Stage-13 title mode 2 presents the three-slot save screen natively | missing | S002 | — |

## Current focus

S003 is the current focus: the live native product now advances through mode 1 and stops at the
explicit mode-2 refusal, so the next grounded ownership unit is `0x8007CEE4`'s three-slot save screen.

## Capability details

### S001 — Spyro 1 boot to title

Evidence: the identity-verified `SCUS_942.28` shipping runtime reaches stage 13 and the resident
OV_5B800 title overlay on both reference and native render legs at framework pin `99a42aa3`. The
serialized reference record `scratch/logs/title-menu-reference-99a42aa3.log` contains 35
`[titleoracle] PASS` results, no `DIVERGES`, `REFUSED`, `STUCK`, or `FATAL`, and a clean
3,000-present cap. Supplemental native captures `present_50.ppm`, `present_150.ppm`, and
`present_250.ppm` show a coherent Universal logo, Insomniac mountain scene, and Spyro title scene,
with measured non-black coverage of 4.08%, 93.26%, and 93.33%. Issue 0085's retained exact command,
denominators, and result are the durable record; the gitignored run artifacts are supporting evidence,
not the sole verification basis.

### S002 — Native title modes 0 and 1

The native title owner reads overlay state through `title_menu_state`, builds bounded commands through
`title_menu_recipe`, and submits them through the existing `0x8007CD38` sprite-emitter owner. The
Spyro 1 runtime advertises the native path and temporal interpolation explicitly; the lineage base
advertises neither, so the unavailable Spyro 2/3 runtimes cannot inherit Spyro 1's product facts.
At framework pin `99a42aa3`, the retained-body oracle passed calls 1 through 35 for reached mode-1
substates 0, 15, and 1 with no divergence. The exact native record
`scratch/logs/title-menu-native-99a42aa3.log` then emitted five consecutive substate-0 frames with
`recipe=3 emitted=3`, opened `scratch/saves/card.mcr`, and ended with watchdog signal 06 / `abort`.
The preceding debugger reproduction locates that explicit abort after the transition to mode 2,
rather than in the five mode-1 recipes.

Gap: real-data command equality is demonstrated only for the reached mode-1 substates; the other
hermetically covered switch arms remain live-corpus gaps. Mode 2 is a separate missing capability.
Issue 0085 records the resolved mode-1 unit.

### S003 — Native title mode 2

Missing capability: after the memory-card path opens its card image, title state advances to mode 2.
`titleMenuRender` deliberately returns false because the three-slot save-screen recipe is not owned,
and `SpyroRenderer::drawFrame` aborts through the explicit first-unimplemented boundary. The exact
`99a42aa3` native record ends with signal 06 / `abort`; the debugger-established call path assigns
that abort to the mode-2 refusal. Issue 0086 tracks the atomic RE and implementation work; a guest
fallback or empty-success path is not present.
