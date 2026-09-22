# Project state

Factual capability coverage for the Spyro trilogy's native/Lightrec execution and presentation.
Goal G001 is three products, so Spyro 2 and Spyro 3 carry their own rows here even though neither is
being implemented yet; a capability nobody has started is `missing`, not absent from the inventory.
Atomic work lives in
`docs/issues/`, ownership and placement in `docs/codemap.md`, and the ordered binary-evidence chain
in `docs/re-frontier.md`.

## Comparison baseline

The comparison baseline is the original USA PSX releases as run in a faithful emulator. The
repository also has a retired intermediate baseline: native-enhanced executables whose remaining
guest code was emitted offline as C. Evidence from that path remains valid only for the exact binary
behavior or native owner it observed; it does not prove that the native/Lightrec product exists.

| ID | Capability / observable outcome | State | Dependencies | Goals |
|---|---|---|---|---|
| S001 | The verified Spyro 1 executable reaches stage 13's title overlay under the recorded native owners | partial | — | G001 |
| S002 | Stage-13 title modes 0 and 1 are presented through game-owned native sprite commands | partial | S001 | G003 |
| S003 | Stage-13 title mode 2 presents the three-slot save screen natively | verified | S002 | G003 |
| S004 | Spyro 1 boot and gameplay advance under a title-owned frame/field scheduler without guest VSync | partial | S001 | G002, G003 |
| S005 | Spyro 1 exposes native rendering and presentation settings through title-owned capability policy | partial | S002, S004 | G003 |
| S006 | Spyro 2 has identity-derived executable facts and a title-local native boot owner through the pre-display boundary | partial | — | G001 |
| S007 | Spyro 1 accepts held digital input and moves the player after the New Game field handoff | partial | S004 | G001, G003 |
| S008 | psxport executes remaining Spyro guest code through a per-Core Lightrec runtime with bounded, accounted fallback | partial | — | G002, G004 |
| S009 | Spyro 1 reaches both stage-13 800/900 discriminators through Lightrec and native frame ownership | verified | S004, S008 | G001, G002 |
| S010 | The generated world-body include is replaced by resumable runtime guest execution | partial | S008, S009 | G002 |
| S011 | Representative Spyro 1 gameplay conforms on each released host through native/Lightrec execution | missing | S005, S007, S010 | G001, G002, G003, G004 |
| S012 | The frozen launcher builds and runs the native/Lightrec product without offline guest translation | partial | S008, S011 | G004 |
| S013 | Windows CI produces an installable release with matching-host runtime checks | missing | S008, S018 | G004 |
| S014 | macOS CI produces a runnable `.app` release with matching-architecture runtime checks | missing | S008, S018 | G004 |
| S015 | Linux CI produces a runnable AppImage release with package-install and runtime checks | missing | S008, S018 | G004 |
| S016 | Android CI produces an APK with ARM64 dynarec execution and authored touch controls | missing | S008, S018 | G004 |
| S017 | A WASM gameplay build is released through CI and deployed on GitHub Pages | missing | S008, S018 | G004 |
| S018 | Packaged first launch selects, validates and persists user-supplied game files without a terminal | missing | S001 | G004 |
| S019 | Widescreen renders additional horizontal scene coverage without stretching the original image | partial | S005 | G003 |
| S020 | 60fps presentation reconstructs motion between game updates from captured source geometry | partial | S004, S005 | G003 |
| S021 | Touch-enabled releases provide an authored SVG control interface | missing | S018 | G004 |
| S022 | Spyro 1 streams its XA music through the shared CD/XA owner | partial | S008 | G002 |
| S023 | Spyro 3 has identity-derived executable facts and a title-local native boot owner through the pre-display boundary | missing | — | G001 |
| S024 | Spyro 2 reaches representative gameplay through native/Lightrec execution | missing | S006, S008 | G001, G002 |
| S025 | Spyro 3 reaches representative gameplay through native/Lightrec execution | missing | S008, S023 | G001, G002 |
| S026 | Spyro 2 widescreen renders additional horizontal scene coverage without stretching | missing | S024 | G003 |
| S027 | Spyro 3 widescreen renders additional horizontal scene coverage without stretching | missing | S025 | G003 |
| S028 | Spyro 2 presents interpolated 60fps from captured source geometry | missing | S024 | G003 |
| S029 | Spyro 3 presents interpolated 60fps from captured source geometry | missing | S025 | G003 |

## Current focus

S011 — the Artisans route matches the full-console reference on every decisive range at every one
of its 477 game frames (`tools/oracle_compare.py --frame-step 1`, 485 checkpoints, 6,305 decisive
range comparisons, zero divergences; issue 0110). The world, the player model, the regular and
secondary actor layers, the world-shaded sprite queue and the terrain producer `0x8004EBA8` are all
reconstructed in an in-between present, which brings the reconstructed share of captured items to
0.955. That cost has been re-measured on gameplay, and the earlier figure was an under-estimate because it
was taken on the save-file dialog (issue 0116). On a local Linux x86-64 Clang build, offscreen and
unpaced, driven to Artisans through `tools/drive.py gameplay` and walked left with
`PSXPORT_DEBUG=perf`, the profiler's rolling 60-step averages separate the two screens plainly:

| leg | on the menu | in Artisans |
|---|---|---|
| 4:3 | 1.3-1.4 ms | 4.6-5.5 ms |
| 16:9 | 1.3-1.6 ms | 5.1-6.5 ms |
| interpolated 60fps | 1.5-1.8 ms | 9.8-10.2 ms |

Against the 33.37 ms a two-field product step has, interpolated 60fps gameplay leaves about 3.3x
headroom on this host, not the 4.6x the dialog measurement suggested. The port stays present-bound:
in the 60fps gameplay window the present CPU holds 9.13 ms of a 10.23 ms step while the guest update
holds 1.10 ms. Gameplay percentiles are not quoted because the profiler's p50/p95/p99 distribution is
cumulative over the whole run, so it cannot be separated from the ~2,900 boot and menu steps that
precede the ~300 gameplay ones; the rolling windows are what distinguishes them. Exactly one frame
per run still misses, by a wide margin: about 3.0 s (3,101 ms, 3,135 ms and 3,189 ms in the three
legs above), which tripped the default 3 s frame watchdog and made every unattended long run abort
unless `PSXPORT_WATCHDOG` was raised (issue 0115). **That is fixed at the cause (psxport
`892e9550`, 2026-09-19).** It was read as a cold CHD hunk cache and it was not; it was also not a
missing Setloc. libcd's `CdControl` sends the Setloc itself for a position-carrying command, and the
framework's `CdControl` override read only the command byte and dropped the position, so the XA
cursor stayed at LBA 0 while Spyro had asked for LBA 113,448 and scanned until it hit unrelated
audio. The stream now starts where the guest asked: skipped sectors 9 -> **0**, disc hunk fills
8,461 -> **841**, time in `chd_read` 3,611.9 ms -> **403.2 ms**, worst single fill 21.2 ms -> **0.9
ms**. A 1,300-frame `looks_right.py` run that previously died with `watchdog STUCK` now completes
and passes all four checks. Oracle parity is unchanged with widescreen and fps60 both live, and as
of 2026-09-19 that is a two-sided measurement rather than an assertion: `tools/oracle_compare.py`
run against `aspect=0 fps60=0` and against `aspect=3 fps60=1`, each configuration confirmed in the
product's own log, produces **byte-identical guest state at all 14 checkpoints**, arrival frame
counts included. The enhancements do not perturb the simulation. Until that day the comparison could
not have shown otherwise: `PSXPORT_SETTINGS` went unset, so the product discovered whichever
untracked `psxport_settings.ini` sat in the working directory, and the report recorded only CLI
overrides. Both halves are fixed -- `drive.environment` now supplies the tracked
`tools/shipping_settings.ini` to every consumer including the oracle, and psxport `5ace1a40` records
the effective configuration in every report. No released host is qualified by this: a
maintainer build on one desktop is not the AppImage, the APK or the browser package. Shadows, glow,
sparkles, particles and tracers are NOT next:
measured together they draw about 32 faces per game update, and what remains replayed verbatim is
82% the unattributed 2D and HUD layer. Widening the route past Artisans is blocked on issue 0114,
because the pacing residual steers a camera-relative walk. Boot/title, a visible player, and one matched route do not establish full
conformance.

## Hosted verification and host gaps

The repository's hosted CI is intentionally asset-free: one Linux x86_64 source-policy job runs the
canonical Python verifier's negative selftests and live scan. It downloads no disc, executable,
BIOS, generated guest body, or prebuilt runtime, and claims no gameplay evidence. The local native
Clang/Ninja gate now passes against the frozen PSXport/Lightrec pin and its synthetic framework
contract. macOS arm64, Windows x86_64, and Android arm64 runtime jobs are partial/missing until the
shared runtime and platform packaging owners provide real target builds and execution evidence.

## Preserved historical evidence

Everything in this section describes the retired product and retained semantic recipes. It is not
evidence that the current JIT product can boot, present, accept input, or render. Runtime oracle,
frame, service, and generated-dispatch integrations named below have been deleted; only the durable
binary facts and independently testable native semantics remain evidence.

The retired generated-code product established that held digital input reaches the source-backed
movement target and moves Spyro after the field handoff; jump, charge, and flame also reach the guest
update. Its controlled native route rendered the visible three-layer Spyro model through FIELD's
`0x80023AC4` owner and continued through the wired stage-0 producers, including the visible near
portal and type-2 particle family, for 1,821 reconciled logic frames with zero dropped layers. Visual
parity, independent oracle comparison, broader actor coverage, and Lightrec execution remain open.

The measured HookEntryInt continuation at `0x8005DFC8` now crosses the scheduler's pending-VBlank
boundary exactly once instead of advancing its root counter twice. The formerly failing 4,255-frame
route therefore reaches a later, intentional refusal at frame 3,652: stage selector 2 / `GS_PauseMenu`
has no native scene owner (retained renderer `0x8001A40C`). That stage is the next render frontier;
the product does not substitute a plausible guest or generic pause picture.

The fresh current-build idle-vs-Left replay pair also exited 0 at 4,255 presents with no native-render
refusal or fatal. At replay frame 3000, idle was `(0x14C00,0x0B800,0x023E0)` while Left was
`(0x1497B,0x0B9CE,0x02514)` and carried a nonzero movement target. The live player-shadow gate
`0x8007AA10+0x24` was zero in both captures; the missing shadow is therefore an uncalled renderer
boundary, not a disabled gameplay state.

The retained shadow boundary is now measured without changing the native picture: on the first
FIELD frame after the gate route, `0x80059F8C` produced zero Moby-shadow packets while `0x80059A48`
produced 16 Spyro-shadow packets over `0x80187BB0..0x80187E30`. All accepted packets retain the
source's 0x28-byte layout, `E1000640` draw-mode setup, flat Gouraud colour `0x32608080`, a shared
projected anchor, and a closed projected-point fan. The capture also recovered the source `SZ` values
and exact OT buckets: anchor `SZ=0x065E`, bias `3`, and packet buckets
`10,10,10,9,9,8,8,6,6,7,8,9,9,10,10,10`, all admitted. The diagnostic body runs from a full
RAM/scratchpad/GTE/CPU snapshot and restores it before native presentation, so this is a producer
measurement rather than a shipping fallback. The captured links resolve to complete guest replay
chains, and an eight-capture controlled route produced 128 packets with no cycle or missing head;
the anchor depth changed from `0x065E` to `0x081A` as gameplay advanced. The source-grounded native
owner now derives the live 16-point fan, exact GTE projection/depth inputs, OT buckets, and linked
painter order in `game/render/field_shadow_recipe.*` / `field_shadow_submitter.*`, and FIELD calls
it after the player model. The first native face matches the retained source capture exactly
(anchor `00A70064/065E`, points `009A0064/0742` and `009E0055/0713`, bucket 10); the recipe and
focused painter-order tests pass. This is geometry/queue evidence, not complete visual or full
packet-byte parity. Sparkle effects remain unowned, and portal
traversal remains outside this control milestone.

S005 remains partial: title modes 0 through 2 are native, wide, and frame-owned. The stage 14 /
`GS_Cutscene` recipe named by the first New Game transition now composes the owned actor, world, and
cyclorama producers plus its measured clear-colour, culling-distance, and fade responsibilities.
The stage-14 owner was observed presenting at 16:9 under the same host-owned frame loop, resolving
its missing-scene refusal, but C228 is falsified as proof of the complete New Game transition because
that run was manually ended before handoff to gameplay. The false guest entry that later crashed
the transition has now been removed, and the product reaches the exact stage-0 native-render seam.
FIELD now has a wired stage-0 producer sequence for the reached Artisans frame: collectables (including
the completed-gem text branch), regular actors, the visible normal Spyro model arm, the composed
secondary/shaded actor pass, the source-grounded Spyro shadow fan, environment, cyclorama, type-0/type-2
particles, fade, border, and tracers, plus the glow and sparkle effects. Other scene arms and live
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
The current audio-field trace ran 1,200 NTSC fields with 882,882 expected and queued samples, every
field rendering 735 or 736 samples into a valid 44.1 kHz stereo WAV. SBS now compares exact per-field
PCM reports after rebinding each core's isolated SPU output state: a 120-frame oracle run produced
240 reports with no audio mismatch. This is audio-field parity only; the run still has known
non-audio boot/state divergence, and complete visual/oracle parity remains open. The former title-card
and level-transition tally shortcuts wrote guest timer/state values directly. They are removed, and
`titles/spyro1/core/spyro1_transition_skip.*` now owns cancellation properly: a Start or Cross press
performs exactly the terminal transition the screen's own guest owner performs and nothing else.
Two screens are covered. The level-transition tally (stage 1) clears `g_LevelTransHudActive`, which
is the whole of `func_8002DA74`'s ending. The return-home glide (stage 10, `func_8002E084`)
DISPATCHES the guest's own `0x8002C664` — the same call the sequence makes on its second counter
wrap — rather than transcribing its ten globals, so there is no hand-written second copy to drift.
Six focused tests cover the classification, including that the glide is not gated on the tally flag.
Neither cancellation has yet been observed live: reaching stage 10 needs a portal entry from Artisans
followed by pause-menu Quit. `tools/drive.py gameplay --gate-teleport 0:0 --seek-portal` now reads the
six `g_Portals` records, teleports onto a gate's own path node through the port's gate diagnostic and
walks the rest, hopping and detouring when steering alone stalls. That route now CROSSES the portal:
the cyclorama refusals of issue 0106 are gone, and the CdControlF mis-binding behind them is fixed
(see below). Stages 1 and 9 now have their native producer — `game/render/level_transition_scene`
over `level_transition_tally_recipe` and the HUD text builder, closing issue 0107 — so the route
renders the transition and the entrance animation and reaches gameplay in the destination level. The
field's Spyro shadow producer `0x80059A48` now accepts the level-entry frames whose anchor projection
saturates. Retail reads SXY2/SZ3 and MAC1-3 without testing the GTE `FLAG` register, so those flags
are diagnostic output rather than a refusal condition. A real portal route after the fix rendered
300 post-entry fields with 16 shadow faces per field, no native-render refusal, and zero Lightrec
fallback. The route therefore advances beyond the former shadow boundary; the next unqualified
scene or cancellation path must be recorded from a fresh observation.

The level entrance sweep (stage 9, `func_8002E000`) is deliberately still absent. Its exit is inside
its own update — `g_Gamestate = GS_Playing` once `g_Camera.m_Rotation.y` has swept below `-0x200`,
or immediately when the level uses the `D_8006CA84` entrance preset — so there is no recovered
terminal function to dispatch. Writing the gamestate directly would leave the camera mid-sweep, and
writing the preset pointer would install High Caves' entrance on every level. A cancellation here
needs the camera's settled gameplay pose recovered first; approximating it is exactly the shortcut
that was removed.

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

Gap: the historical retained-renderer comparison above has not been repeated against the current
independent full-console oracle. Current native/Lightrec title and gameplay observations are recorded
in S009 and S011; complete boot/state parity remains unqualified.

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

Evidence: verified on the real `SCUS_942.28` product. The binary-derived mode-2 recipe owns the two borders,
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
VSync, native-render refusal, or guest-execution refusal. Present 380 is a real 960x720, 69.72%-non-black image;
visual inspection shows all three EMPTY slots, New Game/Load Game choices, card footer, live Spyro,
and the widened mountain backdrop. C227 and issue 0086 record the resolved boundary. The diagnostic
reference leg remains intentionally fail-fast at its guest-VSync tail, so live retired-body
comparison is not claimed for mode 2. Future comparison uses an independent emulator or separately
built test oracle.

### S004 — Native frame and field ownership

Evidence: `Spyro1Runtime` creates one title-local `Spyro1FrameDriver`; `dc_boot_init` returns and the
framework shell calls one finite `stepFrame`. The driver owns the measured input-latch/update/frame-
step/render order, while `FieldScheduler` owns the 60 Hz counter, pad service, guest callback root,
audio, BIOS events, present/pace, host-turn acknowledgement, and producer boundary. Boot
`0x800127C0`/`0x8001286C` is recovered as a resumable sequence of its four eight-field fades, two
210-field holds, CD/state pump, and final display initialization. The adapter config declares
libetc VSync `0x8005DBC4` as the mandatory fatal trap; helper `0x8005DD0C` has no success override.
The retired product kept emitted boot bodies as A/B references. The native/Lightrec product must use
an independent emulator or separately built test oracle instead.

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

Gap: independent oracle timing/device comparison and representative interactive gameplay cadence
remain unqualified beyond these bounded native frame observations.

### S005 — Native rendering and presentation settings

`Spyro1Runtime::renderCapabilities` returns `RenderCapabilities::interpolatedNative()`, making the
native producer path the title default and exposing the shared 60fps interpolation row. The same
runtime creates the `Fps60` temporal presenter. Aspect selection remains available through the
shared player UI; Spyro 1 registers its measured `wide_clip` owners and its direct stage-13 actor,
world, and cyclorama producers derive clip/projection/draw extents from the live wide width. The
lineage base explicitly declares GTE/no-native/no-temporal, so the non-runnable Spyro 2/3 products
cannot inherit Spyro 1's capability claims merely because they share the engine repository.

Gap: other scene arms and live producer variants remain unowned; temporal
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
recorded in issue 0089; after its guest-entry fix, the route reaches stage 0 and truthfully refuses
the previously incomplete FIELD scene. The latest source-owned field unit is the separate `0x80022A2C`
world/shaded queue: its stage-0 snapshot preserves 93 total records, 52 valid meshes / 936 source
primitives, and 3 visible records / 54 candidates, with both observed mesh and lighting-table classes;
the recipe resolves 23 Gouraud faces and is now called by the stage-0 seam. The regular actor owner is
also Ready on that snapshot after issue 0094's Plain descriptor-pair correction: 175 Mobys scanned,
14 records, 423 candidates, 211 rejects, and 212 faces. Its following secondary actor owner is Ready
with 3 visited list members / 1 record, 138 candidates, 63 rejects, and 75 faces. The next authored
actor-pass gap is Moby shadows: the regular native builder now stages the source-backed list entries
from fixed start `0x800724F4` and commits the shared cursor at `0x80075F00` after actor admission,
but the native actor builders/renderers do not yet own the complete Moby shadow result. Moby shadow consumer `0x80059F8C` is now owned by
`game/render/moby_shadow_recipe.*` / `moby_shadow_submitter.*` and is called by the stage-0 seam in
its authored position, between the shaded pass and Spyro's model. Its staging was separately broken:
`0x8001F344`/`0x8001F350` admit an entry only when `m_ShadowDistance` is negative AND the view depth
is nearer than `0x1200`, and the port negated that limit, which no visible Moby can satisfy — so the
list was always empty. Measured over a walk through Artisans after the fix: entries 1..5, drawn up to
2, faces up to 8, with every rejection reason (no plane, far, backfacing, off screen) observed at
least once. Spyro shadow `0x80059A48` is owned by the separate native fan recipe and submitter.
Flame `0x80058D64` is owned by `game/render/spyro_flame_recipe.*` / `spyro_flame_submitter.*` /
`fx_spyro_flame.*` and is now called by the stage-0 seam after Spyro's shadow, in its authored
position. Its missing input is resolved: `0x80023AC4` reads its live GTE rotation matrix back at
`0x8002401C` and publishes it into `g_SpyroFlame+0xB8` at `0x80024110`, gated on `g_SpyroFlame+0x9A`,
and the native Spyro producer that replaced it had dropped that publication, leaving the five words
zero and every flame point collapsed onto the flame origin. `game/render/spyro_flame_matrix.*` now
carries it, publishing the composed layer 1 matrix — the camera rotation composed with `g_Spyro+0x0C`
and then `g_Spyro+0x10` — because retail publishes between those two composition steps. Measured over
a live breath in Artisans: parts 8, tips 8, ribbon quads 8..160, and the census decays back to zero as
the flame dies. The flame's tip fan is untextured, and a first pass named that with a negative colour
mode; the queue's untextured sentinel is 3, and a painter object rejects anything else, so the next
producer's preflight refused the whole frame and the port aborted on the environment producer instead
of on the flame. That is the crash on breathing fire.

`0x80058BA8`, the last call of `0x80019698`, is a two-line C function calling the handwritten glow
renderer `0x800580F4` and then the sparkle renderer `0x800584C4`. The glow half is now owned by
`game/render/glow_recipe.*` / `glow_submitter.*`: sixteen fixed records at `0x80078800`, each fanning
semi-transparent additive Gouraud triangles from one bright projected centre out to a ring of black
points whose screen offsets are scaled by radius over depth, with retail's own delta pre-scaling,
four-edge outcode reject, and the `>> 7` ordering-table bin that steps 0x40 further back past 0xFF.
Six focused tests pass. The sparkle half is now owned too, by `game/render/sparkle_recipe.*` /
`sparkle_submitter.*`: eight records at `0x80077108`, each projected once for its centre and then a
second time through a diagonal matrix whose scale is its own view depth, which cancels the
perspective shrink so a spark keeps a constant screen size, and emitted as two crossed GP0 line
primitives. It is the one render producer in the port that also WRITES guest state — it burns each
lifetime by `g_DeltaTime` at `0x800756CC`, spins the angle byte, and kills a sparkle it declines to
draw — so the derivation stays pure and hands those writes to a named `commit`. Eight focused tests
pass, including the distinguishing case that a culled sparkle keeps its newly spun angle while its
lifetime is zeroed. Lines required a framework admission: psxport's `validateFace` admitted three and
four vertices only, and now admits two as well, untextured only, because a GP0 line carries no
texture word (psxport `25a432e3`, 145/145 tests).

The dragon-rescue cutscene renderer `0x8001CFDC` (stage 8, `GS_Dragon`) is owned by
`game/render/dragon_scene_recipe.*` and `fx_dragon_scene.*`, with the burst star `0x80058864` in
`dragon_burst_recipe.*` / `fx_dragon_burst.*`. The recipe derives which of the eight
`g_DragonCutscene.m_State` branches applies and returns it as a plan — producer list, the two Moby
lists to publish, and which source the regular actor pass reads — so the branch table is one
structure rather than eight compositions. State 0 shares FIELD's model chain through
`field_model_chain.*`. Twenty focused tests cover the recipe and the burst. Reaching it live also
fixed four defects outside the new code: the paired-actor ownership gate aborted bare because its
scene predicate did not know the cutscene draws Spyro; the Moby scale byte at `+0x57` was refused by
both actor renderers and is now implemented once in `actor_transform_math::scaledTranslation`, which
is what retail's `GPF`/`sra 5` idiom does at `0x80022CCC` and `0x8001F864`; the particle producer
`0x800573C8` emitted unordered world items and now publishes a painter object on the new
`LinkPhase::Particle` phase 0, ordered by its record's position in the guest's single emit-list scan;
and the cutscene's Spyro producer no longer applies a hide gate only `0x80019698` owns. Measured in
Artisans: the cutscene runs to completion through all eight `m_State` branches — 0, 1, 2, 3, 4, 5,
6 and 7 — with no refusal and no abort, ending on state 7's fade-out.

`0x80058BA8` is wired as the last FIELD producer via `game/render/fx_glow_sparkle.*`. Measured live in
Artisans: one active glow record fanning 4–8 faces per field, one live sparkle emitting two lines and
then aging out to `alive=0` on its own schedule, `dt=2`, no refusal and no Lightrec fallback over
20.5 M translated blocks. Its screen-edge outcode carried retail's fixed 512 right edge, which in a
684-wide widescreen frame put every vertex of a glow past x=512 outside the same edge and dropped the
whole fan: the gem halo the seeker walks to sits there, so the gem lost its glow. `outcode` now takes
the frame's own right edge. Measured with `tools/actor_oracle_diff.py` over a seeked Artisans capture:
the glow producer `0x800580F4` went from 0 of 37 oracle frames to 37 of 37, the `offscreen` reject
census over the walk fell 79 to 14, and the retail-only primitives on the final frame fell 6 to 2 (0 on
frame -20). The 2 that remain are a second, smaller `608080` fan retail registers inside `0x80019698`
itself, after the port's producers have already read the record table.
The same capture leaves an open depth question, issue 0105: 9.93% of ordered actor primitive pairs
sort against retail, and for the worst pair the port places the farther moby in front. The oracle
now prints each drawn instance's `+0x57` scale byte and the camera position, and
`actor_oracle_diff.py` turns those into a distance, so the geometry is a third opinion rather than
retail's word against the port's — measured 11706 versus 7245 units, which is the order retail sorts
them in and the opposite of the port's. Two causes are already refused by measurement: the scale byte
reads 0 on both instances, and re-normalising `pz` by the model descriptor's own exponent raised the
disagreement rate to 14.63% rather than lowering it.
The separate `0x8002B9CC`
environment/world owner now participates in FIELD composition: on the recorded snapshot it derives selection 17,
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
mid-distance portal frame. Walking up to a portal used to abort the frame: the aperture passes behind the
camera, every projected point saturates off screen with a negative view Z, and the mesh recipe
clipped all 893 candidates away into an empty recipe the submitter read as invalid. `meshVisibility`
now carries retail's own gate from `0x80051D0C`-`0x80051E70`, so such a portal contributes nothing
exactly as retail skips it; a portal walk runs 1,684 frames with no refusal. The mask still clips
against the mesh aperture rather than its own (issue 0106). Retail's last cyclorama decision is
recovered too: `RotVec8ToMatrix` (`0x80016D2C`, yaw about Y then pitch about X then roll about Z,
each composed on the right) and the camera-facing dot test at `0x80051D98`-`0x80051E6C`, so no
cyclorama path is left unrecovered. Two defects behind that route are also fixed: `0x80063D80` is
CdControlF, not CdControlB, and its two-argument ABI has no result buffer, so the result-writing
owner it was bound to wrote 8 bytes at whatever a2 held; and psxport's direct-runtime binding loop
silently dropped bindings past `kMaxBindings` instead of refusing. The `fieldsky` channel
now names each refusing draw with its frame/recipe status, refusal string and reject counters, so
the three previously silent `return false` paths in `fx_field_cyclorama.cpp` no longer abort a frame
without saying why. The current replay reaches this complete
stage-0 composition without a native-render refusal;
the acceptance boundary is now faithful visual/oracle comparison plus the remaining unowned scene
variants. ~~A normal paced audio run after the shared CDC filter fix (`scratch/logs/spyro-xa-after-filter-20260828.log`)
produces 20.02 seconds of non-silent stereo 44.1 kHz WAV for 1,200 VBlanks, with 239 selected XA
sectors on file 1/channel 4 and zero ring-full reports; the prior back-pressure came from decoding
interleaved unselected channels.~~ That measurement's only test was "non-silent", and a constant
level passes it: measured 2026-09-14 for the same window, the shared XA streamer was ending Spyro's
open-ended stream at the first foreign EOF, decoded no sectors, and the capture held a bare DC bias.
See S022. The same run reports 60.0 paced VBlanks/s and 735/736 SPU frames per
field. SBS oracle boot remains limited evidence:
the 120-field run exits cleanly but retains five stack-only differing bytes and leaves one owned
address unreached; its shared WAV sink writes zero bytes and is not audio evidence. A current paced
native run spans about 20.115 seconds for 1,200 fields (~59.66 Hz), confirms 882,882 output samples,
and reports zero ring pressure. These runs prove product timing/routing, not speaker delivery or
independent oracle PCM parity.

### S006 — Spyro 2 measured boot boundary

Binary analysis of the exact manifest-matched `SCUS_944.25` executable identified 683 resident
function entries with no foreign-title or overlay assumptions. The retired dedicated `spyro2_port`
kept its emitted guest bodies separate from Spyro 1, proving the images cannot share address-derived
dispatch. `Spyro2Runtime` has no Spyro 1 compatibility state or hooks. The retired title-owned finite boot driver reproduced
the binary's persistent game-main and boot-prefix stack frames, calls constructors `0x80054834`
and first leaf `0x800548A4`, then enters a title-owned display-bootstrap state machine instead of
dispatching either the non-returning guest main or the retained bootstrap. That owner preserves the
exact nested stack/register state and non-timing effects around three measured field waits: two
direct calls from `0x80011BBC` and a third inside clear helper `0x8004C484`. DrawSync `0x800557E4`
and GPU timeout arm/check `0x80057880`/`0x800578B4` are title-owned synchronous overrides; none
spends a display field.
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

### S007 — Spyro 1 gameplay input

The retail movement routine already consumes held digital input. The authenticated `SCUS_942.28`
loads `[activePad + 4]` at `0x8003D4A0` and `0x8003D4BC`; this is the buffered held word.
The reference decomp declares `g_ActivePad` as the outer `Gamepad*`, where the same `+4` offset is
named `m_Released`. Treating that mistaken type name as buffered release-edge semantics introduced
a redundant native movement override. The guest selector at `0x80043FE4` instead selects the
24-byte buffered record at `0x800773BC + 24 * substep`. The override and its shift-only test are
removed; movement remains owned by the unchanged retail body through Lightrec.

`test_spyro_digital_input` is a local, asset-dependent production-boundary discriminator: it
authenticates the supplied executable, runs press/hold/release/idle states through the retail
function, and checks target speed/rotation, turn momentum, preserved ABI and nonzero JIT execution
with zero fallback. Run explicitly after building the target:
`build/test_spyro_digital_input <path/to/SCUS_942.28>`. It is not an asset-dependent hosted
CTest gate. The operator's focused run passed all 4/4 cases: press and hold each executed 3 JIT
blocks / 40 instructions and produced speed 1536, rotation 992; release and idle each executed
3 blocks / 33 instructions and produced speed 0, rotation 592. Total execution was 12 JIT blocks
/ 146 instructions with zero interpreter fallback. This proves the isolated retail input-consumer
contract and preserved ABI, not representative gameplay conformance.

Historical idle-versus-Left replays changed player position after the New Game handoff, and jump,
charge, and flame reached the guest update. Those observations do not establish the false
release-edge explanation or complete current gameplay parity.

Gap: current interactive jump, charge, flame, and controller parity still need representative
gameplay comparison against the independent console oracle.

### S008 — Runtime Lightrec execution

Implemented subset: the product enters authenticated crt0 through psxport's per-`Core`
`dispatchGuest` boundary. Image-aware native dispatch, scoped `callOriginal`, invalidation, and typed
exit contracts are present, the frozen PSXport/Lightrec backend is linked, and the synthetic framework
contract passes. The old generator/dispatcher/selector/product are absent.

Real-media evidence (2026-09-05, Linux x86_64, Clang, framework `eb5f23a8`): the authenticated
`SCUS_942.28` initially stopped at `0x80064F0C` after one cycle budget, with 43,550 executed JIT
blocks and zero fallback. The root entry point incorrectly treated a normal budget yield as fatal.
`GuestExecution::step` now preserves the initial return address and committed continuation across
budgets; it propagates every other exit without silently resuming it. Four synthetic production-path
scenarios cover an uninterrupted nested call, the same call over 125 yields, an unknown-image fault,
and a native frame exit. Both nested-call variants execute 1,002 JIT blocks / 4,008 instructions and
produce the same result with zero fallback; repeated terminal steps execute nothing.

A debugger observation at the next entry after 100 completed executor calls measured
844 translations, 3,090,158 executed blocks, 21,759,844 executed instructions, 258 host dispatches,
3,089,314 cache hits / 844 misses, zero executor faults, and zero fallback blocks/instructions.
The continuation was `0x8001647C`. The real disc opened; the 15-second headless/no-audio observation
reported CD synchronization timeouts and was externally bounded. This establishes continuation and
real-image JIT execution, not successful boot or CD correctness.

Current integration (2026-09-08): Spyro now composes its native boot, display-field, and scene
owners around bounded guest calls. The framework exposes stock CD command success separately from
blocking-control success and supplies the title disc key. The title WAD owner stages complete reads
before publication, refuses truncated input with a typed fault, derives image identity from SHA-256,
and relies on canonical guest writes for invalidation. Synthetic production tests cover short reads,
per-Core completion, changed-code execution, and missing-disc faults. These changes pass the recorded
stage-13 routes below; the prior CD timeout no longer describes the current frontier.

Gap: representative gameplay, independent-oracle state, and real-game WAD replacement/override
coverage remain unqualified. Synthetic changed-image tests do not prove every gameplay overlay.

Atomic work: issue 0101.

### S009 — Stage-13 dynamic discriminators

Evidence: the Linux x86_64 Clang product, using framework `f7d4baf5`, completes the 800-field idle title
route and the 900-field Start-input save-picker route through native field/scene owners. Guest VSync
remains the framework's mandatory fatal trap. Run caps now return from the complete product step
before ending, so the final presentation fence is checked and the Core is destroyed normally.

The idle run ended at 800 fields / 600 checked steps and fences, with 3,144,960 executed JIT
blocks / 20,096,897 instructions and zero faults/fallback.

The 16:9 save-picker observation ended at 900 delivered fields, 474 completed product steps and 474
presentation fences, with 1,485 translated blocks, 3,116,634 executed blocks / 20,245,455 instructions,
zero executor faults, and zero fallback blocks/instructions. It reached stage 13/mode 2/state 4.
The inspected presented image contains all three slots, menu/footer text, Spyro, and expanded
backdrop; native render width is 684 versus the 512-wide baseline. Interpolation was enabled;
this still image and menu run do not qualify smooth gameplay or all renderer arms.

Reproduce using `build/bin/spyro_port` with `PSXPORT_NOAUDIO=1`, `PSXPORT_VK_WINDOW=0`,
`PSXPORT_ASSET_DIR=external/psxport`, `PSXPORT_NATIVE_FRAMES=900`, and
`PSXPORT_FORCE_BUTTONS=FFF7`. Use `PSXPORT_SETTINGS` pointing to an isolated file containing
`aspect=1`, `ires=1`, and `fps60=1`; `PSXPORT_ASPECT` is not a supported option.
`PSXPORT_PRESENT_SHOT_AT=450` captures the inspected menu. The idle discriminator uses an 800-field
cap and no forced input. These observations establish wiring, not representative gameplay.

### S010 — Resumable world execution

Implemented subset: `game/core/world_body.inc`, its native transcription, and emitted-body calls are
gone. `WorldGuestExecution` resumes unchanged retail `RenderWorldChunks` at `0x800258F0` through the
scoped-original runtime boundary.

Gap: the Lightrec backend must execute that boundary and prove bounded host-service exits resume the
same guest CPU state. No body transcription or interpreter fallback may replace it.

### S011 — Representative gameplay conformance

A current Linux x86_64 Clang observation with framework `ed134133` reaches Artisans stage 0,
accepts held Left, and returns cleanly after 2,744 fields / 1,367 product steps and presentation
fences. Player position changes from `(0x14C00,0x0B845,0x02554)` to
`(0x14991,0x0B289,0x0267B)` over 60 delivered fields. It executes 9,497,188 JIT blocks /
75,262,114 instructions, with 3,683 translations and zero faults or interpreter fallback.

This route first exposed a shared GTE-transfer defect: replaying MTC2 writes while copying the
register bank overwrote IR3 through IRGB's write side effect. The collision loop at `0x8004E9D8`
then repeated unchanged forever. An isolated Mednafen CPU window from the captured live arithmetic
input reaches `0x8004EB34` in 153 steps / 837 oracle cycles with RAM and scratch unchanged. The
incoming jump's delay slot is an ADDI, with no pending load; this comparison explicitly normalizes
external device/timing history and proves this arithmetic window only. The framework now transfers
raw banks and materializes SXYP's alias without shifting the FIFO; shipping-JIT GPF/SQR, FIFO and
roundtrip regressions pass 111 checks, and its combined gate passes 133 tests.

The current Artisans observation uses the native/Lightrec product with widescreen and fps60 enabled
and an independent full-console reference using the user's NTSC-U SCPH-1001 v2.2 BIOS. Spyro is
visible at spawn and above the fountain wall after movement; paired local-to-global OT coalescing
and the explicitly owned shadow projection are documented in
[paired-actor-world-order](findings/paired-actor-world-order.md).

Issue [0102](issues/0102-native-delivered-fields-undercount-guest-vblank.md) resolves duplicate
VBlank delivery and records the exact comparison: native boot/title and Artisans counter deltas
now equal delivered fields; Left for 60 fields ends within 1–2 guest units per player axis of the
oracle. A newer authentic-console comparison at the same Artisans level tick and player position
found the native game tick three behind and a different camera state before Left input. Issue
[0110](issues/0110-artisans-camera-checkpoints-are-not-yet-phase-aligned.md) records the reached
CameraUpdate observer and the first-divergence target; camera state still differs, so this is not
exact-state or complete visual parity. The run
exits 0 after 4,061 fields / 2,062 product steps and fences, with nonzero JIT execution, zero faults
and zero fallback. Paired temporal emission remains positive. Complete FIELD interpolation,
paced audio/performance and all released hosts remain unqualified. The console reference uses an
independent CPU/scheduler but shares Beetle device lineage, so it cannot exclude all common defects.

The title-native screen-actor queue now executes its authenticated GTE offset restore at completion.
A reached retail queue previously left OFX=100 and projected camera X=100 at the first gameplay
tick; after the restore, the same actor write was reached, the queue exited at OFX=256, and the
camera projected X=256. New Game handoff timing and full independent output parity remain open.

The state-aligned comparator (`tools/oracle_compare.py`, framework `tools/oracle/compare.py`)
drives the product and the SCPH-1001 full-console reference by observed guest state with identical
per-frame pad delivery and a blank memory card on both sides. Measured 2026-09-18: every decisive
range matches at all fourteen checkpoints of the Artisans route, `save_picker` through
`gameplay[11]`, including `g_Spyro.m_Position`, `m_State`, `g_GameTick`, `g_StateSwitch`, the three
`g_Pad` words and the collision query's occlusion result. Two causes were fixed to reach that: the
REPL parked one poll behind the console's VBlank phase, and Lightrec dropped the load-delay commit
on a delay-slot load whose branch constant propagation had turned into a NOP, so the collision
query read the wrong table (`shared/lightrec` `3fddb23`). Issue 0110 holds the measurement. One
matched route is a first conformance result, not representative-gameplay conformance: output parity
is separate and the released-host budget is unmeasured.

That route is now compared at full resolution. `tools/oracle_compare.py --frame-step 1` compares
every declared range after **every one** of the twelve gameplay segments' 477 game frames instead of
once per segment: 485 comparisons, **zero divergences**, exit 0, 153 s. A segment-end comparison
cannot tell a state that never diverged from one that diverged and came back, and this one shows it
never diverged.

Two attempts to widen the route past the homeworld both failed, and neither failure is the product
being wrong about a level. A `level` checkpoint that walked each core out of Artisans through a
portal, steering from that core's own camera, let the two cores steer apart: they agreed for two
decisions, drifted from the third, pressed different buttons from the tenth, and entered level 11
from two different places, which the decisive `player.position` range duly reported as a DIVERGE.
Recording the reference's 473 steered frames and replaying exactly those on the product removed the
input as a variable, and then the product did not reach any portal within the 6000-frame budget.

The cause is measured and it is one counter. Spyro's d-pad is camera-relative, and the per-frame
report locates where the camera parts company: one frame after `g_DeltaTime` reads 2 against the
console's 4, the camera's Euler rotation differs by about 1.3 degrees of yaw while its position,
destination, state, occlusion group and every spherical block stay byte-identical. `g_DeltaTime` is
read from `g_LevelTicks`, whose constant per-load offset issue
[0110](issues/0110-artisans-camera-checkpoints-are-not-yet-phase-aligned.md) parked as a pacing
convention. That residual moves no decisive range under held input across 477 frames, so 0110's
result stands, but it does steer, so no camera-relative route replays. The oracle therefore
registers only the checkpoints it can honestly compare, and issue
[0114](issues/0114-no-reproducible-route-out-of-artisans-so-level-entry-is-uncompared.md) owns the
consequence: the product's discard and reload of guest code at a reused load address, and the
translation invalidation that follows it, is still outside every comparison.

One hard stop on that route was separately found and fixed. A secondary actor whose triangle carries
control bit 2 takes the per-face colour program at guest `0x80021DB4`, which the native producer
refused rather than draw with the base material colours that program replaces, aborting the product
shortly after it entered Stone Hill. The program is now ported as a pure owner validated against the
real GTE; the separate additive program at `0x80021FE0` and the quad billboard at `0x8002256C` still
refuse, and neither has been observed on a driven route. Issue
[0113](issues/0113-attract-demo-aborts-secondary-shaded-producers-r.md) holds the deterministic
reproduction, the transcription and what remains refused.

The handoff field-delivery bracket (issue 0110) attributes the residual camera-checkpoint phase
difference to pacing rather than camera math. The console's loader store and its first stage-zero
game tick are one field apart, and that field carries `g_StateSwitch == 1`, which is the main
loop's own draw gate: `func_8001A050` waits for two fields since the previous draw
(`while (pre - post < 2) VSync(0)`), and `src/main.c:21` skips that draw entirely while a state
switch is pending. Retail therefore has no fixed field quota. The frame driver now models that
directly: one product step runs however many draw-less guest iterations the guest chains, then the
drawn one, and only the drawn iteration spends the two-field quota. `g_LevelTicks` still carries a
constant offset from the load boundary, growing from one at level entry to five across the compared
route; timers read from that counter fire early by the same amount. That residual is informational
and is what the remaining `player`, `camera` and `dragon_cutscene` byte deltas follow from.

The post-entry shadow boundary is now exercised on the same real portal route. After rebuilding the
native target, `tools/drive.py gameplay --gate-teleport 0:0 --seek-portal --skip-transitions
--after 1200` reached the destination level and exited 0; the field shadow producer reported 16
faces on each sampled frame and Lightrec reported zero fallback blocks and instructions. Enabling
the actor semantic oracle on a 300-field route compared 440 frames: 383 retail primitives and 403
native primitives yielded 380 matches after the measured -86-pixel presentation offset, with three
retail-only and 23 native-only primitives. Those 23 native-only primitives were attributed to the
shaded/sprite-queue arm `0x80022A2C` and looked like over-inclusion (issue 0111); they were an
instrument artifact. The oracle's packet decoder accepted only Gouraud polygons while that pass emits
the flat family (`0x20`/`0x22`/`0x28`), so every shaded packet was refused and dropped, and the walk
printed no denominator that could show it. The decoder now covers both polygon families, the walk
prints `scanned`/`below_filter`/`refused`/`decoded` plus the refused command codes, and the same
Artisans captures give `0x80022A2C: 22-26 submitted / matched on every frame / 0 unmatched` with
`retail only: 0`, so that arm's colour, semi-transparency and depth rules are parity-verified. The
console confirms the arm draws: retail's shaded pass accepts the same nine world records the native
does (`shaded_flagged=9->9`, including the three `class 83` gem nodes) and advances its per-record
commit pointer for eight of them. The flat texture-word family (`0x26`, eight packets per frame) is refused by name with its code
printed, because a guessed word order was falsified against the matched set. This is a concrete
comparison discriminator, not full scene parity; the remaining actor/depth differences, the
phase-sensitive shadow-arm comparison, the residual `g_LevelTicks` offset, and the unmeasured
per-host frame-time budget keep S011 missing.

The user-reported crash after THE ADVENTURE BEGINS is reproduced and its first cause is closed.
Nine earlier agent observations missed it because they all drove with `tools/drive.py`, which takes
its own route into `GS_Playing` and stops; launching the product through the player environment with
no pad input at all lets the attract demo play itself into a scene holding particle type 3, which
the field-particle producer refused. Two defects, both fixed: the producer had no type-3 arm, and
`abortUnimplemented` dereferenced a null `Core::cfg` and killed the fatal path two lines into its
own report, so the operator saw a segfault instead of the named refusal, its ARMED backlog, or the
RAM snapshot — the same class as resolved issue 0090. Type 3 is a POLY_FT4 sprite like type 2 but
axis-aligned with independent half-extents scaled by the RTPS depth cue, so the two arms now share
one submitter. With denominators: 435 frames decode type-3 records, up to 52 in one frame, 42
sprites submitted. The demo route now advances from frame ~2,475 to frame 5,382, where the regular
actor layer refuses. That refusal named an address and nothing else; it now carries its reason like
every other layer, and reading it moved the answer three times before it was true: `Reason::Malformed`
was one value for four distinct defects plus one unreachable branch, and `populate()` decoded colour
offsets for a quad arm that never indexes them, reporting `color-offset` where the real gap is the
unported billboard quad program — `0x800205C4` in this renderer, the sibling of `0x8002256C` that
issue 0113 describes. That arm is now ported: one projected vertex, a forced-DQA depth divide, two
half-extents packed in the material word, and a ten-word POLY_FT4 with command `0x2C`, owned by
`spyro::actor_billboard::extents` and carried through as `Family::Billboard`. That reached
**frame 9,346**, where the secondary actor producer refused on the second per-face colour program
`0x80021FE0` — which issue 0113 recorded as never yet observed being reached. It is ported too: one
constant added to each vertex's red with saturation and subtracted from green and blue with a floor
of zero, and a command byte stored as exactly `0x34`, which makes such a face opaque against its own
material word. `face_light` now returns three colours rather than one, and `Status::Additive` is
gone. The route then reaches **frame 15,210**, refusing on particle type 6 — the unported default
arm of the same producer whose type 3 started this. Particle types 4, 5 and the default arm remain unported and will refuse
the same way, now legibly. Separately, 28 of 71 `tests/test_*.cpp` were compiled by no target at all —
one had asserted a refusal removed from the product and no longer built. All 28 are registered and
pass (47 CTest entries to 76; the C++ quality gate went from 192 to 221 translation units), and
`verify.py` now refuses an unregistered test source by name. Issue
[0128](issues/0128-user-reported-crash-after-the-adventure-begins-card-not-yet-reproduced.md) holds
the measurement.

Missing capability: a bounded interactive Spyro 1 route must reach at least the current gameplay
frontier with native and scoped-original dispatch, positive and controlled-negative WAD invalidation,
independent-oracle timing/memory/interrupt/device comparison, and the declared correctness/frame-time
budget on every released host architecture. The obsolete generator, corpus, seeds,
dispatcher/tests, and old build/provisioning route have already been removed break-first and may not
return as a fallback.

### S012 — Native/Lightrec launcher

Implemented subset: the frozen launcher authenticates the user's disc, provisions only the selected
PS-X EXE, builds the sole `spyro_port` native/Lightrec target under `build/player`, and launches Spyro
1 by default without offline guest translation or a pre-populated runtime cache. The native target
links the frozen PSXport/Lightrec backend, and its asset-free synthetic framework contract passes.

Gap: cold-path provisioning and packaged first-run setup remain unqualified. The native boot/title
routes are recorded in S009; representative gameplay and host qualification remain open.

### S013 — Windows release

Missing capability: a Windows build/package CI job, installable asset-free release, first-run file
selection, and synthetic execution/package checks on the supported Windows architecture. The
current hosted workflow performs Linux source-policy checks only. Real-title gameplay and
performance remain local qualification requirements.

### S014 — macOS application release

Missing capability: a macOS `.app` bundle and release CI, matching-architecture dynarec/ABI/cache
checks, first-run file selection, persistent user data, and local gameplay qualification. Apple
Silicon support requires an actual AArch64 backend and executable-memory/instruction-cache
qualification; interpreter fallback does not satisfy it.

### S015 — Linux AppImage release

Missing capability: an AppImage builder/release job and cold package-launch/install tests. The local
Clang executable and interactive observations are useful inputs, but do not qualify a relocatable
AppImage, its runtime dependencies, or no-terminal first-run selection.

### S016 — Android APK release

Missing capability: APK packaging/release CI, ARM64 runtime qualification, game-file selection,
authored multi-touch controls, and measured performance on named device classes. Build and emulator
mechanics belong in `shared/android-port`; Activity/SAF/lifecycle mechanics belong in Lucent. The
title owns identity, complete-install policy, touch meaning/layout, and composition. Desktop
measurements and compile-only CI do not establish Android gameplay support.

### S017 — WASM release and GitHub Pages deployment

Missing capability: a browser build using the same runtime guest-discovery/execution contract,
browser package tests, and a CI deployment to GitHub Pages. Browser dynamic execution must preserve
JIT-first semantics with bounded accounted fallback; a missing web backend cannot silently select
an interpreter product. Acceptance includes user-selected local game files, persistent browser
saves, controllable gameplay, audio/input correctness, and measured browser performance. No game
files may be embedded in the deployment.

### S018 — Packaged game-file setup

Missing capability: desktop/mobile native file pickers and browser file selection that authenticate
the complete supplied install, persist it in the OS/browser user-data location, support reselection,
and preserve a prior valid selection on every failure. The existing launcher authenticates and
provisions the disc through maintainer inputs; it does not provide packaged first-run UI. Shared
archive/SAF mechanisms must be consumed from their canonical owners rather than copied into Spyro.

### S019 — Additional widescreen scene coverage

Evidence: the native Artisans observation renders a 684-pixel scene from the original 512-pixel
projection width, with additional horizontal world geometry and an unstretched player. The world
sector preparation now widens the authored horizontal culling plane by the same explicit width
used for projection and animation admission. Focused production tests cover both edges, preserved
near-eye acceptance, native-width behavior, and animation of newly admitted sectors exactly once.

On the Lightrec product (2026-09-18, `external/psxport/tools/port/looks_right.py --repository .
--binary build/bin/spyro_port --replay replays/gameplay/artisans-arrival.pad --frames 7200
--shot-at 3700,3300,3750 --env PSXPORT_WATCHDOG=60`): the 4:3 and 16:9 runs both reach 7,201 fields
with no failure mark, and the wide frame differs from the 4:3 one. The Artisans courtyard capture
at the dragon rescue shows a genuinely wider field of view — the hedge and arena rim on the left
and a further castle building on the right that the 4:3 picture cannot see — at unchanged object
proportions, and the level-intro card's 2D text stays centred. The default watchdog aborts this
route: a level load blocks presentation past its three-second frame-progress timeout, so an
unattended replay run must raise `PSXPORT_WATCHDOG`.

Oracle state parity with widescreen ON (2026-09-19, `tools/oracle_compare.py --bios ../SCPH1001.BIN
--frame-step 1 --product-env PSXPORT_WATCHDOG=60 --product-env PSXPORT_SETTINGS=<wide.ini>`): 485
checkpoints, 6,305 decisive range comparisons, **0 divergences**, run complete. The baseline leg —
same route, same checkpoints, enhancements off — is also 485/6,305/0, so the widescreen leg is
compared against a reference the product already matches rather than against a lower bar. The
comparator was shown the other answer first: `--selftest` seeded a byte at 0x80078A58 and the run
DETECTED it, so a silent comparator cannot account for the zero.

The enhancement was proven live in that exact run rather than inferred. The oracle's own product log
(`scratch/oracle/native.log`) carries `[wide] native picture: aspect=1 wide_engine=1
native_width=512 render_width=684` and `[cfg] PSXPORT_SETTINGS = ... [env]`. This matters because
`tools/drive.py` overrides `PSXPORT_SETTINGS` from its own `--settings` flag: a first proof attempt
through that tool reported `aspect=0 wide_engine=0 render_width=512` while appearing to pass.

What this does and does not establish: widening the projection to 684 px perturbs **no** guest state
the oracle observes, so widescreen is non-invasive to the simulation. It is not evidence that the
additional horizontal pixels are correct — the oracle compares guest state at checkpoints, not
images.

Drawn-coverage measurement (2026-09-19, psxport `a1537b73`'s `coverage` measure over every paired
4:3/16:9 capture in `scratch/`, 11 pairs). The number is the aspect of everything the port actually
drew, which is what moves when a picture genuinely widens rather than merely rescales:

| scene | 4:3 | 16:9 | |
|---|---|---|---|
| Artisans 3D scenes (f3000/3700/3750, five directories) | 1.429 | 1.912 | wider |
| `looks-right` f399 | 2.286 | 3.054 | wider |
| level-intro card (f3300) | 1.992 | 2.000 | no gain — the scene is black, so there was nothing to gain |
| `secondary` f399 | 1.466 | 1.470 | no gain — IDENTIFIED: the Universal boot logo, an upload-only guest-VRAM 2D picture ([0118](issues/0118-the-24bpp-boot-logo-gets-a-black-band-through-it-in-widescreen.md)) |

Eight of eleven confirm the widened picture carries genuinely more content and is not the same
picture scaled down. Tomba! 2, measured the same way, has three full-screen 2D pages that do NOT
widen (its issue 0010).

`secondary` f399 was recorded here as unexplained rather than assumed benign, and that was right: it
is the Universal Interactive Studios boot logo, and at 16:9 it was **visibly broken** — an opaque
black band straight through the picture at display columns 342..454. The cause was in psxport, not
this port: `plan_wide_margin` built its coverage rect from display widths in VRAM halfword space,
which is correct at 15bpp but two thirds of the intended position at 24bpp, where a pixel spans 1.5
halfwords. Fixed in psxport `ac3d1db4`; verified gone by re-measuring interior black column runs
(160-px run at sink columns 480..639 present pre-fix, absent after, with both legs still carrying the
same 487 display columns of picture). See issue
[0118](issues/0118-the-24bpp-boot-logo-gets-a-black-band-through-it-in-widescreen.md).

The remaining no-gain is now explained rather than unknown: the logo is an upload-only guest-VRAM
picture with no geometry to widen, and this port's 2D widen is deliberately disabled (claim C143), so
it is left-anchored inside a correctly black margin. Widening a 2D-only boot picture is gated on
C143 and is a separate item.

This measurement exists because `looks_right.py`'s `widescreen` check asks only whether the two PNGs
DIFFER, which rescaling also satisfies. Earlier "widescreen PASS" lines in this document therefore
do not by themselves establish that a scene gained coverage.

Scene sample widened 2026-09-19 (issue 0124). Six distinct deterministic gameplay scenes, each
captured at 4:3 and 16:9 and put through `widescreen_pair.py`'s translation-vs-stretch
discriminator: **all six read WIDENED**, at separations of 4.4x to 10.3x over the stretch
hypothesis, every one aligning at the predicted dx=+86. The probe is deterministic (the same
invocation twice gives a byte-identical capture) and the discriminator's `--selftest` still reads a
resampled picture as STRETCHED, so it can say both things.

That sample also found two defects the earlier two-capture evidence could not have. **The claim
above that widescreen "perturbs no guest state the oracle observes" was true only because no
declared range covered it**: both field-particle producers computed a guest visibility byte from the
WIDENED horizontal window and wrote it into the guest's particle record, so enabling widescreen
changed guest memory. Fixed by `game/render/particle_screen_space.{h,cpp}`, which separates the
guest's own 512-px window from the widened draw window; the 4:3 capture is byte-identical after the
change and the widescreen particle count falls from 20 to 12. Second, and STILL OPEN: a solid
(248,96,0) quad is drawn at 16:9 inside the shared field of view where the 4:3 frame draws grass
(241 px). Every region of the frame aligns at dx=+86 with the background byte-identical, so it is
wide-only geometry rather than a misalignment. See issue 0124 for the per-producer bisect.

Both issue-0124 defects are now fixed. The paired actor was projected about the 4:3 centre while the
world used the widened one, so Spyro sat ~86 px left of the scene at 16:9; `wide_screen_space.h` owns
the horizontal centre for every producer and the measures on the same scene went Spyro-box MAE
28.02 -> 2.60, wide-only pixels in the shared field 241 -> 0, overlap differing 6.8% -> 3.0% (the
residual being grass dither). All six scenes re-read WIDENED with separations of 8.0x to 20.5x,
roughly double the pre-fix figures. Oracle parity with widescreen ON after both fixes is 485
checkpoints / 6,305 decisive comparisons / 0 divergences / complete, with the seeded-byte selftest
DETECTED in the same configuration.

2026-09-20, the joins. `widescreen_pair.py` previously answered one question -- is the wide frame
a translation of the narrow one, or a stretch -- which says the ORIGINAL picture survived and
nothing about what replaced the empty margins. Two fakes live exactly there and both score a
perfect translation, because both leave the centre untouched: a margin holding no scene, and a
margin holding scene drawn by a projection of its own. The tool now asks all three, and Spyro 1
answers all three at the settled Artisans state `tools/widescreen_check.py` drives:

- 512 -> 684, **3.74 mean absolute error at dx=+86**, the predicted offset, against 15.79 at the
  next best offset, 101.42 at the worst of 189 tried, and 76.09 for the stretch hypothesis: 20.3x.
- both 86px margins 93.3% non-black, 582 and 579 distinct colours, 0 of 85 repeated columns.
- the joins at x=85 and x=597 differ from the columns beside them by **1.01x and 0.71x**, against a
  limit of 2.0x. The same frame with its margins drawn six rows off reads 7.84x and 7.27x, so the
  discriminator was run against both classes on this picture and not only on fixtures.

The join check is LOCAL by measurement rather than by preference: Tomba! 2's widest ordinary column
pair is 42.33 -- its scene contains a hard vertical edge -- while a deliberately broken margin there
reads 33.80, so a frame-wide percentile would have hidden the break behind the scene's own edge.
Its known false positive is stated in the tool: a scene whose own hard edge falls exactly on a join
reads as a break, and the answer is to re-measure at another state.

Gap: complete scene variants and horizontal culling owners remain unqualified -- six Artisans
gameplay scenes plus the courtyard and intro card do not prove the whole game, and no other level
has been captured. A same-state oracle VISUAL comparison under widescreen remains impossible: the
picture oracle correctly refuses to compare a 684-wide product against a 512-wide console, so the
widescreen visual evidence is the port's own 4:3-vs-16:9 pair rather than a console comparison.
What that pair now establishes is that the picture is extended rather than resampled, that the
extension contains world, and that the world does not break where the extension meets it. What it
still does not establish is that the world shown there is the RIGHT world: no 16:9 reference exists
for a PSX title, and no instrument in this project can settle it.

2026-09-20: widescreen RENDER correctness is now measured, not only state parity.
`tools/widescreen_check.py` drives one settled state at 4:3 and at 16:9 and compares the product
with itself: 512 -> 684, the central 512 columns surviving at 2.17% a different colour, both 86px
margins 93.3% non-black with ~580 distinct colours and 0/85 repeated columns. A stretch of the same
frame reads 53.91% in the centre, so the check separates them by 25x. This does not establish that
the extra geometry is correct -- no 16:9 reference exists -- but it does establish that the picture
is extended rather than resampled, which is the distinction S019 is about.

2026-09-20, CORRECTION: the "14/14 checkpoints byte-identical with the enhancements off versus on"
recorded above was NOT a widescreen measurement. tools/shipping_settings.ini asked for aspect=3,
ASPECT_AUTO, which resolves to the sink's aspect; an agent run is headless, so both arms rendered
512 wide. Re-run with aspect=1, the product announcing render_width=684: 15 checkpoints, 0 decisive
divergences, complete. That is the widescreen state-parity claim, evidenced for the first time. See
docs/issues/0130.

2026-09-20: the 4:3 console picture comparison it falls back to is now interpretable, which it was
not. `settled_play` (GS_Playing at g_GameTick 180) is reached by both cores after the SAME 179 game
frames, against 1532 vs 2300 at the old `playing` checkpoint, so the Artisans courtyard is
photographed at one moment on both sides; and the comparator now separates a 15-bit colour-rounding
difference from a real one. The courtyard reads 30922/122880 pixels (25.16%) a different COLOUR,
spread over 352/480 tiles, with 36191 further pixels differing by one colour step or less. The
residual is polygon edges, dither, and two moby objects (Sparx and a sparkle); no contiguous region
is drawn wrongly. That is the first readable gameplay picture number this project has, and it is
still a spread residual, not a clean bill of health -- it is measured under
`tools/reference_settings.ini` (4:3, fps60 off), because a widescreen frame is a different SIZE and
an interpolated one corresponds to no guest state. See docs/issues/0126.

### S020 — Source-based 60fps interpolation

Evidence: Spyro 1 installs a per-instance temporal scene source. It reconstructs the compatible
paired player model from immutable captured geometry while the framework preserves other authored
FIELD producers in their global painter order. Endpoint and midpoint emission share the recovered
local-to-global OT mapping, and camera-depth motion derives its ordering inputs from captured base
MAC-Z rather than requiring a stationary depth origin. Actual presenter tests exercise mixed-world
ordering, empty endpoints with visible midpoints, immutable sources, and moving depth. The S011
run records nonzero paired midpoint/endpoint emission through the shipping JIT product.

Endpoint and midpoint floating projection share the framework's `project_view` formula. The
presenter regression preserves stationary fractional geometry at depths 1000 and 40000 across
both endpoints and the midpoint; it failed before removing endpoint XY truncation and midpoint
depth narrowing. Exact integer GTE projection remains a separately verified framework contract.
An Artisans observation with this shared projection emitted 1,035 midpoint/endpoint pairs
(2,070/2,070 nonempty outputs) and executed 21,244,084 JIT blocks with zero fallback. This proves
the path was exercised, not matched-checkpoint oracle parity or full-scene interpolation.

On the Lightrec product (2026-09-18): the looks-right fps60 leg over the same 7,200-field replay
reports 1,216,422 interpolated prims across 3,374 extra presents, so the extra presents carry
reconstructed geometry rather than duplicated frames. Still images and a prim count do not prove
temporal smoothness, and no frame-time budget has been measured on any released host.

Interpolation measured AT THE PIXELS for the first time (2026-09-19,
`external/psxport/tools/port/fps60_check.py --dir scratch/framedump --tile 16`, 81 real/interp/real
triples captured with `PSXPORT_DEBUG=fps60dump` on the `--seek-class 83` gameplay route):

| class | tiles | share | what it means |
|---|---|---|---|
| BETWEEN | 38,233 | 98.3% | differs from BOTH endpoints -- the shape of a genuinely lerped prim |
| AHEAD | 318 | 0.8% | identical to real(N) |
| STALE | 239 | 0.6% | identical to real(N-1) |
| STATIC | 90 | 0.2% | all three agree; not evidence either way |

An endpoint tile is only a defect if its content actually moved, so the same run measures the best
integer translation per tile: of the 557 endpoint tiles, **423 translated 0px** (sub-pixel change
quantised onto one side, which is correct output) and **134 translated a whole pixel or more while
still being drawn at an endpoint**. That 134 -- 0.34% of tiles -- is the residual, and it
concentrates on the y=0 tile row. The instrument reports all four classes and both sides of the
0px/1px+ split from one run, so this is a measurement rather than a confirmation.

This is the evidence S020 previously lacked: the oracle parity below shows interpolation does not
CHANGE guest state, which is necessary but says nothing about whether the extra frames interpolate.

Oracle state parity with interpolated 60fps ON (2026-09-19, same driver and route as S019, with
`--product-env PSXPORT_FPS60=1`): 485 checkpoints, 6,305 decisive range comparisons, **0
divergences**, run complete — identical to the enhancements-off baseline leg. The product log of
that run states `[fps60] TRUE per-object interpolated 60fps ON (source: env)` and `[cfg]
PSXPORT_FPS60 = true [env]`, so the tier was live and not silently refused. That refusal is a real
branch, not a hypothetical: `SpyroRuntime` declares `temporalInterpolation = false`, and only
`Spyro1Runtime`'s `RenderCapabilities::interpolatedNative()` override turns it on, so a title that
inherited the base would log `interpolated 60fps REFUSED` and still pass every checkpoint.

This establishes that reconstructing and presenting midpoint frames writes no guest state the oracle
observes. It does not establish that the interpolated pictures themselves are right: the extra
presents are never sampled by the comparator, which reads guest memory at logic-frame checkpoints.

Regular actors — the 0x8001F798 producer, the largest FIELD actor layer — gained their own temporal
source on 2026-09-19. The producer retains its own record corpus as one endpoint per logic frame;
the interval pairs records by Moby instance in occurrence order and samples each pose through the
same `ProjectionStream` the world uses, so every sampled vertex comes from its own endpoint's model
vertex through its own transform. A record with no compatible predecessor is drawn at its own
endpoint instead of replaying the previous frame's picture for it. `actor_emit` owns the one
compose/preflight/publish path both the logic frame and the reconstruction go through.

Measured on the same 7,200-field Artisans replay: the looks-right fps60 leg reports 2,109,213
interpolated prims across the same 3,374 extra presents, against 1,216,422 before. By the per-layer
census the reconstructed share of captured items rose from 0.323 to 0.560, and world-layer prims
replayed verbatim fell from 4,843,384 to 3,057,360. The actor interval is admitted on 2,608 of
3,382 logic frames, and inside an admitted interval 100,095 of 101,060 records (99.0%) carry a
sampled pose: 755 are incompatible (525 a changed model descriptor, 220 a changed depth scale), 210
have no predecessor, and the sampler refused none. The 485-comparison per-frame Artisans oracle run
still matches the full-console reference with zero divergences, so the logic-frame route is
unchanged. Secondary actors (0x80020F34), world-shaded sprites (0x80022A2C), shadows, particles,
glow and tracers still replayed verbatim in an in-between present at that point; the first two have
since gained their own sources.

Secondary actors — the 0x80020F34 layer, the second largest FIELD actor layer — gained their own
temporal source on 2026-09-19. It owns nothing the regular layer already owns: the endpoint
lifecycle and the consecutive-frame admission rule are `spyro::temporal::Pair`, the pairing and the
measured identity rule are `spyro::actor_pairing`, and the route from a ready recipe to the queue is
`actor_submission`. Only two things are this layer's own. Its endpoint is the whole secondary scene
frame rather than a record corpus, because the recipe reads the frame's per-record lighting control
word and its shadow list. And its records sit inside a larger per-actor struct, which is why the
shared pairing takes pointers: one implementation serves a flat record vector and a nested one.

Measured on 2026-09-19 over the same 7,200-field Artisans replay: the looks-right fps60 leg reports
2,141,330 interpolated prims across the same 3,374 extra presents, against 2,109,213 before, and the
reaches and widescreen verdicts still pass. The FIELD composition runs on 430 of 3,382 logic frames
— the replay's Artisans stretch — and refuses none of them; the secondary interval is admitted on
429 of those 430. Across 2,145 reconstructions 2,135 records carry a sampled pose, 10 are
incompatible, none is unpaired and the sampler refused none. The gain is small because this route
passes few secondary actors: driven to Artisans through `tools/drive.py gameplay` and walked left,
the layer draws up to 82 faces per frame with 557 of 567 reconstructions sampled. Prim counts and
admission rates prove the path is exercised, not that the motion is smooth, and no frame-time budget
has been measured on any released host.

World-shaded sprites — the 0x80022A2C layer, the other half of the FIELD composition — gained their
own temporal source on 2026-09-19. Its endpoint is the recipe's own input rather than the scene
frame: the rest of a scene frame is the shadow cursor and the visited/transformed actor lists, which
a reconstruction never commits and never reads. Its sampling is not the actor layers'. A
compressed-model actor is sampled by rebuilding its pose from two keyframe streams, while a
shaded-queue record carries its mesh already decoded, so the interval is between two transforms over
one vertex corpus and is applied inside the recipe, in view space, before projection. Only the
pairing is shared, which is why `instance_pairing` was extracted from `actor_pairing` first: the
occurrence-ordered walk over guest instances does not depend on what a record contains, so the
identity rule and the sampler arrive as arguments.

Measured on 2026-09-19 over the same 7,200-field Artisans replay, with the same three verdicts
passing: the looks-right fps60 leg reports 2,155,839 interpolated prims across the same 3,374 extra
presents, against 2,141,330 before. The FIELD composition runs on 430 of 3,382 logic frames and
refuses none; the shaded interval is admitted on 429 of them. Across 2,145 reconstructions — three
admission samples and two presented ones per admitted interval — 9,250 of 9,260 records carry a
sampled transform, 10 have no predecessor, and the identity rule rejected none. Zero incompatible
against a denominator of 9,260 is a rule that ran and matched everything on this route, not one that
was never reached: the recipe's own `sampled` counter independently reports the same 9,250, and its
`sampleDeclined` counter reports no record whose interval the projection refused. The layer
published 72,557 in-between faces. The 485-comparison per-frame Artisans oracle run still matches
the full-console reference on every decisive range with zero divergences, so the logic-frame route is
unchanged. The gain over the secondary layer's landing is small because this route draws few shaded
sprites; prim counts and admission rates prove the path is exercised, not that the motion is
smooth.

`fx_field_shaded_queue.cpp` was removed in the same change, for the same reason `fx_secondary_actor`
was: a standalone producer for this layer with no caller anywhere in the repository. The live owner
is `fx_field_actor_composition`, because the secondary and world-shaded layers share one guest
shadow-list transaction.

The terrain producer (0x8004EBA8) was split into owners on 2026-09-19, ahead of giving it a temporal
source. It draws 90% of every item still replayed verbatim in an in-between present, and no second
picture of it could be built while the deriving code read guest memory as it went: at present time
that memory already describes the next game update. `terrain_scene` now captures one game update's
corpus — the object list the selector resolves, each visible object's decoded model vertices, its
face table and its colour words — `terrain_recipe` derives faces from that corpus purely,
`terrain_submitter` plans and publishes them, and `terrain_emit` is the one derive/preflight/publish
route. `native_terrain.cpp` is the guest entry points and one log line, 76 lines against 424.

Three duplicated implementations went with it. The producer carried its own copy of the framework's
fixed-point affine transform and its 44-bit wrap, and reached the framework's projection only
through an adapter that read the GTE control registers back out, so the transform it drew with was
whatever was last written rather than a value it held. It no longer writes the GTE at all: it used
to load two matrices into CR0 through CR7 and a widened screen centre into CR24, then restore all
thirty-two data and control registers on the way out. The projection plane the render queue
normalises stored depth against used to be a side effect of whichever vertex happened to be
projected last; it is now installed explicitly for the length of the submission and restored after.

The split is proved bit-identical rather than argued to be. Over the same 7,200-field Artisans
replay the captured PNGs at present 3,000 are byte-for-byte equal to the pre-split build's in all
three legs — 4:3, 16:9 and fps60 — and the fps60 leg reports the same 2,155,839 interpolated prims
over the same 3,374 extra presents. Ten focused tests run 70 checks over the recipe; each was shown
to fire by disconnecting the interval from the projection, moving the face-table refusal ahead of
the clip test, dropping the flat-colour primitive tag and ignoring the primitive-pool budget, which
failed two, one, one and one test respectively.

One behaviour deliberately differs and is not a picture difference: the capture reads the face table
of every object that clears the visibility test, where retail read it only for objects that also
survived the whole-object clip test, which needs projection and so cannot run at capture time. The
extra reads are of in-bounds memory and their results are carried, not acted on — the two conditions
retail checks late, an out-of-RAM face table and an out-of-RAM colour word, travel as flags for the
recipe to refuse on at the point it reaches them. The cost of those reads is unmeasured, as is the
frame-time budget on any released host.

The terrain layer then gained its temporal source on 2026-09-19, and it is the largest single change
to what an in-between present contains. Its endpoint is the captured corpus; the pairing is
`instance_pairing` keyed on the guest object pointer; the identity rule is the mesh the interval
indexes, meaning the vertex corpus and the face list over it; and the route to the queue is
`terrain_emit`, which the game update takes as well. What is this layer's own is where its motion
lives. The guest loads its view matrix with a zero translation and bakes each object's world
position into its vertex coordinates, so a frame's camera motion appears in the rotation and in
every object's vertices at once. The interval samples both. Sampling only the matrix would hold the
world still while the camera turned.

Measured on 2026-09-19 over the same 7,200-field Artisans replay, with the reaches, widescreen and
fps60 verdicts all passing: the looks-right fps60 leg reports 3,607,931 interpolated prims across
the same 3,374 extra presents, against 2,155,839 before. By the per-producer census the
reconstructed share of captured items rose from 0.572 to 0.955, and items still replayed verbatim
fell from 1,482,522 to 341,856. The terrain interval is admitted on 2,608 of 3,382 game updates.
Across 13,040 emissions 162,710 of 163,270 objects carry a sampled transform and 7,260,356
in-between faces were published; 560 objects have no predecessor, the identity rule rejected none,
and the sampler refused none.

Zero incompatible against a denominator of 163,270 is a rule that ran and matched everything on this
route, not one that was never reached. Terrain object meshes are static, so a changed vertex or face
count would mean a different object reached through the same guest pointer — which this route never
produces. The rule is exercised in its own tests, where disconnecting it fails two of four. The
recipe's own `sampled` counter independently reports the same 162,710, and its `sampleDeclined`
counter reports no object whose interval the projection refused.

What is left replayed verbatim is no longer terrain. Of the remaining 341,856 items, 281,828 (82.4%)
are layer 3 with no producer attribution, which is the 2D and HUD layer; about 53,000 belong to five
guest-side producers in the 0x80057000-0x8005A000 range; and 1,572 are terrain itself, from the 774
game updates whose interval was not admitted. The 485-checkpoint per-frame Artisans oracle still
matches the full-console reference on all 6,305 decisive range comparisons with zero divergences, so
the game-update route is unchanged. Two adjacent in-between presents were inspected directly and
show terrain, sky and actors in register with coherent motion between them, but a still image and a
prim count do not prove temporal smoothness, and no frame-time budget has been measured on any
released host.

Which producer to reconstruct next was measured rather than assumed, and the answer was not the one
on the list. The fps60 presenter could say how much of a captured frame replayed verbatim and the
sequence dump could say in which layer, but neither could say whose, because several producers draw
into the world layer and one run spanned them all. Adding the painter object to that run's key
(psxport 4598acd6) made the remainder attributable. Over 1,577 extra presents of the same replay,
1,482,522 items replayed verbatim and 1,335,351 of them — 90.1% — came from the terrain producer
`0x8004EBA8` alone. The 2D layer accounts for another 9.8%. The producers this document had listed as
next — shadows, glow, sparkles, particles, tracers — do not appear in the verbatim total at all;
instrumented separately over the same route they draw about 32 faces per logic frame between them,
so all five together would have moved the reconstructed share by under one percent. Terrain is the
next source.

Three duplications were removed as part of the work rather than after it. The occurrence-ordered
pairing walk and the reason-split census are now `instance_pairing`, used by both actor layers and
the shaded layer. The derive/preflight/publish route is `field_shaded_queue_emit`, taken by the
logic frame and the reconstruction alike, so a reconstructed picture cannot differ from the logic
frame's for reasons nobody chose. And the inverted-draw-area predicate, which seven producers had
each spelled out, is now `spyro::draw_area::ready`.

`fx_secondary_actor.cpp` was removed in the same change. It was a standalone secondary producer with
no caller: the live owner has been `fx_field_actor_composition` since issue 0099, because the
secondary and world-shaded layers share one guest shadow-list transaction. A dead second
implementation of a producer is exactly the drift the one-owner rule exists to prevent.

The first compatibility rule was wrong, and the census is what found it. The draw record's header
word packs the keyframe blend factor beside the coordinate shift, so requiring the whole word to
match rejected 75,630 of 75,645 incompatible records — three quarters of every actor drawn — for
animating rather than for being a different model. Identity is now the model descriptor, the vertex
count, the header's top byte and the primitive-word count; every other field is per-endpoint state
the sampler already reads from the side it belongs to. A census that only counted "not
interpolated" could not have told that from a rule that never ran.

World endpoint capture and reconstruction now share an owned source boundary, with a separate
queue-only emission path; its preservation contracts are described in
[world-semantic-oracle](findings/world-semantic-oracle.md#owned-world-endpoint-source).
World sampling now reconstructs LQ/HQ geometry and refinement from paired raw source transforms.
Per-game history rejects resource-generation changes, missing sources and incompatible camera/frame
provenance, and reconstructs both endpoints into the current draw destination. The six focused
source, refinement, producer and temporal tests pass, including 135 world-history checks and 903
joint temporal-scene checks. They exercise midpoint-only visibility through the actual presenter,
refusal of newly visible malformed sources, buffer flips and unchanged live rendering counters.

The first live Artisans test refused every world interval because complete material byte equality
treated scrolling UV values as resource identity. Consecutive captures isolated all 46 changed
bytes to the low/HQ texture-coordinate fields of material 23; palette, texture-page and attribute
fields were unchanged. Material pairing now permits only codec-defined UV state changes, using
the current discrete UV state with interpolated geometry. Refinement tables, overlapping non-UV
interpretations, resource spans and material identity remain strict. Source-pair tests pass 401
checks; the shipping scene sampler passes 283 checks including medium, near and direct material
paths, exact endpoints and identity-change negatives.

The corrected Artisans run reached field 3975 and then exercised 60 fields of held-left movement.
World and paired-model presentation were jointly admitted for 38/40 sampled intervals (frames
1931–1970); two world preflight refusals retained endpoint presentation.
The material animation continued changing during the run. At normal exit, 4,038 fields and 2,048
presentation fences executed 21,820,287 JIT blocks / 182,279,195 instructions with zero faults and
zero fallback; paired presentation emitted 2,074/2,074 midpoint/endpoint outputs. A native
684×240 capture shows the Artisans world and player during movement. This is local source-sampling
evidence, not matched-checkpoint oracle parity, historical VRAM reconstruction or full-scene cadence.
The combined Clang/Ninja gate passes all 34 CTest tests, 160 translation units through clang-tidy,
276 source/header formatting checks, and the exact framework pin `161cb132`.

A follow-up held-left observation with sample/queue diagnostics reproduced two refusals at frames
1962 and 1966: the exact previous endpoints and joint painter plans were valid, while midpoint
recipes reported `ActiveAnimation` / `active_animation`. The sampled visibility guard requires
animation state that was not advanced at an endpoint. The diagnostic also reports successful sample
and painter denominators; its existing synthetic tests distinguish valid output from malformed or
unadvanced midpoint sources. Both focused temporal tests and the two touched TUs' clang-tidy pass.

The first measurement of what the interpolated present actually LOOKS like in gameplay was taken
2026-09-19, through `tools/drive.py gameplay --hold left` rather than the recorded pad, because that
pad never leaves the save-file dialog (issue 0116). 238 real/interp/real triples at 16:9 with fps60
on, 684x240, 16-pixel tiles, captured with `PSXPORT_DEBUG=fps60dump,fps60seq` in one run so the
frames and the owner log share their fences:

| verdict | tiles | share |
|---|---|---|
| STATIC | 23,616 | 15.4% |
| BETWEEN (lerped) | 123,279 | 80.3% |
| STALE | 132 | 0.1% |
| AHEAD | 6,483 | 4.2% |

Endpoint tiles split by whether their content actually translated between the two real frames: of
the 132 STALE, 118 did not move and 14 did; of the 6,483 AHEAD, 866 did not move and **5,617 did**.
So the picture is overwhelmingly interpolated, and what fails does so by snapping FORWARD — 4,918 of
the AHEAD tiles are a clean 2-pixel translation.

That last number is the shape of the defect. A tile that takes the newer frame is what an
unblendable state change looks like (an animation frame flip, a newly visible object), which is
correct output; a clean 2-pixel translation is not that.

**It is NOT attributed, and the tables that said it was are withdrawn.** Tile attribution credits a
moving tile to the smallest `fps60seq` run covering it, which requires run extents and captured
pixels to describe the same place. They never did: a run's extent comes from `RqItem` screen
vertices in the renderer's own space, and the capture is the VRAM display region. Spyro's widest run
spans 2,048 px against a 684 px frame.

The 57.5% -> 100.0% coverage improvement previously recorded here was not a fix. Coverage cannot
fail, because every frame holds a screen-sized fill and a misaligned fill still covers every tile,
so it takes whatever nothing else claims. The discriminator that does fail is whether being inside a
run predicts motion at all, with fills excluded: on this route a tile inside a specific run is
**1.12x** as likely to have moved as a tile inside none, over 3,222 fences. That is chance, and the
display-origin and raw mappings score identically, so the origin correction only changed which
misaligned run won. The ownership and layer splits that stood here are therefore withdrawn, as is
the claim that 99.7% of reconstructed-run snaps had verbatim content over them. `fps60_check.py`
now refuses to print that table (psxport 6a019811, psxport issue 0120).

**The forward snap is explained, and it is not an interpolation defect.** Forcing the interpolation
factor to its previous endpoint (`PSXPORT_FPS60_TFORCE=0`) over the same deterministic 478-frame
route separates the two populations completely:

| | moved to the PREVIOUS endpoint | moved to the NEXT endpoint |
|---|---|---|
| t = 0.5 (product) | 14 | 5,617 |
| t = 0.0 (forced) | 105,850 | 5,542 |

105,836 tiles moved when `t` moved. The forward-snapping population did not: 5,617 against 5,542, a
1.3% difference over two runs of the same deterministic route. Content that does not respond to the
interpolation factor is not being interpolated at all. This comparison is between two captured image
sets and consults no run, so the withdrawn attribution above does not reach it.

**2026-09-20: the forward snap is EXPLAINED, and it is correct output.** It needed no attribution
machinery at all, which is why it survived psxport issue 0120 being open.

Two measurements, both on `tools/drive.py gameplay --hold left --hold-frames 478` at 4:3 with
fps60 on. First, the same captured images the withdrawn tables came from, classified per PIXEL
instead of per tile, at a forced interpolation factor rather than by tile identity. For every pixel
whose two real endpoints differ, force `t=0` and ask where it went:

| where the pixel sits when forced to t=0 | samples | share |
|---|---|---|
| at the PREVIOUS endpoint — it responded to t, so it is interpolated | 5,117,903 | 99.9% |
| at the NEXT endpoint — it did not respond to t at all | 1,781 | 0.03% |
| neither (partial coverage, blending) | 2,170 | 0.04% |

The control is that all 518 real frames are byte-identical between the two runs, so `t` reaches
only the in-between present and the route did not drift. 81 triples, 512x240.

The 0.03% is not spread over the screen: 83% of it falls in one row band and 70% in one column
band, a single compact region. At the worst triple it is one on-screen actor and the object beside
it, pixel-identical at `t=0.5` and `t=0` and identical to `real(N)`, while the terrain around them
is midway at `t=0.5` and exactly at `real(N-1)` at `t=0`.

Second, the pairing census says which rule rejected them — after being taught to distinguish two
failures it had been merging. `instance_pairing::Census` counted one `unpaired` for both "the
producer gave this draw no instance" and "the previous frame did not draw that instance", which
have different owners and different fixes. Split, over the same route:

| layer | emits | unattributed | instances ever absent | absent-run length |
|---|---|---|---|---|
| actor | 12,435 | **0** | 51 | 96 runs, all 1.0 logic frame |
| terrain | 12,435 | **0** | 77 | 126 runs, all 1.0 logic frame |
| field-shaded queue | 1,363 | **0** | 17 | 31 runs of 1.0, 2 partial intervals |
| secondary actor | 742 | **0** | 0 | — |

`unattributed = 0` everywhere: every draw is attributed to a Moby instance, so no producer is
failing to identify what it drew. And **no instance is ever absent on two consecutive frames** —
every absent-run is exactly one logic frame, the frame that instance first appears. An object the
previous frame did not draw has no predecessor to interpolate from, so drawing it at its own
endpoint is the only available output, and it pairs normally from its second frame onward.

So the forward-snapping population is objects entering the scene, not content that failed to
interpolate. The earlier reading — that it was unreconstructed content with no native producer —
does not hold for this route: such content would not respond to `t` on EVERY frame it is visible,
and nothing here is absent twice in a row. It remains the right description of a layer with no
temporal source at all, which this census cannot see because such content never reaches it; what
the pixel measurement adds is that on this route that content is at most 0.03% of changed pixels.

**Re-measured at 16:9 through the shipping tool (2026-09-22).** The pixel measurement above was an
ad-hoc script; it now lives in `external/psxport/tools/port/fps60_check.py --forced`, its one home,
with 41 selftest checks and every branch failing under mutation. Re-driven at `aspect=1` on the same
route, 81 triples, 684x240, all 518 real frames byte-identical between the two runs:

| where the pixel sits when forced to t=0 | samples | share |
|---|---|---|
| at the PREVIOUS endpoint — interpolated | 6,735,009 | 99.94% |
| at the NEXT endpoint — did not respond to t | 1,824 | 0.03% |
| neither | 2,186 | 0.03% |

So widening the picture does not widen the defect: the extra 172 columns interpolate like the rest.
Per triple, **all 81 are at or under 1% unresponsive**, the median is 0.00%, and the worst is 0.72%
at fence 447 — there is no wholly unresponsive triple anywhere on this route. The residue stays
compact: 81.4% of it in one row band, 86.6% in one column band.

Scope: one route, one area, 81 triples of images at each of 4:3 and 16:9, and 2,489 intervals of
census at 4:3. A 5-frame absence in some other scene would read differently, and the check that
would find it is the run-length above rather than another capture of the same walk. The same tool on
Tomba! 2 reads 96.35% on gameplay and finds a wholly unresponsive opening cutscene (that repo's
issue 0021), so this result is Spyro's, not the framework's.

The mechanism is `Fps60::presentPass`. Both presents of a fence run over the *same* captured queue
and differ only in `t`; only the items the scene source `owns` are replaced by reconstructed ones,
and everything else keeps its exact captured values — which are the current game update's. So an
item with no native producer is drawn in the in-between present at the position the NEXT real frame
will show it, a whole frame early. That is the 2-pixel translation: it is this route's per-update
camera motion, delivered at the wrong time.

Which content that is remains unanswered, because naming it needs the attribution that has just
been withdrawn. What is known without it: the defect is content the in-between present does not
reconstruct, the per-producer item census still reports 0.955 of captured items reconstructed on
this route, and the remaining verbatim items are 82.4% the unattributed 2D and HUD layer. Whether
those are the same items as the 5,617 snapping tiles cannot be stated until psxport issue 0120 makes
run extents and captured pixels comparable.

Gap: newly visible animated sectors need a faithful endpoint-state lifecycle; regular actors, shadows,
particles and other unowned temporal sources lack complete matching-source interpolation. Closing the
forward snap means reconstructing more of what the in-between present replays, not adjusting
interpolation; which producer to start with needs psxport issue 0120 resolved first. The looks-right verdicts have been re-taken on gameplay. They were previously run through
`replays/gameplay/artisans-arrival.pad`, which never leaves the save-file dialog (issue 0116);
`external/psxport/tools/port/looks_right.py` now takes `--route`, a command template the title fills
in, and Spyro's is `tools/drive.py gameplay --hold left --hold-frames 180`. On that route all three
verdicts pass: reaches with no failure mark, widescreen differing from 4:3, and fps60 reporting
3,125,946 interpolated prims over 3,105 extra presents (psxport b1ed4202). The 4:3 and 16:9 captures
show Artisans with Spyro, the archway and a gem in them, and the 16:9 one shows world to the left
and right that 4:3 does not — a wider view, not a stretched one. Looking at them is still the rest of
the check; no tool in this repository can say it looks right.

The oracle comparison is not affected by that and never was. `tools/oracle_compare.py` builds its
route from `tools/drive.py`'s observing Navigator rather than from a pad file, and arrives in
Artisans; its 485 checkpoints over 6,305 decisive range comparisons with zero divergences describe
gameplay. Issue 0116 reaches the looks-right verdicts and the frame-time budget only.

The endpoint lifecycle is now owned for visible world animation channels. When a previous source
contains a pending channel, temporal admission decodes that channel through the same pure animation
plan used by the frame producer, applies its writes to the retained endpoint arrays, retires only
the endpoint stamp, and records every animation table/header/keyframe/payload span. Hidden channels
still refuse when they become newly visible, and failed multi-channel materialization is atomic.
Pending-channel inputs are captured at the earlier frame's retain boundary without decoding hidden
geometry. Each span retains its image generation when owned and an exact SHA-256 content digest,
including for global animation-set pointers outside the image catalog. Midpoint preparation refuses
a replaced image, changed bytes, or changed source range before changing the retained endpoint.
Focused animation and temporal tests pass (68 and 162 checks), including image replacement,
same-generation content mutation, and newly acquired image ownership negatives. A fresh Artisans
route reached 3,161 post-entry fields
with 2,386 admitted world intervals and zero `active_animation`, animation-resource, or
endpoint-materialization refusals; the shipping JIT executed with zero fallback. This is live route
and source-oracle-path evidence, not full retail packet parity.

Gap: regular actors, shadows, particles and other unowned temporal sources lack complete matching-
source interpolation, and full-scene independent-console oracle parity remains open.
Discontinuous paired-model intervals retain endpoint presentation. Full-scene 60fps motion,
paced timing/audio and performance remain unqualified.
Issue 0102 resolves the observed duplicate field tick; it does not qualify full-scene cadence parity.

### S021 — SVG touch-control interface

**Status: missing.** Touch-enabled releases require an authored SVG overlay, reachable multi-touch
layout, safe-area handling, input cancellation, and controller-aware hiding or configuration. Touch
controls must use the same title action policy as physical controllers. No integrated or qualified
Spyro touch overlay exists yet; Android runtime mechanics belong to Lucent and build/device mechanics
to shared/android-port.

Related goal: G004.

### S022 — Spyro 1 music (XA streaming)

**Status: partial.** Music now streams. Spyro's sound driver sets mode `0xC8` plus a file 1/channel 4
filter and issues `ReadS` **without** a `Setloc` — it scans forward and polls `GetlocL` — and the
shared XA streamer treated a foreign (other file/channel) EOF as the end of that open-ended stream,
which happened seventeen sectors in: `xa_sectors=0`, `xa_wr=0`, `xa_pulls=1` for 3,500 fields while
the sink received a slowly-varying constant on both channels. psxport `379eafea` skips a foreign EOF
instead (a bounded clip still ends at `end_lba`, and only our own channel's EOF ends an open-ended
stream). The same windowless 3,500-field capture now decodes 959 XA sectors with a real waveform
(zero crossings 900–2400/s, RMS 1750–4134). Still open: the sink carries a ≈+1430 DC bias even while
the SPU is disabled (the CD-audio source is ruled out — `CDC_GetCDAudioSample` writes zeros when
nothing is streaming — so it enters in the SPU mix or the sink conversion; it is inaudible on a
device but it is why "non-silent PCM" cannot be used as an audio test); the intro cutscene's music
has not been compared against the console's own PCM;
nothing has been verified through a real audio device (headless `PSXPORT_WAV` captures only).

**What that scan actually costs, measured 2026-09-19.** "Scans forward" is not a figure of speech and
it is not free. The port keeps a SECOND disc cursor: the data path tracks the head in `cd.sec_lba`
while the XA stream keeps its own `s_lba`, which starts at 0. On the `tools/drive.py gameplay` route
the stream started at LBA 0 and the scan reached LBA 53,874 — about 122 MB and 7,400 CHD hunk
decompressions — **inside a single 1/44100 s output sample**, because the pull loop re-enters
`xa_decode_next_sector` as soon as its 64-sector guard expires. That was the three-second stall
recorded as [issue 0115](issues/0115-the-first-gameplay-frame-stalls-three-seconds-in-cd-audio.md).

**Resolved 2026-09-19.** The open RE question — "no `Setloc` is logged before the read at all, so
where does the driver believe the head is?" — had a third answer neither hypothesis covered: libcd's
`CdControl` (`0x80063EAC`) consults a per-command table (`0x80074DAC`) and sends the Setloc ITSELF
for `0x03`, `ReadN`, `SeekL`, `SeekP` and `ReadS`. Spyro's sound driver at `0x800568D0` does
`CdIntToPos(lba, &loc); CdControl(CdlReadS, &loc, 0)` and needs no Setloc of its own. The framework
override that replaced `CdControl` read only the command byte, so the position was thrown away.
Fixed in psxport `892e9550`, which applies the position through one owner shared by the explicit and
implicit Setloc; the stream now starts at the traced LBA 113,448 and the scan is gone. The
second-cursor observation stands as a design note — pacing head advance at the guest's declared
drive speed is still worth doing — but it is no longer needed to end the stall.

Related goal: G002.

### S023-S029 — Spyro 2 and Spyro 3

**Status: missing, and deliberately not started.** Goal G001 is three products, so these rows exist
to keep the inventory honest about what the repository does not yet do. Nothing below is evidence of
work in progress.

Both titles are held behind Spyro 1 by the single-title rule in `CLAUDE.md`: "Finish Spyro 1 before
continuing title-specific Spyro 2 or Spyro 3 implementation." Spyro 1 has not finished — S011
(representative gameplay conformance on each released host) is `missing`, every platform release row
is `missing`, and S020's in-between present still replays unreconstructed producers. Starting a
second title now would split the one maintainer across two incomplete ports.

What exists for each is only binary facts, and they are recorded rather than verified by execution:

| | identity | entry | game main | libetc VSync | measured boundary |
|---|---|---|---|---|---|
| Spyro 2 | `SCUS_944.25` | `0x8005478C` | `0x80011ADC` | `0x80058EDC` | three black display fields, then stops at `0x80011B1C` (S006) |
| Spyro 3 | `SCUS_944.67` | `0x80059444` | `0x8001200C` | `0x8005956C` | none — disc provenance and product execution are both unverified |

Spyro 3 is the weaker of the two: its addresses come from executable analysis, and no run of any kind
has been recorded against it. S022 in this document was a detail section with no row in the table
until 2026-09-19, and Spyro 3 had neither; both are inventory defects rather than lost work.

The widescreen and 60fps rows are listed separately per title because they are separately observable
against the baseline and will not come for free from Spyro 1. Spyro 1's interpolation is built out of
title-owned producers that read Spyro 1's game state — the terrain, actor and shaded-sprite sources
named in S020 — and none of them transfers to another title's scene layout. What does transfer is the
framework: the temporal presenter, the pairing walk, the projection stream and the measurement tools.

Related goals: G001, G002, G003.
