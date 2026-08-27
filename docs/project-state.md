# Project state

Factual capability coverage for Spyro 1's boot and native title presentation. Atomic work lives in
`docs/issues/`, ownership and placement in `docs/codemap.md`, and the ordered binary-evidence chain
in `docs/re-frontier.md`.

| ID | Capability / observable outcome | State | Dependencies | Goals |
|---|---|---|---|---|
| S001 | The verified Spyro 1 executable boots through the shipping runtime to stage 13's title overlay | verified | — | — |
| S002 | Stage-13 title modes 0 and 1 are presented through game-owned native sprite commands | partial | S001 | — |
| S003 | Stage-13 title mode 2 presents the three-slot save screen natively | verified | S002 | — |
| S004 | Spyro 1 boot and gameplay advance under a title-owned frame/field scheduler without guest VSync | verified | S001 | — |
| S005 | Spyro 1 exposes its native renderer, wider-FOV aspect modes, and temporal interpolation through title-owned capability policy | partial | S002, S004 | — |

## Current focus

S005 is the current focus: title modes 0 through 2 are native, wide, and frame-owned. The stage 14 /
`GS_Cutscene` recipe named by the first New Game transition now composes the owned actor, world, and
cyclorama producers plus its measured clear-colour, culling-distance, and fade responsibilities.
An isolated real-disc run now verifies that route at 16:9 under the same host-owned frame loop;
issue 0088 is resolved. Native FIELD and the remaining scene arms keep S005 partial.

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
hermetically covered switch arms remain live-corpus gaps. Issue 0085 records the resolved mode-1
unit; mode 2 is independently verified under S003.

### S003 — Native title mode 2

Verified on the real `SCUS_942.28` product. The binary-derived mode-2 recipe owns the two borders,
three empty/occupied slot summaries, dragon-count digits, selection/overwrite states, and slide-out
pair through the same state lens and `0x8007CD38` sprite submitter as mode 1. Its retained-body
command-stream oracle now selects the same recipe for mode 2; 26 hermetic recipe cases include the
18-command capacity boundary and exact/mutated/truncated stream controls.

The first live run exposed a false shared-state assumption: `[0x80078D7C] == 2` means a paired actor
only in title mode 3, but also names save-picker state 2. Qualifying the paired scene by mode 3 fixed
that abort. The next run exposed a real frame-loop contract hole: a guest render-suppressed update
skipped the presentation fence. `FieldScheduler` now presents the previous picture for one
host-owned field on that path, so suppression cannot silently become a zero-fence product step.

At framework `3a8256e9`, `scratch/logs/spyro-mode2-native-wide-lerp-3a8256e9.log` enables the native
renderer, interpolation, and explicit 16:9 settings, announces `aspect=1`, `wide_engine=1`, and
`512 -> 684`, reaches mode-2 state 4 for 174 draws, and emits all eight commands on each recorded
frame. It exits cleanly at the 900-field cap with the frame-loop contract satisfied and no guest
VSync, native-render refusal, or recomp miss. Present 380 is a real 960x720, 69.72%-non-black image;
visual inspection shows all three EMPTY slots, New Game/Load Game choices, card footer, live Spyro,
and the widened mountain backdrop. C227 and issue 0086 record the resolved boundary. The diagnostic
reference leg remains intentionally fail-fast at its guest-VSync tail, so live generated-body
comparison is not claimed for mode 2.

### S004 — Native frame and field ownership

`Spyro1Runtime` now creates one title-local `Spyro1FrameDriver`; `dc_boot_init` returns and the
framework shell calls one finite `stepFrame`. The driver owns the measured input-latch/update/frame-
step/render order, while `FieldScheduler` owns the 60 Hz counter, pad service, guest callback root,
audio, BIOS events, present/pace, host-turn acknowledgement, and producer boundary. Boot
`0x800127C0`/`0x8001286C` is transcribed as a resumable sequence of its four eight-field fades, two
210-field holds, CD/state pump, and final display initialization. The adapter config declares
libetc VSync `0x8005DBC4` as the mandatory fatal trap; helper `0x8005DD0C` has no success override.
The generated boot bodies remain intact as A/B references.

The real `SCUS_942.28` product path now supplies the runtime proof. Clang build
`scratch/build/agent-spyro` passed 34/34 CTests, then
`scratch/logs/agent-spyro-wide-capture-600.stdout.log` exited 0 at an 800-field cap after the 436-field
boot and 182 native logic frames. The framework reported its frame-loop contract satisfied, all
three stage-13 native producer rows were exercised, and neither the guest-VSync trap nor the
one-presentation-fence assertion fired. Issue 0087 and C225 hold the exact causal sequence and
falsifier. The retained reference renderer `0x8001ED5C` still contains guest VSync and deliberately
fails fast until its diagnostic display tail is split; that separate diagnostic gap does not regain
product timing ownership.

The forced-input mode-2 path separately reached a guest render-suppressed update. The old cadence
predicate accepted that iteration without a field, contradicting the framework's exact one-fence
contract. The scheduler now delivers one visible host field with the previous picture; the ensuing
900-field native/wide/interpolated run reached 415 logic frames and satisfied the same contract.

### S005 — Native, widescreen, and temporal product exposure

`Spyro1Runtime::renderCapabilities` returns `RenderCapabilities::interpolatedNative()`, making the
native producer path the title default and exposing the shared 60fps interpolation row. The same
runtime creates the `Fps60` temporal presenter. Aspect selection remains available through the
shared player UI; Spyro 1 registers its measured `wide_clip` owners and its direct stage-13 actor,
world, and cyclorama producers derive clip/projection/draw extents from the live wide width. The
lineage base explicitly declares GTE/no-native/no-temporal, so the non-runnable Spyro 2/3 substrates
cannot inherit Spyro 1's capability claims merely because they share the engine repository.

State remains partial because native FIELD and the other scene arms remain unowned; temporal
eligibility is limited to the compatible paired-actor path. The post-migration live configuration is
verified: the 800-field product run enabled the temporal presenter and announced
`aspect=1`, `wide_engine=1`, `native_width=512`, `render_width=684`. Its present-600 capture is a real
960x720 stage-13 picture with 69.7% non-black pixels and 3,022 colors. This proves exposed 16:9
projection. C226 adds the temporal runtime proof through the corrected host-owned scheduler: a
bounded no-input 4,000-field run reached 142/142 compatible intervals and the shipping presenter
executed 141 strict-interior plus 141 endpoint callbacks, emitting 282/282 with no empty output and
no presentation-fence violation. This verifies the reached paired-actor interpolation path, not the
unowned scenes or producer variants that keep this state partial.

The first real-disc New Game transition advanced mode 2 through the save-slot flow, entered mode 3,
and changed the stage selector from 13 to 14. Native rendering then refused at `GS_Cutscene` before
retained arm `0x8001E9C8`, whose reached layer includes `RenderWorldChunks` `0x800258F0`; the guest
VSync trap did not fire first. This is diagnostic frontier evidence only because another title
briefly overlapped the run. Static RE of retained body `0x8001E9C8` showed that the complete scene
reuses the owned actor, world, and cyclorama producers in their authored order, with cutscene-local
clear colour, `0x14000` world distance, and conditional fade producer `0x800190D4`. Those
responsibilities now have separate shipping modules and focused Clang tests.

That isolated check now exists at framework `3c342ec3`. The real `SCUS_942.28` New Game route
reached selector 14, resolved `aspect=1` to `512 -> 684`, and exited 0 after a clean REPL `end`.
The framework reconciled 1,962 logic frames with zero dropped layers, reported its frame-loop
contract satisfied, and re-earned the actor/world/cyclorama rows plus first-earned native fade
`0x800190D4` on seven frames; no guest-VSync violation appeared. Visual inspection of the 684x240
capture and six consecutive presents shows coherent animated cutscene geometry across the widened
scene, without black side bars, corruption, or a missing layer. C228 and resolved issue 0088 retain
the exact falsifier and evidence. This verifies the reached stage-14 cutscene, not the remaining
unowned native scenes that keep S005 partial.
