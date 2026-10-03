# 0154 — Widescreen must override every owned horizontal limit

**Status:** implemented and measured. One error in issue 0152 is corrected here; see
"Correction to 0152" below, which is the most important thing in this file.

## The rule

Widescreen is a **pure projection change**. It may change what the port *draws* and nothing else:

- Every horizontal cull and every screen-rect limit **the title owns** is overridden, so the
  margins show what the widened view would.
- Every byte the **guest reads** keeps retail's 4:3 answer. The projection is not gameplay state in
  Spyro, but `D_800771C8`, the Moby `+0x51` bytes and the Moby shadow list are guest state, and the
  guest's own animation walk depends on them.

Both halves run through **one** rule, `spyro::wide` in `game/core/wide_clip_plan.h`:

    viewHorizontalInside(extent, depth, width)  // 4*512*extent < 3*width*depth
    drawnHorizontalInside(extent, depth, width) // native answer OR the widened answer

`drawn` OR-s the native answer in deliberately: scaling a negative depth term makes the plane
stricter, not wider, so a near-eye bound the native plane admitted would be lost. The drawn set is
therefore a superset of the guest's by construction, and widening can never remove what 4:3 showed.

The drawn right edge itself is one accessor, `wide_screen_space::drawClipRight(Core*)`, replacing
four open-coded `gpu_vk_wide_engine(core) ? gpu_vk_wide_engine_w(core) : 512` expressions.

## The audit

Every horizontal cull or screen-rect limit this title owns, with where it lives and its status.
"Already widened" means an earlier change did it; "widened now" means this change did it.

| # | Owner | Guest citation | Site | Status |
|---|---|---|---|---|
| 1 | World sector horizontal plane (`broadCull`) | `0x800258F0` | `world_scene_prepare.cpp:28` | already widened (0152) |
| 2 | World face clip right edge | `0x8002651C`/`0x80026544`/`0x8002654C` | `world_recipe.h` `clipCode`, `world_lq_recipe.cpp:22` | already widened — the caller passes `frame_rendererWidth(Core*)` as `clipRight` |
| 3 | Moby plane, regular pass | `0x8001F158` | `actor_scene_builder.cpp:149` `classify_view` | already widened (0152) |
| 4 | Moby plane, secondary pass | `0x800208FC` | `secondary_actor_scene.cpp:40` `build_source_record` | already widened (0152) |
| 5 | Moby shadow staging depth, regular + secondary | `0x8001F34C`, `0x80020AF8` | `actor_scene::kShadowStagingDepth` | already widened |
| 6 | **Moby shadow staging depth, SHADED pass** | **`0x80022A2C` / `0x80022C30`** | `field_shaded_queue_scene.cpp` | **WIDENED NOW** — see below |
| 7 | **Cyclorama (sky/portal) mask quad** | 512×240 literal, no guest compare | `cyclorama_mask_recipe.cpp:101` | **WIDENED NOW** — see below |
| 8 | Cyclorama portal mesh screen width | — | `cyclorama_portal_mesh_recipe.cpp` ×2 | already widened; now via `drawClipRight` |
| 9 | Glow screen-edge outcode | `0x80058BA8` / `0x800580F4` | `glow_recipe.cpp:41` `outcode` | already widened — `outcode` takes the frame's own right edge; the 684-wide glow edge named in `project-state` near line 454 is this row |
| 10 | Particle type 0 (point) and type 1 (line) visibility | `0x800573C8` | `field_particle_endpoint.h` `classify` | already widened (0152) |
| 11 | Particle type 2/3 | — | `field_particle_type{2,3}_submitter.cpp` | **N/A** — their extent is derived from the projected centre (`center.sx + (width >> 1)`), so there is no fixed screen-rect limit to override |
| 12 | World render width | — | `world_scene_builder.cpp:47` | already widened; now via `drawClipRight` |
| 13 | Terrain right clip | — | `terrain_scene.cpp:188` | already widened; now via `drawClipRight` |
| 14 | Per-object draw distance (`cullingDistance`, `coarseVisible`) | — | `world_source`, `actor_scene_builder` | **N/A** — a depth bound, not a horizontal one. Out of scope by definition, and changing it would alter gameplay-adjacent behaviour |
| 15 | HUD and the 2D overlay layer | — | `field_2d_overlay_recipe.*` | **N/A — not ours.** Anchoring is owned by agent `hud-anchor` under S030. This change touches no HUD producer |

### 6. The shaded shadow staging limit — established against retail, and 0152 was wrong

The operator's requirement was to establish this against retail *first*. Done, from a RAM dump of
the loaded executable:

    80022C2C  bgez  $a0, 0x80022C48          ; m_ShadowDistance >= 0 -> skip
    80022C30  addi  $a0, $v1, -0x1100
    80022C34  bgez  $a0, 0x80022C48          ; viewZ - 0x1100 >= 0 -> skip  =>  stage iff viewZ < 0x1100

and the regular pass, which is the same idiom with a different constant:

    8001F344  bgez  $t3, 0x8001F37C
    8001F34C  addi  $t3, $v1, -0x1200
    8001F350  bgez  $t3, 0x8001F37C          ;  =>  stage iff viewZ < 0x1200

`$v1` is a **positive** GTE depth: `mfc2 $v1, $k1, 0` at `0x80022BD8`, and the bounds around it are
`sub $a1, $v1, $a0; bgez` (skip when the difference is `>= 0`), which only reads as a *far* bound if
the depth is positive.

**So both pairs are near bounds, the shaded limit is 0x1100 positive, and the shaded pass stages
shadows.** `kShadedShadowStagingDepth` is 0x1100 and the pass now stages.

### 7. The cyclorama mask

`cyclorama_mask_recipe.cpp` built its screen-space quad from a literal `{{{0,0},{512,0},{512,240}}}`,
so the portal clip region never reached columns 512..683 and the sky was unmasked in the margin. It
now spans the drawn width. The vertical extent is untouched — 240 lines is the authored sky box at
both aspects.

## Correction to 0152

Issue 0152 reported that the shaded pass "compares the negated depth", that `view[2] - (-0x1100)`
"is negative for every visible Moby", and that the pass therefore staged **no** shadow — and on that
basis it *reverted* a correct fix and pinned the wrong behaviour with a negative test.

**That reasoning was circular.** It was read off this project's own prose comment, which had already
been written from the same unverified assumption, and not off the bytes. The disassembly above
refutes it. The port shipped a fidelity defect, the 0152 change removed the fix that corrected it,
and the negative test would have kept it removed.

Corrected here, in three places: the constant is `actor_scene::kShadedShadowStagingDepth = 0x1100`;
the call site passes it; and the negative test is replaced by
`test_shaded_pass_stages_against_its_own_nearer_limit`, which pins the positive behaviour *and* that
the shaded limit is strictly nearer than the regular one, so a build that swaps the two constants
fails.

The general lesson, and the reason this is in the issue rather than only in a commit message: a
claim about a guest constant that is "established" by reading the project's own comment about that
constant has not been established at all. Every such claim in this task is now cited to bytes.

## The proof

A pixel census cannot answer this. A pixel that was already non-black and stays non-black
contributes the same 1 to the margin total whatever colour it is, so a widening that moves or
recolours existing geometry is **invisible** to a pixel count — which is exactly what 0152 measured
and exactly why its margin number could not move.

So the proof counts **objects**, per class, with the span each producer actually rasterised.

- `game/render/temporal/margin_object_census.{h,cpp}` — a `SpanBucket` per class (drawn, outside the guest
  window, leftmost and rightmost screen x) accumulated in a `Recorder` owned by `SpyroContext`. A
  recipe with no `Core` by design carries a bucket and the `Core` owner drains it.
- Fed at each producer's **post-reject** point, so "outside" means drawn:
  sectors at the world recipes' face-append (`world_lq_recipe.cpp`, `world_hq_refinement.cpp`), Mobys
  at the single face emission shared by the regular and secondary layers
  (`actor_face_submitter.cpp`), particles at both `field_particles` submission arms, glows at
  `glow_submitter.cpp`, shadows at `field_shadow_submitter.cpp`.
- `PSXPORT_MARGIN_CENSUS=<path>` writes the report at process exit. It is read through the framework's
  `cfg_str` owner, never `getenv`; a run without it pays one branch per object and writes nothing.
- `tools/margin_coverage.py --class-census CONTROL WIDENED` reads a matched 4:3/16:9 pair and
  reports each class against the 4:3 control, naming any class the widening never reached. It
  **refuses** a report naming fewer than the closed class set, because a class silently absent is
  indistinguishable from a class that never drew.

Both answers are selftested: `tests/test_margin_object_census.cpp` (8 cases) and
`tools/margin_coverage.py --selftest` (13 cases, up from 8) cover the native control reporting zero
objects past the window, the widened run reporting them, the half-open boundary, a wide object whose
centre is inside but whose span is not, `merge()`, a never-drawn class, and a **negative** case
where every class reports `outside=0` and the tool must refuse to score that as reaching its
producers.

## Measured

Recorded in the run report below; see `docs/project-state.md` S019.

## What this does not do

- It does not widen the HUD or the 2D overlay layer. That is agent `hud-anchor`'s, under S030.
- It does not touch depth culls. They are not horizontal limits, and a draw-distance change is a
  different question with a different owner.
- It does not make the margin *look* like more game. It makes every owned limit stop hiding what the
  widened view would otherwise show, which is the whole claim.
