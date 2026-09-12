# Project state

Factual capability coverage for Spyro 1 native/Lightrec execution and presentation. Atomic work lives in
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

## Current focus

S011 — compare the reached Artisans gameplay against the independent console oracle, including
the remaining camera-state difference, and extend source-based world/camera interpolation. Boot/title and a visible player do not establish full conformance.

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

The post-entry shadow boundary is now exercised on the same real portal route. After rebuilding the
native target, `tools/drive.py gameplay --gate-teleport 0:0 --seek-portal --skip-transitions
--after 1200` reached the destination level and exited 0; the field shadow producer reported 16
faces on each sampled frame and Lightrec reported zero fallback blocks and instructions. Enabling
the actor semantic oracle on a 300-field route compared 440 frames: 383 retail primitives and 403
native primitives yielded 380 matches after the measured -86-pixel presentation offset, with three
retail-only and 23 native-only primitives. This is a concrete comparison discriminator, not full
scene parity; the remaining actor/depth differences and camera mismatch keep S011 missing.

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

Gap: complete scene variants, horizontal culling owners, and same-state oracle visual comparison
remain unqualified. Additional coverage in Artisans does not prove the whole game.

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

Gap: newly visible animated sectors need a faithful endpoint-state lifecycle; regular actors, shadows,
particles and other unowned temporal sources lack complete matching-source interpolation.

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
