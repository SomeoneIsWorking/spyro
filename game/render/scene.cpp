// scene.cpp — WHICH SCENE is on screen, and what porting it would take.
//
// The guest's render driver 0x8001ED5C is a linear if-chain over one stage selector, and each arm
// is a different screen (front-end, title, field, …). So "what am I being asked to draw" has a
// concrete answer in game state, and it is the answer a native producer has to dispatch on. This
// file holds that identity: the selector, the arms transcribed from the chain, and the FIELD arm's
// layer list.
//
// NOTHING HERE IS NAMED BEYOND ITS ADDRESS unless this repo already named it. A stage whose role
// has not been reverse-engineered gets "(role not RE'd)" — an invented name would read as
// knowledge, and the whole point of this table is to be the porting backlog rather than a story
// about one.
#include "core.h"
#include "guest_gp.h"
#include "proj_params.h" // ProjParams::geomValid — is the camera the game STATED available yet?
#include "render.h"
#include <lucent/log.h>

namespace {

// gp+0x574 — the STAGE SELECTOR. Both the per-frame update 0x8003385C and the render driver
// 0x8001ED5C dispatch on it, so it names WHICH game mode is running and therefore which scene a
// native renderer would have to produce.
constexpr uint32_t kStageSelector = kGp + 0x574u; // 0x800757D8

// ── The stage arms of the render driver 0x8001ED5C ───────────────────────────────────────────────
// Transcribed from the linear if-chain at 0x8001EDF8-0x8001EF80 and spot-checked against the
// disassembly (the chain compares [0x800757D8] against 1,2,3,4,5,6,7,8,9,0xA…0xF in order). Two
// arms are themselves conditional and two more are indirect; all four are represented honestly
// rather than collapsed to a single address.
constexpr StageArm kStageArms[] = {
    {0,
     0x00000000u,
     "FIELD / world — the arm that runs during gameplay (C151, snap_15000). "
     "Not one call: a 10-entry layer list, see kFieldLayers"},
    {1, 0x8001A050u, "(role not RE'd)"},
    {2, 0x8001A40Cu, "(role not RE'd)"},
    {3, 0x8001A40Cu, "(role not RE'd)"},
    {4, 0x8001CA38u, "(role not RE'd) — reaches EmitStaticActorMeshList 0x8004EBA8"},
    {5, 0x8001CA38u, "(role not RE'd) — reaches EmitStaticActorMeshList 0x8004EBA8"},
    {6, 0x8001A40Cu, "(role not RE'd)"},
    {7, 0x00000000u, "INDIRECT — calls (*[0x8007567C])(), so the handler is data, not code"},
    {8, 0x8001CFDCu, "(role not RE'd)"},
    {9, 0x8001A050u, "(role not RE'd)"},
    {10, 0x8001C694u, "(role not RE'd)"},
    {11, 0x8001D718u, "(role not RE'd)"},
    {12, 0x8001E24Cu, "(role not RE'd)"},
    {13, 0x00000000u, "SPLIT on [0x80078D78]==3 -> 0x8001E6B8, else 0x8007CEE4"},
    {14, 0x8001E9C8u, "(role not RE'd) — reaches RenderWorldChunks 0x800258F0"},
    {15,
     0x00000000u,
     "SPLIT on [0x80075704]<99 -> 0x8007BFD0, else 0x8001EB80 "
     "(the latter reaches RasterizeSpritePrimQueue 0x80022A2C)"},
};
constexpr uint32_t kStageIndirectPtr13 = 0x80078D78u; // the [..]==3 discriminator of stage 13
constexpr uint32_t kStageIndirectPtr15 = 0x80075704u; // the [..]<99 discriminator of stage 15
constexpr uint32_t kStageFnPtr7 = 0x8007567Cu;        // stage 7's function pointer

// ── The FIELD (stage 0) layer list ───────────────────────────────────────────────────────────────
// The stage-0 arm of 0x8001ED5C, in order, from the decompile in scratch/decomp/frameloop.c. `gate`
// is the global the guest tests before making the call; 0 means unconditional. This list IS the
// native-renderer backlog for the field, in the order the guest draws it.
constexpr FieldLayer kFieldLayers[] = {
    {0x800521C0u, 0, false, "(role not RE'd)"},
    {0x80019300u, 0x80075690u, false, "(role not RE'd) — runs when [0x80075690] == 0"},
    {0x80018908u, 0x80075714u, true, "(role not RE'd) — runs when [0x80075714] != 0"},
    {0x80019698u,
     0,
     false,
     "actor pass — reaches EmitActorDrawList 0x8001F798 and "
     "EmitSecondaryActorPrimitives 0x80020F34"},
    {0x8002B9CCu, 0, false, "(role not RE'd)"},
    {0x80050BD0u, 0, false, "(role not RE'd)"},
    {0x800573C8u, 0, false, "(role not RE'd)"},
    {0x800190D4u,
     0x80075918u,
     true,
     "(role not RE'd) — runs when [0x80075918] != 0, called with "
     "([0x80075918]<<3) in a1/a2/a3"},
    {0x80018F30u,
     0,
     false,
     "(role not RE'd) — runs when [0x8007570C] != 0 OR "
     "[0x800756C0] != 0; reported as always-armed here because this "
     "classifier does not evaluate the OR"},
    {0x800189F0u, 0, false, "(role not RE'd)"},
};

} // namespace

// WHAT THIS CLASSIFIER CAN AND CANNOT TELL APART, stated where it is implemented rather than in a
// report someone has to find:
//   * IT CAN distinguish the 16 stage-selector values, and for stages 7/13/15 it also reads the
//     runtime discriminator that picks the actual handler — so those three report a HANDLER, not
//     just a stage.
//   * IT CANNOT say what most of those stages ARE. Only stage 0 (the field) has an RE'd role, so a
//     "scene identity" here is an address plus what this repo has proven about it — never a name.
//   * IT DOES NOT look below the stage. Two frames of the same stage drawing completely different
//     content (a different level, a different menu page) are one identity to it.
Scene SpyroRenderer::classifyScene() const {
  const uint32_t s = mC->mem_r32(kStageSelector);
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
void SpyroRenderer::reportBacklog(const Scene &sc) const {
  if (!sc.arm) {
    lucent::error("render",
                  "  stage {} is outside 0..15: the guest's render driver 0x{:08X} falls "
                  "off its if-chain and draws nothing for it, so there is nothing to port "
                  "for this stage — but reaching it means the selector is a value this port "
                  "has not seen before. Investigate before adding an arm.",
                  sc.stage,
                  kFrameRenderDrv);
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
                  kStageIndirectPtr13,
                  mC->mem_r32(kStageIndirectPtr13));
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
