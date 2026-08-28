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
| S006 | Spyro 2 has an identity-derived resident substrate and a title-local native boot owner through the pre-display boundary | partial | — | — |
| S007 | Spyro 1 accepts held digital input and moves the player after the New Game field handoff | verified | S004 | — |

## Current focus

S007 is verified: held digital input reaches the source-backed movement target and moves Spyro after
the field handoff, and the retained guest update also responds to jump, charge, and flame input. The
next focus is the actor/effect coverage that makes those actions visible. The controlled native route
now renders the visible three-layer Spyro model through FIELD's `0x80023AC4` owner and continues
through the wired stage-0 producers, including the visible near portal and type-2 particle family,
for 1,821 reconciled logic frames with zero dropped layers. Visual parity, independent oracle
comparison, and broader actor-producer coverage remain open; portal traversal stays outside this
control milestone.

The fresh current-build idle-vs-Left replay pair also exited 0 at 4,255 presents with no native-render
refusal or fatal. At replay frame 3000, idle was `(0x14C00,0x0B800,0x023E0)` while Left was
`(0x1497B,0x0B9CE,0x02514)` and carried a nonzero movement target. The live player-shadow gate
`0x8007AA10+0x24` was zero in both captures; the missing shadow is therefore an uncalled renderer
boundary, not a disabled gameplay state.

The retained shadow boundary is now measured without changing the native picture: on the first
FIELD frame after the gate route, `0x80059F8C` produced zero Moby-shadow packets while `0x80059A48`
produced 16 Spyro-shadow packets over `0x80187BB0..0x80187E30`. The packets retain the source's
0x28-byte layout, `E1000640` draw-mode setup, and flat Gouraud colour `0x32608080`. The diagnostic
body runs from a full RAM/scratchpad/GTE/CPU snapshot and restores it before native presentation,
so this is a producer measurement rather than a shipping fallback. The next faithful actor owner is
therefore Spyro-shadow geometry and queue submission; no guessed ellipse is justified.

S005 remains partial: title modes 0 through 2 are native, wide, and frame-owned. The stage 14 /
`GS_Cutscene` recipe named by the first New Game transition now composes the owned actor, world, and
cyclorama producers plus its measured clear-colour, culling-distance, and fade responsibilities.
The stage-14 owner was observed presenting at 16:9 under the same host-owned frame loop, resolving
its missing-scene refusal, but C228 is falsified as proof of the complete New Game transition because
that run was manually ended before handoff to gameplay. The false recompiler entry that later crashed
the transition has now been removed, and the product reaches the exact stage-0 native-render seam.
FIELD now has a wired stage-0 producer sequence for the reached Artisans frame: collectables (including
the completed-gem text branch), regular actors, the visible normal Spyro model arm, the composed
secondary/shaded actor pass, environment, cyclorama, type-0/type-2 particles, fade, border, and
tracers. The Moby/Spyro shadow packets, flame/glow/sparkle effect arms, other scene arms, and live
producer variants remain unowned, so the complete game remains partial. The actor composition's first
live route ran 3,700 presented fields with 1,910 reconciled logic frames and no render refusal; that
route had a valid-empty secondary list and emitted roughly 110–120 shaded faces per FIELD frame.
The environment layer's `active_animation` refusal is resolved: RenderWorldChunks' phase-1 per-sector
animation is owned natively (`game/render/world_animation.cpp`) and proven byte-exact against the
retained body on the exact frame that was refusing (C229, instrument I057, issue 0089). The
post-framework replay `scratch/logs/spyro-replay-post-framework-field-20260828.log` then ran through
the recorded user input and 10,000 presented fields with rc=0, 5,057 reconciled frames, zero dropped layers, and no native
render refusal. That is a route-continuity result, not visual or full-oracle parity. The animation's
BLENDED (GTE-interpolated) form is covered hermetically only; no live frame has exercised it yet.
The current audio-field trace also ran 1,200 NTSC fields with 882,882 expected and queued samples,
every field rendering 735 or 736 samples into a valid 44.1 kHz stereo WAV. This proves the current
cadence and non-silent sink path, not PCM equality against the reference. The field scheduler also
skips the level-transition tally on a fresh Start edge by advancing
`g_LevelTransTicks` at `0x800756AC` to the guest's exact hidden boundary `417`; the CD load and guest
transition state remain active, and the portal traversal is intentionally not bypassed.

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

State remains partial because other scene arms and live producer variants remain unowned; temporal
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

At framework `3c342ec3`, an isolated `SCUS_942.28` run reached selector 14, resolved `aspect=1` to
`512 -> 684`, exercised the actor/world/cyclorama/fade composition, and produced visually coherent
early cutscene frames. The operator ended it through the REPL before transition completion. C228 is
therefore falsified as whole-route evidence and cannot prove gameplay after the card; it remains only
an observation of the reached early stage-14 picture. A later exact run exposed the collision crash
recorded in issue 0089; after its recompiler root fix, the route reaches stage 0 and truthfully refuses
the previously incomplete FIELD scene. The latest source-owned field unit is the separate `0x80022A2C`
world/shaded queue: its stage-0 snapshot preserves 93 total records, 52 valid meshes / 936 source
primitives, and 3 visible records / 54 candidates, with both observed mesh and lighting-table classes;
the recipe resolves 23 Gouraud faces and is now called by the stage-0 seam. The regular actor owner is
also Ready on that snapshot after issue 0094's Plain descriptor-pair correction: 175 Mobys scanned,
14 records, 423 candidates, 211 rejects, and 212 faces. Its following secondary actor owner is Ready
with 3 visited list members / 1 record, 138 candidates, 63 rejects, and 75 faces. The next authored
actor-pass gap is Moby shadows: the regular native builder now stages the source-backed list entries
from fixed start `0x800724F4` and commits the shared cursor at `0x80075F00` after actor admission,
but the native actor builders/renderers do not yet own the complete shadow result. The retained Moby
shadow consumer `0x80059F8C`, Spyro shadow `0x80059A48`, and flame/glow/sparkle effects remain
unowned.
The separate `0x8002B9CC`
environment/world owner is also compiled but unwired: on the same snapshot it derives selection 17,
distance `0x28000`, 86 sectors (20 low / 29 high), 1,376 candidates, 1,039 rejected, and 413 final
faces without mutating the culling word or any of the 7,168 edge-work bytes during preparation. Its
corrected medium-quad texture rule still needs issue 0077's retained-world oracle. The compiled
`0x80050BD0` cyclorama owner now covers the exact main-sky class with inactive or projected-empty
portals and atomically reuses owned `0x8004EBA8`. The Artisans snapshot has five logically active
records but every projected aperture has zero screen-crossing edges, so all five are valid-empty and
the cyclorama recipe is Ready for that frame. Gate-0 teleport reaches a visible aperture whose
near-family recipe produces 94 clipped triangles. The source-backed `0x8004FEA0` mask now emits its
two clipped full-screen triangles through a dedicated painter object; a controlled Left route runs
through the visible-portal path, and the gate-teleport route advances to the non-type-0 particle
refusal. The production-compiled `0x80050240` recipe and family submitter remain ready for a future
mid-distance portal frame. The current replay reaches this complete
stage-0 composition without a native-render refusal;
the acceptance boundary is now faithful visual/oracle comparison plus the remaining unowned scene
variants. A normal paced audio run after the shared CDC filter fix (`scratch/logs/spyro-xa-after-filter-20260828.log`)
produces 20.02 seconds of non-silent stereo 44.1 kHz WAV for 1,200 VBlanks, with 239 selected XA
sectors on file 1/channel 4 and zero ring-full reports; the prior back-pressure came from decoding
interleaved unselected channels. The same run reports 60.0 paced VBlanks/s and 735/736 SPU frames per
field. SBS oracle boot remains limited evidence:
the 120-field run exits cleanly but retains five stack-only differing bytes and leaves one owned
address unreached; its shared WAV sink writes zero bytes and is not audio evidence. A current paced
native run spans about 20.115 seconds for 1,200 fields (~59.66 Hz), confirms 882,882 output samples,
and reports zero ring pressure. These runs prove product timing/routing, not speaker delivery or
independent oracle PCM parity.

### S006 — Spyro 2 resident boot substrate

The exact manifest-matched `SCUS_944.25` executable emits 683 resident functions in nine generated
translation units with no foreign-title or overlay seeds. A dedicated `spyro2_port` links that
substrate separately from Spyro 1, selects only the Spyro 2 executable identity, and installs a
direct `Spyro2Runtime` with no legacy config or hooks. The title-owned finite boot driver reproduces
the binary's persistent game-main and boot-prefix stack frames, calls constructors `0x80054834`
and first leaf `0x800548A4`, then enters a title-owned display-bootstrap state machine instead of
dispatching either the non-returning guest main or the retained bootstrap. That owner preserves the
exact nested stack/register state and non-timing effects around three measured field waits: two
direct calls from `0x80011BBC` and a third inside clear helper `0x8004C484`. DrawSync `0x800557E4`
and GPU timeout arm/check `0x80057880`/`0x800578B4` are title-owned synchronous overrides with their
generated bodies retained as A/B supers; none spends a display field.
The Clang product and focused runtime, launcher, structure, provisioning, and executable-help gates
pass. An isolated real-executable run passed shared boot initialization, audited crt0 10/10, and
reached the title-owned `0x80011BBC` boundary with fatal VSync registration intact.

Live PID `3564943` presented all three finite fields, each captured and visually verified as the
expected uniform black clear picture. It completed display bootstrap, selected NTSC 59.940 Hz,
returned to `0x80011EB4`, and deliberately stopped at later binary-owned boot-prefix leaf
`0x80011B1C`; no guest VSync violation occurred.

Gap: issue 0092 owns the post-display initialization and loader chain. The later loader reaches
`0x80077374` outside resident executable text and needs source/base/payload evidence before dispatch.
First gameplay frame, native renderer, widescreen, and temporal interpolation are not yet owned or
verified.
