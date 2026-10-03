// scene.cpp — WHICH SCENE is on screen, and what porting it would take.
//
// The guest's render driver 0x8001ED5C is a linear if-chain over one stage selector, and each arm
// is a different screen (front-end, title, field, …). So "what am I being asked to draw" has a
// concrete answer in game state, and it is the answer a native producer has to dispatch on. This
// file holds that identity: the selector, the arms recovered from the chain, and the FIELD arm's
// layer list.
//
// NOTHING HERE IS NAMED BEYOND ITS ADDRESS unless this repo already named it. A stage whose role
// has not been reverse-engineered gets "(role not RE'd)" — an invented name would read as
// knowledge, and the whole point of this table is to be the porting backlog rather than a story
// about one.
#include "core.h"
#include "frame_renderer.h"
#include "guest_globals.h"
#include "guest_gp.h"
#include "proj_params.h" // ProjParams::geomValid — is the camera the game STATED available yet?
#include <lucent/log.h>

namespace {

using spyro::render::FieldLayer;
using spyro::render::StageArm;

// gp+0x574 — the STAGE SELECTOR. Both the per-frame update 0x8003385C and the render driver
// 0x8001ED5C dispatch on it, so it names WHICH game mode is running and therefore which scene a
// native renderer would have to produce.
using spyro::guest::kGamestate;

// ── The stage arms of the render driver 0x8001ED5C ───────────────────────────────────────────────
// Recovered from the linear if-chain at 0x8001EDF8-0x8001EF80 and spot-checked against the
// disassembly. Two arms are themselves conditional and two more are indirect; all four are
// represented honestly rather than collapsed to a single address. Every role is named from the
// vendored decomp's `GamestateDraw`, which agrees with this transcription on all sixteen arms —
// handler addresses, the two arms that share a handler, the indirect arm 7, and both two-way
// splits. The selector [0x800757D8] is that decomp's `g_Gamestate`.
constexpr StageArm kStageArms[] = {
    {0,
     0x00000000u,
     "FIELD / world — GS_Playing, the arm that runs during gameplay (C151, snap_15000). "
     "Not one call: a 10-entry layer list, see kFieldLayers"},
    {1, 0x8001A050u, "GS_LevelTransition"},
    {2, 0x8001A40Cu, "GS_PauseMenu — shares its handler with arms 3 and 6"},
    {3, 0x8001A40Cu, "GS_InventoryMenu — shares its handler with arms 2 and 6"},
    {4,
     0x8001CA38u,
     "GS_Respawn — shares its handler with arm 5; reaches EmitStaticActorMeshList "
     "0x8004EBA8"},
    {5,
     0x8001CA38u,
     "GS_GameOver — shares its handler with arm 4; reaches EmitStaticActorMeshList "
     "0x8004EBA8"},
    {6, 0x8001A40Cu, "GS_OldDragon (a prototype leftover) — shares its handler with arms 2 and 3"},
    {7,
     0x00000000u,
     "GS_FlightResults — INDIRECT: calls (*[0x8007567C])(), so the handler is data, not code, and "
     "the decomp confirms it is an OVERLAY function"},
    {8, 0x8001CFDCu, "GS_Dragon"},
    {9, 0x8001A050u, "GS_EntranceAnimation — shares its handler with arm 1"},
    {10, 0x8001C694u, "GS_ExitLevel"},
    {11, 0x8001D718u, "GS_Fairy"},
    {12, 0x8001E24Cu, "GS_Balloonist"},
    {13,
     0x00000000u,
     "GS_TitleScreen — SPLIT on [0x80078D78]==3 -> 0x8001E6B8 (the attract/DEMO mode), else "
     "0x8007CEE4 (the title screen proper; the port's first native producer draws this one)"},
    {14, 0x8001E9C8u, "GS_Cutscene — reaches RenderWorldChunks 0x800258F0"},
    {15,
     0x00000000u,
     "GS_Credits — SPLIT on [0x80075704]<99 -> 0x8007BFD0 (an OVERLAY function), else 0x8001EB80 "
     "(reaches RasterizeSpritePrimQueue 0x80022A2C)"},
};
using spyro::guest::kTitlescreenState;
constexpr uint32_t kStageIndirectPtr15 = 0x80075704u; // the [..]<99 discriminator of stage 15
constexpr uint32_t kStageFnPtr7 = 0x8007567Cu;        // stage 7's function pointer

// ── The FIELD (stage 0) layer list ───────────────────────────────────────────────────────────────
// The stage-0 arm of 0x8001ED5C, in order, from the decompile in scratch/decomp/frameloop.c. This
// list IS the native-renderer backlog for the field, in the order the guest draws it. Every role is
// RE'd from two independent sources: this repo's transcription of the arm, and the vendored
// decomp's stage-0 arm (external/spyro-1/src/gamestates/draw.c:2716-2747), which has the same ten
// calls in the same order under the same conditions. The gates name four globals:
// [0x80075690] g_IsFlightLevel, [0x80075714] g_DemoMode, [0x80075918] g_Fade,
// [0x8007570C] g_ScreenBorderEnabled.
//
// WHICH LAYERS NEED A 3D PRODUCER was measured by COP2 traffic over each layer's OWN body,
// not over its call closure: a call-graph walk scores the hand-written
// assembly particle renderer 0x800573C8 at zero because it projects and emits inline and calls
// nothing. On SCUS_942.28 over 779 function extents the 3D backlog is FIVE layers, and two of them
// (environment, cyclorama) bottom out in renderers this port already owns byte-exactly.
constexpr FieldLayer kFieldLayers[] = {
    {0x800521C0u,
     0,
     false,
     "moby list build — NOT a renderer: 64 instructions, 0 COP2, queues the level's mobys for the "
     "passes below (decomp: 'Queue render mobys', asm/moby_lists.s)"},
    {0x80019300u,
     0x80075690u,
     false,
     "collectables (gems, lives) — 2D, 0 COP2 over 459 instructions; reaches the AddPrim leaf "
     "0x800168DC. Runs when [0x80075690] (g_IsFlightLevel) == 0"},
    {0x80018908u,
     0x80075714u,
     true,
     "demo-mode text — 2D, 0 COP2 over 274 instructions. Runs when [0x80075714] (g_DemoMode) != 0"},
    {0x80019698u,
     0,
     false,
     "actor pass — THE BIGGEST 3D LAYER (cop2 1244): mobys, shadows, Spyro, flame, glows and "
     "sparkles. Reaches the moby renderer init/cull 0x8001F158, EmitActorDrawList 0x8001F798, "
     "EmitSecondaryActorPrimitives 0x80020F34, RasterizeSpritePrimQueue 0x80022A2C and the paired "
     "actor 0x80023AC4 — the last two already have native producers"},
    {0x8002B9CCu,
     0,
     false,
     "environment / world — 3D (cop2 276, ALL of it in RenderWorldChunks 0x800258F0, which this "
     "port owns byte-exactly). Picks the occlusion group and the culling distance, then calls the "
     "world renderer once: the GROUND and the CLIFFS"},
    {0x80050BD0u,
     0,
     false,
     "cyclorama / sky — 3D (cop2 230), bottoms out in EmitStaticActorMeshList 0x8004EBA8, which "
     "this port owns byte-exactly AND already drives as a direct native producer"},
    {0x800573C8u,
     0,
     false,
     "particles — 3D (cop2 166), and ALL 166 are in its own 843-instruction body: a hand-written "
     "assembly renderer that calls nothing, so a call-graph walk scores it 0. asm/renderers/"
     "r_particles.s"},
    {0x800190D4u,
     0x80075918u,
     true,
     "screen fade — 2D, 0 COP2 over 139 instructions; one flat quad through the AddPrim leaf. Runs "
     "when [0x80075918] (g_Fade) != 0, called as (2, g_Fade<<3, g_Fade<<3, g_Fade<<3)"},
    {0x80018F30u,
     0,
     false,
     "screen border — 2D, 0 COP2 over 117 instructions. Runs when [0x8007570C] "
     "(g_ScreenBorderEnabled) != 0 OR [0x800756C0] != 0; reported as always-armed here because "
     "this classifier does not evaluate the OR"},
    {0x800189F0u,
     0,
     false,
     "tracers — the flame/trail streaks. 3D but SMALL (cop2 15, and those are in the shared "
     "world->screen helper 0x80017B48 and isqrt 0x80017A38, not in a renderer of its own): it "
     "projects each tracer point, then builds flat prims from the screen-space deltas"},
};

} // namespace

// WHAT THIS CLASSIFIER CAN AND CANNOT TELL APART, stated where it is implemented:
//   * IT distinguishes the 16 stage-selector values, and for stages 7/13/15 it also reads the
//     runtime discriminator that picks the actual handler — so those three report a HANDLER, not
//     just a stage.
//   * A name in kStageArms is still only a NAME: it says which mode the guest thinks it is in, not
//     that this port can draw it. kFieldLayers is what "can it be drawn" is answered by.
//   * IT DOES NOT look below the stage. Two frames of the same stage drawing completely different
//     content (a different level, a different menu page) are one identity to it.
spyro::render::Scene spyro::render::FrameRenderer::classifyScene() const {
  const uint32_t s = mC->mem_r32(kGamestate);
  for (const StageArm &a : kStageArms) {
    if (a.stage == s) {
      return {s, &a};
    }
  }
  return {s, nullptr};
}

// The backlog for one scene: what the guest would have called, and — for the field — which of its
// layers are ARMED on this very frame, i.e. which are missing from the picture right now.
//
// Every line carries its denominator by construction: the field list prints ALL ten layers with an
// ARMED/not marker, not just the armed ones, so "nothing armed" is visibly different from "the
// reporter had nothing to say".
void spyro::render::FrameRenderer::reportBacklog(const spyro::render::Scene &sc) const {
  if (!sc.arm) {
    lucent::error("render",
                  "  stage {} is outside 0..15: the guest's render driver 0x{:08X} falls "
                  "off its if-chain and draws nothing for it, so there is nothing to port "
                  "for this stage — but reaching it means the selector is a value this port "
                  "has not seen before. Investigate before adding an arm.",
                  sc.stage,
                  spyro::render::kFrameRenderDrv);
    return;
  }
  lucent::error("render", "  arm: {}", sc.arm->what);
  if (sc.arm->handler) {
    lucent::error("render", "  the guest would have called 0x{:08X}", sc.arm->handler);
  }
  if (sc.stage == 7) {
    lucent::error("render",
                  "  the guest would have called (*[0x{:08X}]) = 0x{:08X}",
                  kStageFnPtr7,
                  mC->mem_r32(kStageFnPtr7));
  }
  if (sc.stage == 13) {
    lucent::error("render",
                  "  [0x{:08X}] = {} selects 0x8001E6B8 (==3) or 0x8007CEE4",
                  kTitlescreenState,
                  mC->mem_r32(kTitlescreenState));
  }
  if (sc.stage == 15) {
    lucent::error("render",
                  "  [0x{:08X}] = {} selects 0x8007BFD0 (<99) or 0x8001EB80",
                  kStageIndirectPtr15,
                  mC->mem_r32(kStageIndirectPtr15));
  }
  if (sc.stage == 0) {
    lucent::error("render",
                  "  the FIELD backlog, in the guest's own draw order — ARMED means the "
                  "layer's gate is satisfied on THIS frame, i.e. it is missing from the "
                  "picture right now:");
    for (const FieldLayer &L : kFieldLayers) {
      const bool armed = L.gate == 0 || ((mC->mem_r32(L.gate) != 0) == L.gateNonZero);
      lucent::error("render", "    [{}] 0x{:08X}  {}", armed ? "ARMED" : "  -  ", L.fn, L.what);
    }
  }
  // CAN A PRODUCER EVEN BE WRITTEN FOR THIS FRAME? A native producer projects from the camera the
  // GAME STATED — the OFX/OFY/H it passed to libgte's SetGeomOffset/SetGeomScreen (C156), recorded
  // by the framework at those two leaves. `geomValid()` is false until BOTH have run, and the
  // recorded defaults are 0 rather than the stock 160/120/350 precisely so "never set" cannot be
  // mistaken for "set to the usual thing". Reported here because it is the first thing the first
  // producer will need, and a scene reached before the game states a projection is a scene no
  // producer can draw yet — a fact about ORDER, not about the producer.
  const ProjParams &pp = mC->rsub.projParams;
  lucent::error("render",
                "  projection (the game's own SetGeomOffset/SetGeomScreen values): "
                "geomValid={} ofx={} ofy={} H={}",
                pp.geomValid() ? 1 : 0,
                pp.geomOfx(),
                pp.geomOfy(),
                pp.geomH());
}
