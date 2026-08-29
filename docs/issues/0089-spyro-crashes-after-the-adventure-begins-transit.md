---
id: 89
title: Spyro crashes after THE ADVENTURE BEGINS transition
status: investigating
symptom: Current Spyro 1 product renders the THE ADVENTURE BEGINS... card and then crashes before gameplay
tags: spyro1,user-reported,crash,new-game,stage14,boot-to-play
created: 2026-08-27
updated: 2026-08-28
---

User-observed on 2026-08-27 with attached product screenshot showing a correctly rendered THE ADVENTURE BEGINS... transition card. The previous C228 evidence ended the agent run manually after early stage-14 presentation, so it did not cover completion of the cutscene-to-gameplay handoff; C228 is falsified. Reproduce from the shipping default route, capture the exact fatal PC/RA/stack and last stage/substate transition, then fix the binary-grounded owner. Do not treat early stage-14 rendering or a bounded green exit as success; the falsifier is entering controllable gameplay after the card without guest VSync or a crash.

## Measured failure boundary

The 2026-08-27 shipping-route witness reached the post-card Artisans load, then failed in the
handwritten collision routine at `0x8004D5EC` on an unmapped read from `0x8C428A90`. Its
`r30=0x8C428A84` was consumed while traversing the collision list associated with Artisans moby 29.
The routine intentionally uses `r29/r30/r31` as working registers and saves the guest ABI at
`0x80077DD8`; the apparent stack/register damage is therefore a consequence of following the bad
collision pointer, not evidence that the native renderer failed to preserve the caller ABI. The
exact guest word supplying that pointer was not present in the fatal register dump.

The on-disc Artisans scene and an older playable Artisans RAM image contain zero at the corresponding
collision-root region. The loaded actor bytes themselves agree with that older image, while the bad
root lies near the end of the scene payload. This made an incomplete scene copy a concrete hypothesis:
the actor could be present while the collision roots remained stale.

## Falsified: incomplete Artisans scene transfer

PID 3558798 reproduced the shipping New Game route with sequential archive-copy coverage measured at
the production CD stream owner. At fields 7051-7053 the Artisans scene read was:

```text
dest=0x8016313C len=83968 coverage=[0,83968) complete=1
```

`83968` is exactly `0x14800`, so the transfer covered the collision-root tail. The run ended cleanly
through the REPL immediately after this verdict; it was not advanced to the known later crash. A
short archive copy is therefore not the cause of this issue. The independent API defect that a
different short copy could still be acknowledged as successful is tracked as issue #91.

The next investigation tested whether one selected grid-root word changed after the complete
transfer. The fatal retained `t0=0xEA`, the routine's selected grid index. With the then-observed
`[0x80075778]=0x80175014` and the routine's `+0x1000` root-table displacement, the primary grid word
was `0x801763BC`. The archive word at scene relative `0x13280` (WAD byte offset `0x9DD280`) was zero,
and `tools/writers.py 0x801763BC` found no immediate-form stores. Trusted instrument I016 therefore
watched `[0x801763BC,0x801763C0)` for a computed-pointer writer after transfer. Instrument I030's
reported guest PC remained the last function entered rather than the exact store instruction.

## Current product boundary after the collision witness

The address-scoped PID 3569153 witness copied zero from the Artisans archive into
`[0x801763BC,0x801763C0)` and then passed field 7200 without a later nonzero write or the earlier
collision fatal. It ended cleanly at frame 7631, before the counter-384 transition later shown to
trigger the fatal. It therefore proves only that this word stayed zero up to that early endpoint;
it says nothing about the later producer writes at the failure boundary and never showed the
collision issue was fixed.

PID 3574262 captured the native product at field 7200 and ended cleanly. The 512x240 VK readback was
completely black while the state remained stage `13/3/2`, load phase 13. No guest VSync, watchdog, or
fatal occurred. Static control flow then narrowed what that snapshot means: phase 13 is the final
entry of `func_80015370`'s 0..13 table; there is no retail phase 14. `func_80032B08` at `0x800330EC`
stops pumping `0x80015370` once phase 13 is reached, increments `[0x80078D80]`, and deliberately waits
until that counter reaches 384. Only then does `0x80033158` call `0x8004AC24(1)` and
`0x80015370(1)`; phase 13 calls `0x8001364C`, whose `0x80013B4C` store changes stage 13 to stage 0
(stage 12 is the only exception).

The field-7200 black frame was therefore captured during the measured 384-count hold, not after its
retail exit predicate. The next bounded discriminator is the transition at counter 384: stage must
become 0 and the product must show coherent, controllable Artisans gameplay. Until that happens,
issue #89 remains open even though the earlier collision fatal did not recur.

## Regular shadow-list feeder is now staged (2026-08-29)

The native regular actor owner now prepares the shadow side effect from the same source-backed
`0x8001F158` culling pass that builds its actor records. It resets the staged cursor to the retail
list start `0x800724F4`, derives the descriptor-relative model byte at `+0x2A + moby[0x3E]*8`,
and commits the entries plus `0x80075F00` only after actor queue admission succeeds. A focused test
proves that a prepared empty frame publishes the fixed list start and does not mutate guest state
during preparation.

The bounded real route built and presented 1,200 native frames with no refusal or fatal. Its live
regular actor frames reported `shadows=0`, so this is feeder ownership and ordering evidence, not
proof that the shadow renderer is visible. The Moby shadow packet consumer `0x80059F8C`, Spyro's
shadow `0x80059A48`, and the flame/glow/sparkle arms remain separate unowned renderers.

## Counter-384 transition reproduces the collision fatal

PID 3581368 reached `[0x80078D80]=384` with stage 13, then the next two-field step wrote stage 0 and
immediately reproduced the original collision failure before a post-transition frame could be
captured. The fatal again followed `r30=0x8C428A84` and attempted `read32(0x8C428A90)`. This proves
the retail hold and stage-13-to-0 predicate complete correctly; the current failure is the collision
initialization performed by `0x8001364C` after that store.

The earlier `0x801763BC` attribution was wrong. Disassembly of `0x8004D5EC..0x8004DB30` shows that
`0x8004D684`, `0x8004D6B8`, `0x8004D6EC`, and `0x8004D718` compact as many as four nonzero grid
roots into scratchpad. The traversal then loads a root at `0x8004DB0C`, but every iteration executes
`0x8004DB1C: lw fp,4(fp)`. Thus the bad `r30/fp` can come from a linked collision node's `next` word,
not directly from the grid root. `ra=0x1F800004` proves only that traversal is still within the first
scratch-root list; it does not prove that the first root word itself was bad. The prior watch covered
one guessed grid word and could not observe a corrupt link field.

`PSXPORT_COLLISION_CHAIN_PROBE=1` now wraps the already-existing `0x8004D730` entry without changing
the generated collision behavior. At that boundary the routine has selected and compacted its roots
but has not started traversal. The probe walks precisely those retained `node+4` links and reports
the first invalid value together with the exact guest source word. It is gated to stage 0 with the
transition counter at least 384, so the next authorized witness answers one question: which root slot
or node `next` field supplies `0x8C428A84` at the reproduced handoff.

PID 3601402 measured the failing call as grid index `0xEA`, one compacted scratch root, and
`[0x1F800000]=0x00010FF0`. Main RAM's low physical alias is valid, so this root is followed rather
than rejected. The exact retained-executable words then complete the load chain:

```text
0x8004DB0C reads scratch root [0x1F800000] = 0x00010FF0
0x8004DB1C reads [0x00010FF0 + 4]        = 0x800427BC
0x8004DB1C reads [0x800427BC + 4]        = 0x8C428A84
0x8004DB28 reads [0x8C428A84 + 12]       -> fatal at 0x8C428A90
```

The exact guest word supplying the invalid node pointer is therefore `0x800427C0`. It is resident
executable code whose instruction word happens to be `0x8C428A84`; collision traversal reaches it
only because the sole selected grid root is the invalid code-region pointer `0x00010FF0`. With
`table=0x80176014`, base index `0xEA`, and both neighbor deltas positive for the measured coordinates,
that root came from exactly one of `0x801763BC`, `0x801763C0`, `0x8017643C`, or `0x80176440`.

## Collision-grid producer ownership

All four source words are zero in the transferred Artisans payload, so the bad value is installed
after the complete CD copy. The retained collision owner has three computed-pointer mutation paths:
`0x800526A8` inserts a Moby at the head of its position-derived grid cell, `0x80052568` unlinks a
Moby and replaces its slot/link with `m_CollisionChainNext`, and `0x800529E4` moves an existing Moby
between cells. The generated bodies agree with the handwritten executable for these stores.

The Artisans Moby table further narrows the cell history. Mobys 9, 10, and 29 have primary cell
`0xEA`; the failing query occurs immediately after the loader reaches Moby 29 at `0x8016DDE0`.
The corrupt value `0x00010FF0` also equals Moby 32's on-disc relative props offset, but that equality
does not identify a writer by itself. Static code cannot determine which of the four runtime words
changed or whether the value was inserted directly versus propagated through an unlink.

PID 3619224 reran that first writer probe and reproduced the same stage-13-to-0 fatal, but recorded
no writer mutation. The stage-0 store occurred immediately before the consumer verdict, proving the
grid had already been populated while stage 13 was still visible. This falsifies stage 0 as a valid
producer lifetime gate; it does not falsify the three retained mutation owners.

The revised issue-89 probe has no phase guess. It identifies this exact Artisans instance through
both measured owners (`g_MobyCollisionChain=0x80175014` and `g_LevelMobys=0x8016D3E8`), then records
only changes to the four exact candidate cells. A bounded 64-entry ring retains writer, caller,
old/new value, Moby argument, pre-call `next`, move flags, and whether the new root came from the
object or its `next` word. The consumer dumps both the four live words and the ordered history on
the first invalid chain. It does not alter the grid or reject the bad root.

## Falsified: one of the four candidate grid words contains the bad root

PID 3624551 reached the same exact Artisans consumer and invalid chain, but all four candidate live
words were zero:

```text
0x801763BC=0  0x801763C0=0  0x8017643C=0  0x80176440=0
GRID HISTORY retained=0 capacity=64 dropped=0
scratch [0x1F800000]=0x00010FF0
```

The bad scratch root therefore was not installed by any of the three collision-grid mutation
owners and was not loaded from the four source words inferred from the final register snapshot.
That inference was stale: the split selection helpers reuse `r4/r5/r9`, so their final values at
the `0x8004D730` consumer do not by themselves prove the addresses used by each earlier load.

The current diagnostic wraps only the exact measured query
`(x=0x15C00,y=0xFC00,z=0x2576)` while both Artisans owners match. It arms the runtime store watch for
scratch `[0x1F800000,0x1F800010)` before `0x8004D5EC`, then records every generated copy site's
instruction/function PC, scratch target and prior value, reconstructed source address and live
source value, written value, and immediate `r1/r2/r8/r9`. This is the next discriminator between a
wrong computed source, stale scratch, interrupt-time register mutation, and split-helper register
corruption. It remains diagnostic-only; no invalid root is rejected or skipped.

PID 3638208 recorded all four generated copy stores before reproducing the same fail-fast:

```text
primary   0x8004D690 expected 0x801763BC live=0 write=0
x-neighbor 0x8004D6C4 expected 0x801763C0 live=0 write=0
y-neighbor 0x8004D6F8 expected 0x8017643C live=0 write=0
diagonal  0x8004D724 expected 0x80176440 live=0 write=0x00010FF0
```

The first divergence is therefore inside the diagonal split helper, before its scratch store. The
watch callback runs from `Core::mem_w32` before the store and before `func_8004D730` can poll pending
work; it already observed the expected diagonal cell as zero. Consequently the D730 poll cannot be
the event that zeroed the expected source after the diagonal load. Stale scratch is also falsified:
the bad value is actively written by `0x8004D724`.

## Root cause: a false fragment entry executes a branch delay slot twice

The recompiler output settles the actual load address without another product run. In the retail
code, `0x8004D70C` is a conditional branch and `0x8004D710: sll t1,t1,2` is its delay slot. Generated
`gen_func_8004D6E0` faithfully executes that shift inline, but then calls `func_8004D710` on the
fallthrough path. Generated `gen_func_8004D710` starts by executing the same shift again. The
correct diagonal index `0x10B` is therefore shifted to `0x42C` before helper entry and then shifted
again to byte offset `0x10B0`. With table base `0x80176014`, the wrong actual load is
`0x801770C4`, whose value is the observed `0x00010FF0`; the intended address is `0x80176440`.

`0x8004D710` did not come from Spyro's seed registry, pointer-table discovery, constructed function
pointers, code-pointer tables, overlay pointers, or direct-call discovery. Before cross-fragment
completion the containing span is correctly `0x8004D5EC..0x8004DF24`. The false entry is added in
cross-fragment round one from the permissive module-wide computed-jump recovery at `jr s1
0x8004D2D8`: that global scan reports 50 candidates while the owning local span reports five. The
framework comments already state that module-wide targets are graph edges rather than entry proof,
but `_uncovered` also promoted every global candidate directly.

The generic recompiler correction removes that direct promotion. Module-wide edges still feed the
reaching-constant analysis, and values actually proven at a computed jump still become dispatchable
entries. Focused emitter regressions prove both sides: an unproven global candidate landing on a
branch delay slot remains inside its containing body, while the established flow-derived fragment
fixture remains emitted. The Spyro substrate must be regenerated and the shipping New Game route
must reach coherent, controllable Artisans gameplay before issue #89 can be resolved.

## Post-regeneration result and current acceptance boundary

The authoritative regeneration removed `func_8004D710` from generated dispatch and left the
`0x8004D70C` branch's delay slot owned exactly once by its containing `0x8004D5EC` body. The exact
Clang product then followed the real New Game route through `THE ADVENTURE BEGINS...`, changed stage
13 to stage 0, and did not reproduce the collision-chain fatal or guest-VSync trap. It instead
stopped at the shipping native-render refusal at `0x80016958`: stage 0 had no complete native scene
producer. Thus the false-entry collision cause is fixed, but this user-visible issue stays open until
the same route reaches coherent controllable gameplay rather than a deliberate renderer refusal.

The first field-scene units are now source-owned but remain deliberately unwired. The regular actor
chain `0x8001F158`/`0x8001F798` now selects the binary's direct Plain material pair at descriptor
offsets `+28/+32`; the captured stage-0 snapshot is Ready with 175 Mobys scanned, 14 records, 423
candidates, 211 rejects, and 212 faces. The separate secondary actor chain
`0x800208FC`/`0x80020F34` is Ready on that snapshot with 3 visited list members, one visible record,
138 candidates, and 75 faces after 63 rejects. NegativeBlend/global-far-colour and shadow ownership
remain outside those ready frame recipes. The stage-0 orchestrator remains fatal until the remaining
dynamic layers have equally complete owners. The separate world-sign class
of `0x80022A2C` now also has an unwired atomic owner. Its same snapshot distinguishes 93 queued
records, 52 valid meshes / 936 source primitives, and 3 visible records / 54 candidates; both mesh
IDs 83/84 and lighting offsets 8/16 are represented, and exact word-1 palette indices yield 23 native
Gouraud faces after 31 guest-equivalent rejects. This advances the field recipe but does not satisfy
the gameplay acceptance checkpoint.

The separate cyclorama wrapper `0x80050BD0` now owns inactive and projected-valid-empty portals
without executing guest rendering. The five Artisans records all have world-sector `-1`, but every
projected aperture has zero screen-crossing edges, so all five are valid-empty and the final
`0x8004EBA8` main-sky recipe is Ready. A genuinely visible portal still refuses until issue 0093's
mask/near-family/painter submission contract is owned.

## Session 2026-08-28: deterministic repro, intro skip, stage-0 branch, next dependency

The shipping route is now deterministically reproducible windowless: replaying the user's own
auto-recorded pad session (`scratch/bin/pad_session.1.pad`, captured 14:03) drives boot -> intro
card -> menu -> New Game -> stage-14 card -> the 384 hold -> the crash, ending rc=139 at the
render refusal. The same replay proves the intro skip (previous commit): the user's field-1137
Start edge lifted `g_TitlescreenState.m_Tick` 73 -> 384 and stage 14 began immediately.

The stage-0 branch is now registered (`renderScene`), composing the field-snapshot owners in
GamestateDraw's authored order, and the crash is a NAMED backlog: the branch refuses at the first
layer without a complete owner. That refusal fired one layer earlier than particles — the
environment owner refuses the live frame with `scene=world refused world=2 reason=active_animation`:
a visible Artisans sector has an active world-mesh animation channel (the per-channel 0x80
counter check in world_scene_prepare), which the recipe cannot represent. Owning animated world
chunks is the next dependency, then particles 0x800573C8, then tracers 0x800189F0; the screen
fade/border producers behind them are owned (border landed this session with its recipe test) and
wait only for the layers ahead of them.

Also fixed this session (user-reported "run.sh runs unbounded speed"): the frame_commit fence
paced ONE field per logic frame while the guest's own tail spends TWO (30 Hz logic on 60 Hz
display) — the game ran at 2x. `frame_commit` now paces `kFieldsPerLogicFrame` = 2, measured
windowless at 59.6 fields/s / 29.8 logic fps against retail 59.94/30.

## Animated world chunks are now owned; the refusal moved to particles

The environment layer's `active_animation` refusal is resolved by owning phase 1's per-sector
animation rather than by tolerating it. `RenderWorldChunks` does not only select and cull sectors:
for each one it keeps, it advances up to four channels that write back into that sector's own vertex
and colour arrays (`0x80025BAC..0x800261A0`), and phase 2 then projects exactly what they wrote. A
producer that skipped them would draw last-frame geometry; the one that refused was blind instead.
Both are wrong, so `game/render/world_animation.cpp` owns the step.

Each channel is gated by one of the sector's four stamp bytes at `+0x18`, ORed with the mask naming
which quality halves were emitted; it runs when its byte is below `0x80` and the guest retires it by
stamping `0xFF` back. The four differ only in destination, and each has a direct-copy and a
GTE-interpolated form:

| channel | set slot | destination | interpolated by |
|---|---|---|---|
| 0 | `g_EnvironmentAnimations+0x14` | `sector+28` | INTPL, packed 11/11/10 |
| 1 | `+0x1C` | `sector+28 + [sector+16]*4`, walked by each word's top byte | DPCS |
| 2 | `+0x24` | `sector+28 + [sector+23]*4` | INTPL, packed 11/11/10 |
| 3 | `+0x2C` | two streams from the `[sector+20]` layout word | DPCS |

The decode is pure and yields a plan; `world_scene::animate` is the single place that commits it,
and it re-walks the selection in the refusing form afterwards so a surviving channel is reported
rather than assumed away. The read-only recipe producers keep their contract unchanged.

### Verified against the retained body, not declared

The guest's animation retires itself, which gives an exact A/B with no address exclusion list: run
`gen_func_800258F0` twice from the same captured RAM — once as the guest (animate + render), once
after the native animation has retired the channels (render only). If the native animation is the
guest's, both legs leave byte-identical guest RAM. That comparison is instrument I057.

On `scratch/raw/stage0_artisans_refusal.bin` — the exact frame that was refusing, captured by the
new RAM dump on the render-refusal fatal — the two legs are identical across the whole 2 MB after 2
channels and 28 writes. The shipped negative control (`PSXPORT_WORLD_ANIMATION_ORACLE_MUTATE=1`)
flips one byte the animation itself wrote and the same comparison reports 80 differing bytes, so the
instrument is sensitive to what it claims to measure. C229 records the claim and its falsifier.

**Coverage gap, stated rather than papered over:** that frame's two channels are both the DIRECT
form. The live corpus exercises ZERO blended channels, so the INTPL/DPCS transcriptions are covered
only by `tests/test_world_animation.cpp` (46 hermetic checks across all four channels and both
forms). A real frame with a nonzero keyframe blend factor has not yet been compared.

### Current boundary

The stage-0 seam now composes the reached Artisans FIELD layers in authored order: collectables,
regular and secondary actors, world/shaded queue, environment, cyclorama, type-0 particles, fade,
border, and tracers. The post-framework replay `scratch/logs/spyro-replay-post-framework-field-20260828.log`
ran through the recorded interaction and continued to the 10,000-present cap with rc=0, 5,057
reconciled frames, zero dropped layers, and no native-render refusal; its producer DB records
18,951 type-0 particle lines. This proves route continuity,
not that the image matches the disc or that every FIELD variant is owned.

The remaining acceptance boundary is now visual/oracle comparison and the unowned variants: actor
shadows, nonzero world primitive variants, visible cyclorama portals, blended world animation,
non-type-0 particles, tracer variants, and the retained guest-renderer tail. A current paced audio
run is non-silent and duration-correct: 1,200 VBlanks span about 20.115 seconds, the WAV contains
882,882 samples with exact 735/736 NTSC field cadence, 239 selected file-1/channel-4 XA sectors
decode, and no ring-full event occurs. This proves product timing/routing but not speaker delivery or
independent-oracle PCM parity. Issue #89 stays open until the controlled route renders its visible
portal and the remaining fidelity boundaries are verified.

## Control milestone: digital movement is now live

The collision false-fragment fix and the particle-array ownership fix moved the route far enough to
test control instead of another portal or renderer hypothesis. The guest pad path is live: after the
New Game handoff, `g_ActivePad` contains the guest's active-high Left bit (`m_Held=0x8000`). The
recompiled `func_8003D3B8` nevertheless reads `m_Released`, which is zero during a held input, so its
digital target speed remains zero. That is the actual control defect; the pad transport itself is
not missing.

`game/core/native_gameplay.cpp` installs a runtime override for `0x8003D3B8`. It super-calls the
generated body, preserves its analog and release-edge branches, and supplies the source-defined
digital target from `m_Held`, the guest direction table at `0x8006C5D0`, and camera rotation. The
focused test passes. A real Clang product comparison over the same 3400-step route, using the
diagnostic-only `PSXPORT_GAMEPLAY_PROBE=1` field path, produced:

```text
idle: 0x80078A58 = 00014C00 0000B845 00002554
Left: 0x80078A58 = 00014C1B 0000B602 000025C3
```

The Left run also exposed the corrected nonzero target (`speed=0x400`, `rotation=0x800`) and the
pad/target state was inspected at the live gameplay boundary. The probe suppresses only the
incomplete picture producer while retaining the real input, logic, collision, and host field
scheduler; the normal native product path is unchanged. A speed audit then found that the probe was
delivering one field per logic iteration, unlike the retail two-field frame tail. The probe now
defers presentation on the first field and presents once on the second, preserving the one-fence
contract while matching the retail 30 Hz logic cadence. A Clang smoke run completed with 600
presents, 1,020 delivered fields, and 362 reconciled logic frames under the corrected path.

## Stage-0 continuity after actor and painter fixes (2026-08-29)

The empty regular-actor scene adapter now distinguishes a known-empty live frame from an absent
diagnostic corpus, so the reached FIELD route no longer refuses merely because that frame has no
regular actor records. Framework painter ownership also admits trailing ordinary world primitives
after authored painter groups; the old rule only admitted a trailing two-vertex line and rejected the
type-2 particle/tracer-shaped quad that the live route actually emits. The focused framework test
keeps the real refusal for an ordinary primitive before authored painter order and covers the valid
trailing case.

The final controlled gate run (`scratch/logs/gate-after-painter-fix.log`) teleported to the
source-backed gate-0 node and continued through the wired stage-0 sequence with exit code 0: 3,520
presented fields, 1,820 reconciled logic frames, zero dropped layers, and paired-actor temporal
proof of 223/223 eligible intervals, 223/223 midpoint and endpoint eligibility, and 446/446
emissions with zero empty output. The log contains no native-render refusal or fatal. This proves
controllable route continuity through the currently wired producers, including the visible portal
mask/near family and type-2 particles; it is not proof of complete actor coverage, visual parity,
or an independent reference-oracle match. The secondary-actor, shaded-queue, and Spyro-player
owners are now called by FIELD; Moby/Spyro shadow packets, flame/glow/sparkle effects, and full
visual/oracle parity remain open.

## Current acceptance boundary

Controllable player state and the reached native FIELD route are unlocked, but issue 0089 remains
open for actor shadow/effect families, variants, visual/oracle parity, and the remaining producer
coverage. Level-transition portal traversal remains separate from this
control milestone.

## Fresh control-path witness (2026-08-29)

The current pushed build (`4fde3df` against framework `e84865b4`) was rerun with the existing idle
and Left replays through the native FIELD route. Both runs exited 0 at the 4,255-present cap with
no native-render refusal or fatal. At replay frame 3000 the captured Spyro state was:

```text
idle: position=(0x14C00,0x0B800,0x023E0), target=(0,0x0400)
Left: position=(0x1497B,0x0B9CE,0x02514), target=(0x0109C,0x00995)
```

The captured shadow input block was live and enabled in both runs (`0x8007AA10+0x24 == 0`), so the
missing shadow picture is not explained by a disabled player-shadow state. The native FIELD
sequence still ends after the player model; `0x80059A48` is not called. The next implementation
step therefore requires translating or bridging that renderer's source-defined packet production,
not another movement or portal workaround.

## Controllable-action matrix (2026-08-29)

The replay masks were corrected before this matrix: `.pad` files carry PSX active-low bits, so the
framework mapping is `Up=0x0010`, `Right=0x0020`, `Down=0x0040`, `Left=0x0080`, `Start=0x0008`,
`Select=0x0001`, `Cross=0x4000`, `Circle=0x2000`, and `Square=0x8000`. Each action was held for
replay fields 3000--3007 on the same New Game route and capped at 3020 presented frames. The
seven gameplay-action runs (four directions plus Cross, Circle, and Square) all exited 0 with no
native-render refusal or fatal. The live RAM witnesses at replay field 3002/3004 were:

```text
Up:     m_Held=0x1000, m_Down=0x1000, target=(0x12C2,0x0400), position changes
Right:  m_Held=0x2000, target=(0x12C2,0x0000), position changes
Down:   m_Held=0x4000, target=(0x12C2,0x0C00), position changes
Left:   m_Held=0x8000, target=(0x12C2,0x0800), position changes
Cross:  m_State=5, then m_airTime=1 (jump path)
Square: m_State=0xB (charge path)
Circle: g_SpyroFlame+0x98=1 (flame-active path)
```

This proves the retained guest update and the one diagnosed native digital-movement correction
provide controllable gameplay; it does not claim that the unowned flame/shadow effect packets are
visible yet. Start (`0xFFF7`) and Select (`0xFFFE`) were also isolated. They reach the expected
pause (`stage=2`) and inventory (`stage=3`) handlers, then fail at the explicit native-render
refusal because those menu picture owners are not implemented. That is a separate menu-rendering
gap, not a reason to alter movement or pursue portal traversal.

### Tooling closed this session

The render-refusal fatal wrote no RAM dump, so every unowned stage-0 layer had to be re-driven live
to be looked at once. It now calls `snapshot_now` unconditionally on that path — the same reasoning
as the recomp-MISS dump — which is how this session's corpus was captured at all.

## Retained shadow consumer capture (2026-08-29)

The first FIELD frame of the source-backed gate route was run with the diagnostic-only
`PSXPORT_SHADOW_ORACLE=1` capture. It snapshots RAM, scratchpad, GTE, and CPU state, dispatches the
retained Moby-shadow consumer `0x80059F8C` and Spyro-shadow consumer `0x80059A48`, then restores that
state before the native FIELD picture continues. The retained consumers were not used as a shipping
fallback and the native renderer produced no shadow from this probe.

The measured packet ranges were:

```text
[shadoworacle] call 1 moby: packet range 0x80187BB0->0x80187BB0, packets=0 gate=0x00000000 shadow_cursor=0x800724F4
[shadoworacle] call 1 spyro: packet range 0x80187BB0->0x80187E30, packets=16 gate=0x00000000 shadow_cursor=0x800724F4
```

All 16 accepted Spyro packets were then logged. Every packet has the source's `E1000640` draw-mode
setup, `0x32608080` flat Gouraud colour, and a 0x28-byte slot. The packet's projected anchor is
`00A70064` in every slot; the packet points form a closed fan from `009A0064` through the sequence
below and back to the first point. The command word's low 24 bits are the next packet link for the
linked slots; `06000000` terminates the corresponding link.

| packet | guest base | command/link word | anchor SXY | point SXY | next point SXY |
|---:|---:|---:|---:|---:|---:|
| 0 | `80187BB0` | `06187BD8` | `00A70064` | `009A0064` | `009E0055` |
| 1 | `80187BD8` | `06187C00` | `00A70064` | `009E0055` | `00A3004C` |
| 2 | `80187C00` | `06187DB8` | `00A70064` | `00A3004C` | `00A40044` |
| 3 | `80187C28` | `06187C50` | `00A70064` | `00A40044` | `00A70040` |
| 4 | `80187C50` | `06187D68` | `00A70064` | `00A70040` | `00AC0037` |
| 5 | `80187C78` | `06187CA0` | `00A70064` | `00AC0037` | `00B30037` |
| 6 | `80187CA0` | `06187D40` | `00A70064` | `00B30037` | `00C60037` |
| 7 | `80187CC8` | `06187CF0` | `00A70064` | `00C60037` | `00DD0064` |
| 8 | `80187CF0` | `06000000` | `00A70064` | `00DD0064` | `00C70092` |
| 9 | `80187D18` | `06000000` | `00A70064` | `00C70092` | `00B50096` |
| 10 | `80187D40` | `06000000` | `00A70064` | `00B50096` | `00AC0094` |
| 11 | `80187D68` | `06187D90` | `00A70064` | `00AC0094` | `00A70089` |
| 12 | `80187D90` | `06000000` | `00A70064` | `00A70089` | `00A30084` |
| 13 | `80187DB8` | `06187DE0` | `00A70064` | `00A30084` | `00A0007C` |
| 14 | `80187DE0` | `06187E08` | `00A70064` | `00A0007C` | `009D0072` |
| 15 | `80187E08` | `06000000` | `00A70064` | `009D0072` | `009A0064` |

The same capture also recovered the source's depth provenance. The generated body performs the anchor
projection at `0x80059ADC`; the observer saw `anchor SXY=00A70064`, `anchor SZ=065E`, and 25 GTE
operations. Its scratch table contains the 16 fan-point `(SXY,SZ)` pairs below:

```text
009A0064/0742  009E0055/0713  00A3004C/06D6  00A40044/069E
00A70040/065E  00AC0037/060D  00B30037/05A5  00C60037/04DD
00DD0064/0423  00C70092/04CF  00B50096/058F  00AC0094/0604
00A70089/065E  00A30084/069E  00A0007C/06D6  009D0072/0714
```

With the retained formula
`(((SZ[a]+SZ[b]) + ((SZ[a]+SZ[b]) >> 1) + anchor_SZ) >> 9) - OT_bias`,
and `OT_bias=3`, the packet buckets are, in packet order,
`10,10,10,9,9,8,8,6,6,7,8,9,9,10,10,10`; all 16 are non-negative and therefore admitted.
The diagnostic now records these values without changing the native picture. It still does not walk
the final guest ordering-table links to prove their replay order. The probe run later reached the
existing explicit pause-menu native-render refusal, so its exit code is not evidence about shadow
capture; the packet ranges were recorded before that unrelated refusal. A guessed ellipse or a portal
workaround would not be source-grounded.
