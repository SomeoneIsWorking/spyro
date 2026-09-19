---
id: C143
kind: claim
status: unmeasurable
created: 2026-07-30
tags: render,widescreen,depth
---

## Claim

psxport's widescreen 2D widen CANNOT be enabled on this port yet, and the previously recorded reason was wrong. It was recorded as an ORDERING problem (2D widened before the 3D projection was re-centred); the projection is now re-centred across every contributing renderer, and enabling the widen still makes the picture worse. Measured: sky, ground AND the screen-space caption each move a further +86 px, so it is not shifting 2D relative to 3D — it shifts the WHOLE FRAME a second time. The real gate is 2D-vs-3D discrimination, which rides on per-primitive depth; at ~2.5% depth coverage almost nothing is classified 3D, so 'widen the 2D' means 'widen everything'.

## Evidence

Enabled the latch (s_prev_had3d/s_prev_had_bg2d no longer rolled on a zero-primitive frame) so the 2D widen fires, captured frame 46501 at 16:9, and measured the per-band shift against the same frame with the widen inactive (scratch/screenshots/wide_2d.png vs wide_ofx_all.png, fixed-denominator offset search): sky rows 0-45 +86 px, ground rows 90-150 +86 px, caption rows 180-210 +86 px. All three bands, by the margin, on top of the OFX re-centring already applied.

## What would falsify it

native depth coverage rising enough that s_seen3d is set by real world prims — then the widen would move only screen-space content and this measurement should show the caption moving while sky and ground do not

## NOT CHECKABLE 2026-09-19 — and it describes a path the product no longer uses

Two separate reasons, both measured:

1. **The falsifier cannot be evaluated.** It names native depth coverage rising. The instrument for
   that, I051 `render_depth_coverage_report`, has no call site left in this repository, and
   `tools/depth_cov.py` parses the framework's guest-OT `[ndepth fN]` lines instead. See I051.
2. **The mechanism is not on the shipping path.** This claim is about psxport's widescreen 2D widen
   inside the guest-OT compositor (`gpu_native.cpp`), where the 2D/3D split rides on per-primitive
   depth. The product's render path is `native` — "[render] render path = native — geometry from
   PC-NATIVE producers, rasterized by the PC rasterizer (SDL_GPU)" — which never executes that
   compositor. So even a depth-coverage number would not decide whether Spyro's 2D content widens.

What replaces it: 2D placement on the native path is the render queue's 2D space, `RQ_2D_AUTHORED_4_3`
(centred by the queue) versus `RQ_2D_WIDE_FINAL` (identity, already canvas coordinates). Only three
producers here declare one — `fx_screen_fade`, `fx_dragon_burst`, `fx_screen_border`, all
`RQ_2D_WIDE_FINAL`. Whether every other 2D/HUD producer is correctly placed is the open question, and
it is the same shape as Tomba! 2's issue 0010, which was found by measuring drawn coverage per scene
rather than by reasoning about depth.

## The obvious successor hypothesis is FALSE, checked 2026-09-19

Reframing C143 as a render-queue 2D-space question raises an immediate suspicion: a 2D producer
whose x came out of a WIDENED projection but which does not declare `RQ_2D_WIDE_FINAL` gets centred a
second time, landing one margin off its anchor. That is Tomba! 2 kanban #73, and the margin for
Spyro's 512 -> 684 display is exactly **86 px** — the same number C143 measured moving. It looks like
the answer. It is not.

`fx_sprite_queue` is the producer that fits the shape: it pushes `RQ_HUD` / `RQ_OM_2D_FG` from
`project_screen_vertex`, a real GTE RTPS, and declares no space. But the framework does not widen the
guest GTE at all — `gte_beetle.cpp`: "PSXPORT_WIDE is PC-native widescreen now: the GTE keeps its
NATIVE projection (NO squish) and the renderer re-centers the geometry into a wider scratch
framebuffer at a true wider FOV", with `widescreen_hack = 0`. The sprite queue's own constants agree:
`kQueueExitOfx = 0x01000000` is OFX 256, half of this game's native 512, and every completed
traversal restores the guest's screen centre.

So that producer's x IS authored 4:3, and the queue centring it is correct. The three producers that
do declare `RQ_2D_WIDE_FINAL` — `fx_screen_fade`, `fx_dragon_burst`, `fx_screen_border` — are the
ones fed by the widened NATIVE camera, and they are declared correctly.

What remains genuinely open is the Tomba!-2-issue-0010 question proper: whether a full-screen 2D page
should be centred or have its margins owned. Spyro's pages are the title menu and save picker, and no
capture route reaches them — the boot route hits `abortUnimplemented` on the particles producer
`0x800573C8` at around fence 2,000 (open issue 0103). Measuring them is blocked on that, not on this
claim.
