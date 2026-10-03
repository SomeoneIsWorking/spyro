// field_2d_overlay_recipe.h — the FIELD arm's screen-space 2D layer, derived purely.
//
// WHAT THIS LAYER IS, IN THE GUEST'S OWN TERMS. It is not a render pass; it is a SECOND ORDERING
// TABLE. Every screen-space primitive in a GS_Playing frame is prepended to the list whose head
// pointer lives at 0x8007581C, by the leaf at 0x800168DC:
//
//     0x800168DC   lui $v1,0x8007 ; addiu $v1,$v1,0x581C ; lw $v1,($v1)   ; $v1 = *0x8007581C
//                  lw  $at,($v1) ; beqz $at,…                                     ; $at = $v1[0]
//                  sw  $a0,($v1) ; sh $a0,($at) ; sb ($a0>>16),2($at)             ; prepend
//
// while the depth-indexed table at 0x80075820 is the one 0x800168A0 writes (`sll $a1,$a1,3 ;
// add $a1,$a1,$a2`). 0x80016784(n) — the frame's `DrawOTag(func_80016784(0x800))` argument — walks
// the depth table from slot n down to 0 clearing both words of each slot, then clears the front
// list. game/render/frame_env.cpp already names the same three globals (kFrontList 0x8007581C,
// kOtBase 0x80075820, kPoolBase 0x800757B0), which is independent corroboration of the decode.
//
// WHY THAT MATTERS HERE. A layer with its own ordering table, fed by a leaf that takes no
// coordinates and no depth, and reached through producers whose OWN bodies contain no COP2 at all,
// is a flat screen-space overlay. Measured over the 13 producer bodies that build it
// (tools/probe_field_2d_layer.py): 1,451 instruction words scanned, 0 COP2/LWC2/SWC2, and the only
// OT-link leaf reached is 0x800168DC for eight of them — 0x800189F0's tracer streaks being the one
// member that links through 0x800168A0 because they carry a projected depth. So the three draws
// modelled here need NO GTE state at all: they are derivable from pre-GTE game state, which is the
// line AGENTS.md draws ("native render producers consume pre-GTE game state").
//
// THE THREE PRODUCERS, IN THE GUEST'S OWN PUBLICATION ORDER. GamestateDraw's GS_Playing arm
// (external/spyro-1/src/gamestates/draw.c:2715) calls, in this order:
//
//   0x80019300  collectables  — the orb/egg POLY_FT4s, plus a shaded-moby-queue append that is
//                                guest STATE and belongs to the shaded-queue producer, not here
//   0x80019698  actors        — the HUD text/score Mobys reach the picture through 0x80022A2C,
//                                which is the world-shaded sprite queue and already has a temporal
//                                source; this layer owns no part of it
//   0x8002B9CC  environment
//   0x80050BD0  cyclorama
//   0x800573C8  particles
//   0x800190D4  screen fade   — one DR_MODE + one POLY_F4 over (0,8)-(512,232)
//   0x80018F30  screen border — two black POLY_F4 bars
//   0x800189F0  tracers       — NOT modelled here; see field_2d_overlay.cpp
//
// The sprites and the fade/border are published at DIFFERENT points, and the difference is
// load-bearing: the sprite quads and the fade quad overlap, so which one is submitted last decides
// which one is blended over the other. That is why `Part` exists and why the owner publishes a part
// at a time instead of the overlay in one call.
//
// THE PACKET SHAPES, from the bytes. `lui $reg,0x900` + `sw` for the sprite (0x8001919C:09), `lui
// $reg,0x500` for the fade and both bars (0x800190D4:24, 0x80018F98 and 0x80018FEC). The fade's
// code byte is 0x2A = 0x28|0x2 (0x8001912C) and the sprite's is 0x2C (0x800191B8); the bars carry a
// bare 0x28 (`addiu $v0,$zero,0x28 ; sb $v0,7($s0)`, 0x80019008). 0x2 is the semi-transparency bit,
// so the fade and the sprites are alpha-blended and the bars are opaque — which is the whole reason
// the border is a black bar and the fade is a dimmer.
#pragma once

#include "field_collectables_recipe.h"
#include "instance_pairing.h"
#include "screen_border_recipe.h"
#include "screen_fade_recipe.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace spyro::field_2d_overlay_recipe {

// The guest's three publication points, named by the producer that owns each. The order of the
// enumerators is NOT the order they are published in — see the header note; `Sprite` is published
// first, at the collectables call site.
enum class Part : uint8_t { Sprite, Fade, Border };

// The guest address of each producer is named where its queue emission lives — `screen_fade`
// (0x800190D4), `screen_border` (0x80018F30) and `field_collectables` (0x80019300, which
// reaches the sprite primitive at 0x8001919C). There is deliberately no copy of that table here:
// the overlay publishes through those owners, which already open their own producer scopes, and a
// second table of addresses would be a second thing to fall behind.

// The guest's own RECT, as func_8001919C reads it (`lhu ($a0)`, `lhu 2($a0)`, `lhu 4($a0)`,
// `lhu 6($a0)` at 0x800191F8..0x80019274).
struct Rect {
  int32_t x = 0;
  int32_t y = 0;
  int32_t w = 0;
  int32_t h = 0;
};

// ONE screen-space 2D draw. Every field is written for at least one `Part`; the untextured draws
// carry a zero texture origin and tpage because the guest writes zero texcoords into a POLY_F4 too
// (0x8001901C onward stores 0 into both coordinate words' halves through the `sh`/`sb` pairs).
struct Draw {
  Part part = Part::Fade;
  // The pairing key. It is derived from (part, slot) and is never zero, because every draw in this
  // overlay is attributable by construction: there is no per-instance guest pointer to lose, which
  // is exactly why this layer's census can report `unattributed == 0` and mean it.
  uint32_t instance = 0;
  uint8_t slot = 0; // the fade is slot 0; the bars are 0 and 1; a sprite is its own ordinal
  Rect rect{};
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  bool semi = false;     // the guest's code bit 0x2
  uint8_t blendMode = 0; // the DR_MODE blend word the guest set before this prim (`sll $a3,$a0,5`)
  uint8_t u0 = 0;
  uint8_t v0 = 0;
  uint16_t clut = 0;
  uint16_t tpage = 0;
};

// The guest's three CALL-SITE GATES, and they are not the producers' own conditions. The GS_Playing
// arm decides whether to CALL each producer; only then does the producer decide what to draw:
//
//   0x80019300  draw.c:2725  `if (!g_IsFlightLevel) { func_80019300(); }`
//   0x800190D4  draw.c:2739  `if (g_Fade) { func_800190D4(2, g_Fade * 8, ...); }`
//   0x80018F30  draw.c:2743  `if (g_ScreenBorderEnabled || D_800756C0) { …; }`
//
// These three conditions were written TWICE: once here, to decide what the derivation may emit, and
// once in `Frame::armed`, to decide what the logic frame publishes. A gate written twice is a gate
// that gets applied on one path and not the other, and this layer had exactly that. `derive`
// ignored the flight-level gate, so a flight frame's endpoint could carry orb/egg sprites the guest
// never reaches — 0x80019300 is not called there at all, so its sprite loops at draw.c:583/597
// never run, whatever the HUD block says — while the logic frame correctly published none. `derive`
// applies the gate now and `Frame::armed` asks these three functions, so there is one
// implementation of each and the endpoint cannot disagree with the logic frame about whether a
// producer ran.
struct Gates {
  bool fade = false;    // draw.c:2739 — 0x800190D4 was called
  bool border = false;  // draw.c:2743 — 0x80018F30 was called
  bool sprites = false; // draw.c:2725 — 0x80019300 was called
};

// The per-part draw counts, in the order the field2dtemporal channel prints them (fade, border,
// sprites). COMPUTED rather than stored beside `draws`: a stored count is a second thing that can
// disagree with the vector it counts.
struct PartCounts {
  uint32_t fade = 0;
  uint32_t border = 0;
  uint32_t sprites = 0;
};

// One logic frame's whole 2D overlay, in the order the guest emits it. A flat vector rather than
// three named groups because the interval has to pair the three fixed slots and the N sprites with
// ONE occurrence-ordered walk (spyro::instance_pairing), and because a group per part would make
// "which draw is the second border bar" a second question with a second answer.
struct Overlay {
  std::vector<Draw> draws;
  // The bar height the guest COMMITS while building this layer: 0x80018F30 stores its stepped
  // height back to D_800756C0 (0x800756C0) at 0x80018F80 and 0x80018FA4. Carried out of `derive` so
  // the caller can write it, because a derivation that hid the commit would make the border never
  // move — the failure is invisible, because the picture would still be a correct bar of the wrong
  // height.
  int32_t barHeight = 0;
  // Which of the guest's three call sites ran on THIS frame. Retained with the endpoint rather than
  // recomputed where it is printed, because "this frame drew no 2D layer" is only worth retaining
  // if it arrives with the reason: `draws=0` on its own is equally the shape of the correct null
  // (the guest called nothing) and of a silent failure of the derivation, and the field2dtemporal
  // census has to tell those apart rather than leave a reader to guess which one 2,505 of them
  // were.
  Gates gates{};
};

inline constexpr uint32_t kFadeSlot = 0u;
inline constexpr uint32_t kBorderTopSlot = 0u;
inline constexpr uint32_t kBorderBottomSlot = 1u;
inline constexpr uint32_t kSpriteSlotBase = 16u; // sprites start past every fixed slot

inline uint32_t instanceOf(Part part, uint8_t slot) {
  switch (part) {
  case Part::Fade:
    return 1u;
  case Part::Border:
    return 2u + slot;
  case Part::Sprite:
    return kSpriteSlotBase + slot;
  }
  return 0u;
}

// The pre-GTE game state the three producers read. Every field is named after the guest's own
// global and carries its address, because a native producer that reads state it cannot name is a
// magic offset with a comment.
struct State {
  uint32_t fade = 0;          // g_Fade                 0x80075918
  uint32_t borderEnabled = 0; // g_ScreenBorderEnabled  0x8007570C
  int32_t barHeight = 0;      // D_800756C0             0x800756C0
  int32_t deltaTime = 0;      // g_DeltaTime            0x800756CC
  // The DRAWENV's draw offset and the live render width. The guest authored 512; the port widens
  // the uniform overlay, which is why these are inputs rather than constants.
  int32_t drawOffsetX = 0;
  int32_t drawOffsetY = 0;
  int32_t renderWidth = 512;
  // The HUD block, as field_collectables_recipe::State already reads it: g_Hud 0x80077FA8,
  // m_SpriteRect 0x8007840C, m_OrbAndEggSprite 0x8007850C, D_800770C8.m_specularTime 0x800770F4.
  field_collectables_recipe::State collectables{};
};

// The guest's three call-site conditions, each a function of the pre-GTE state and nothing else.
// These are the ONLY implementations; `derive` applies them and `Frame::armed` asks them.
inline bool armedFade(const State &state) {
  return state.fade != 0u; // draw.c:2739 `if (g_Fade)`
}

inline bool armedBorder(const State &state) {
  // draw.c:2743 `if (g_ScreenBorderEnabled || D_800756C0)`, on the PRE-step height: the guest's
  // `if` runs before 0x80018F30 has stepped anything, and a gate that read the stepped value would
  // keep the producer alive for one frame after the guest's would have released it.
  return state.borderEnabled != 0u || state.barHeight != 0;
}

inline bool armedSprites(const State &state) {
  // draw.c:2725 `if (!g_IsFlightLevel) { func_80019300(); }`. The sprite loops INSIDE 0x80019300
  // (draw.c:583 and 597) are not themselves gated on the flight level, so a model of the producer's
  // body alone draws them in a flight level; it is the CALL SITE that keeps them out, and a flight
  // level has its own UI instead (`HudTick` is skipped there too, update.c:1127).
  return !state.collectables.flightLevel;
}

// The bar height after the guest's own step. It is NOT reimplemented here: `derive` takes it from
// screen_border_recipe, which owns the rule, so there is one implementation of the step and not two
// that can agree today. The caller needs it to write D_800756C0 back even when the overlay
// publishes no bar, because the guest steps that global BEFORE it decides whether the bar has a
// height.

// The overlay, from the three producers' own recipes. `Status::InvalidCount` is the collectables
// recipe's own refusal (a life/egg count outside what the sprite pool can represent) and it refuses
// the WHOLE overlay rather than publishing the fade and the border without the sprites they were
// drawn over — a partial layer is a picture that differs from the guest's for a reason nobody
// chose.
enum class Status : uint8_t { Ready, ValidEmpty, InvalidCount };

inline const char *statusName(Status status) {
  switch (status) {
  case Status::Ready:
    return "ready";
  case Status::ValidEmpty:
    return "valid-empty";
  case Status::InvalidCount:
    return "invalid-count";
  }
  return "unknown";
}

inline Status derive(const State &state, Overlay &overlay) {
  overlay.draws.clear();
  overlay.gates = {
      .fade = armedFade(state), .border = armedBorder(state), .sprites = armedSprites(state)};

  const auto fade = screen_fade_recipe::field(
      state.fade, state.drawOffsetX, state.drawOffsetY, state.renderWidth);
  if (overlay.gates.fade && fade.visible) {
    overlay.draws.push_back({.part = Part::Fade,
                             .instance = instanceOf(Part::Fade, 0),
                             .slot = static_cast<uint8_t>(kFadeSlot),
                             .rect = {fade.x0, fade.y0, fade.x1 - fade.x0, fade.y1 - fade.y0},
                             .r = fade.r,
                             .g = fade.g,
                             .b = fade.b,
                             .semi = true,
                             .blendMode = fade.blendMode});
  }

  const auto border = screen_border_recipe::field(state.borderEnabled,
                                                  state.barHeight,
                                                  state.deltaTime,
                                                  state.drawOffsetX,
                                                  state.drawOffsetY,
                                                  state.renderWidth);
  // The guest commits the stepped height whether or not the bar ended up with one, so the owner
  // reads it from the recipe unconditionally. Stepping while the call site is shut is a provable
  // no-op rather than an extra state change: the gate at draw.c:2743 is false only when
  // g_ScreenBorderEnabled == 0 AND D_800756C0 == 0, and the down ramp of a zero height by a
  // positive g_DeltaTime is zero, so the committed word is the word that was already there.
  overlay.barHeight = border.barHeight;
  if (overlay.gates.border && border.visible) {
    const int32_t width = border.x1 - border.x0;
    overlay.draws.push_back(
        {.part = Part::Border,
         .instance = instanceOf(Part::Border, 0),
         .slot = static_cast<uint8_t>(kBorderTopSlot),
         .rect = {border.x0, border.topY0, width, border.topY1 - border.topY0}});
    overlay.draws.push_back(
        {.part = Part::Border,
         .instance = instanceOf(Part::Border, 1),
         .slot = static_cast<uint8_t>(kBorderBottomSlot),
         .rect = {border.x0, border.bottomY0, width, border.bottomY1 - border.bottomY0}});
  }

  const auto sprites = field_collectables_recipe::derive(state.collectables);
  if (sprites.status == field_collectables_recipe::Status::InvalidCount) {
    overlay.draws.clear();
    return Status::InvalidCount;
  }
  // The sprite positions are the guest's own RECTs; the port adds the frame's draw offset exactly
  // where field_collectables did (`sprite.rect.x + gpu.s_off_x`), so the same number reaches the
  // queue on both the logic frame and a reconstruction.
  //
  // The call-site gate is applied HERE, not inside field_collectables_recipe::derive, because the
  // recipe models 0x80019300's BODY and the body really does compute the sprites on a flight
  // level — what a flight level does not do is call the body (draw.c:2725). The split is the
  // guest's own: the arm decides whether to call, the producer decides what to draw.
  for (uint32_t i = 0; overlay.gates.sprites && i < sprites.spriteCount; ++i) {
    const auto &sprite = sprites.sprites[i];
    overlay.draws.push_back({.part = Part::Sprite,
                             .instance = instanceOf(Part::Sprite, static_cast<uint8_t>(i)),
                             .slot = static_cast<uint8_t>(i),
                             .rect = {sprite.rect.x + state.drawOffsetX,
                                      sprite.rect.y + state.drawOffsetY,
                                      sprite.rect.w,
                                      sprite.rect.h},
                             .r = sprite.r,
                             .g = sprite.g,
                             .b = sprite.b,
                             .semi = false,
                             .blendMode = 0,
                             .u0 = sprite.tile.u,
                             .v0 = sprite.tile.v,
                             .clut = sprite.tile.clut,
                             .tpage = sprite.tile.tpage});
  }
  return overlay.draws.empty() ? Status::ValidEmpty : Status::Ready;
}

inline PartCounts countParts(const Overlay &overlay) {
  PartCounts counts;
  for (const auto &draw : overlay.draws) {
    switch (draw.part) {
    case Part::Fade:
      ++counts.fade;
      break;
    case Part::Border:
      ++counts.border;
      break;
    case Part::Sprite:
      ++counts.sprites;
      break;
    }
  }
  return counts;
}

// ── Why a record could not be sampled against the frame before it
// ───────────────────────────────────
//
// `None` is the compatible case; every other value names the exact field that differed. A bare
// "not interpolated" count cannot be told from a rule that never ran, and the egg sprites are the
// case that proves the rule is load-bearing rather than decorative:
//
//   RECOVERED. g_Hud.unk_0x40 (0x80077FE8) advances by one modulo nine once per game update
//   (0x8002EB48 `addiu $a1,$a1,1`, then the 0x38E38E39 multiply and the `subu $a1,$a1,$v0` at
//   0x8002EB98, stored by `sw $a1,0x7fe8($at)` at 0x8002EBA0), and func_80019300's egg loop reads
//   it to pick the tile: `lw $a3,-0x424($s2)` at 0x8001961C, `(unk_0x40 + i) % 9` by the same
//   0x38E38E39 reciprocal, and `addiu $a1,$s2,0x108 ; addu $a1,$a3,$a1` at 0x80019638/0x80019660
//   for the `&m_OrbAndEggSprite[1 + phase]` base. So EVERY egg's texture advances one animation
//   frame per game update. Blending its rectangle across that flip would be interpolating between
//   two different pictures, and the honest output is the current endpoint's own tile.
//
//   The life orbs are the opposite: their tile is `m_OrbAndEggSprite[0]` (0x80019578
//   `addiu $a1,$s2,0xa0`), constant, and only the grey level moves — `((COSINE_8((specularTime -
//   i*256/20) & 0xFF) >> 7) + 128)`, recovered from 0x80019564..0x800195B8. So they pair.
enum class Mismatch : uint8_t {
  None,
  PartSlot, // the two endpoints disagree about WHICH draw this is (a slot reused for another part)
  TilePage, // the texture page changed: a different image, not a moved sprite
  Clut,     // the palette changed
  TileOrigin, // the texel origin changed: a different tile of the same sheet
  BlendMode,  // a different DR_MODE blend, so the two draws are not the same operation
};
inline constexpr size_t kMismatchCount = static_cast<size_t>(Mismatch::BlendMode) + 1u;

inline const char *mismatchName(Mismatch mismatch) {
  switch (mismatch) {
  case Mismatch::None:
    return "none";
  case Mismatch::PartSlot:
    return "part-slot";
  case Mismatch::TilePage:
    return "tile-page";
  case Mismatch::Clut:
    return "clut";
  case Mismatch::TileOrigin:
    return "tile-origin";
  case Mismatch::BlendMode:
    return "blend-mode";
  }
  return "unknown";
}

using Census = instance_pairing::ReasonedCensus<Mismatch, kMismatchCount>;

// Two draws describe the same overlay entry when everything the interval does NOT sample is already
// identical. Position and colour are exactly what it samples; the texture a draw reads from, the
// blend it runs under, and which part and slot it is, are not.
inline Mismatch mismatch(const Draw &previous, const Draw &current) {
  if (previous.part != current.part || previous.slot != current.slot) {
    return Mismatch::PartSlot;
  }
  if (previous.tpage != current.tpage) {
    return Mismatch::TilePage;
  }
  if (previous.clut != current.clut) {
    return Mismatch::Clut;
  }
  if (previous.u0 != current.u0 || previous.v0 != current.v0) {
    return Mismatch::TileOrigin;
  }
  if (previous.blendMode != current.blendMode) {
    return Mismatch::BlendMode;
  }
  return Mismatch::None;
}

// Nearest integer, half away from zero, which is what a mid-frame sample of an integer pixel
// coordinate means. Two properties are load-bearing and both are asserted by the focused test:
//
//   * THE ENDPOINTS ARE EXACT. `lerp(a, b, 0.0) == a` and `lerp(a, b, 1.0) == b` for every pair.
//   The
//     real present runs this same reconstruction at t == 1, so a sampler that was off by one at
//     either end would make the logic frame's picture depend on which of the two paths drew it —
//     and the first version of this function added 0.5 unconditionally, which moved every
//     coordinate up a pixel at t == 0. A sampler whose endpoints are not its inputs is not an
//     interpolation.
//
//   * IT IS SYMMETRIC. `lerp(a, b, t) == lerp(b, a, 1 - t)` exactly, because the interpolated value
//     does not depend on which endpoint is called "previous". Round-half-away-from-zero is
//     symmetric under negation; a rule that rounded ties toward the nearer endpoint is not, and it
//     rounds a tie AWAY from the endpoint that is nearer, which is the wrong way to be wrong.
inline int32_t lerp(int32_t a, int32_t b, double t) {
  return static_cast<int32_t>(std::round(static_cast<double>(a) + static_cast<double>(b - a) * t));
}

inline uint8_t lerpChannel(uint8_t a, uint8_t b, double t) {
  return static_cast<uint8_t>(lerp(static_cast<int32_t>(a), static_cast<int32_t>(b), t));
}

// The predecessor of each of `current`'s draws, parallel to `current.draws`, nullptr where a draw
// was unpaired or its predecessor was rejected. Pure: no Core, no queue, no guest state.
inline std::vector<const Draw *>
pair(const Overlay &previous, const Overlay &current, Census &census) {
  struct Slot {
    const Draw *draw = nullptr;
    const Draw *endpoint = nullptr;
  };
  std::vector<Slot> before(previous.draws.size());
  std::vector<const Slot *> beforeCursor;
  beforeCursor.reserve(before.size());
  for (size_t i = 0; i < previous.draws.size(); ++i) {
    before[i].draw = &previous.draws[i];
    beforeCursor.push_back(&before[i]);
  }
  std::vector<Slot> after(current.draws.size());
  std::vector<Slot *> afterCursor;
  afterCursor.reserve(after.size());
  for (size_t i = 0; i < current.draws.size(); ++i) {
    after[i].draw = &current.draws[i];
    afterCursor.push_back(&after[i]);
  }
  std::vector<uint32_t> reasons(kMismatchCount, 0u);
  const auto identity = [](const Slot &slot) {
    return slot.draw->instance;
  };
  const auto admit = [&reasons](const Slot &endpoint, const Slot &slot) {
    const auto reason = mismatch(*endpoint.draw, *slot.draw);
    if (reason != Mismatch::None) {
      ++reasons[static_cast<size_t>(reason)];
      return false;
    }
    return true;
  };
  const auto apply = [](const Slot &endpoint, Slot &slot) {
    slot.endpoint = endpoint.draw;
    return true;
  };
  instance_pairing::walk(std::span<const Slot *const>(beforeCursor),
                         std::span<Slot *const>(afterCursor),
                         census,
                         identity,
                         admit,
                         apply);
  for (size_t i = 0; i < kMismatchCount; ++i) {
    census.mismatches[i] += reasons[i];
  }
  std::vector<const Draw *> predecessors;
  predecessors.reserve(after.size());
  for (const auto &slot : after) {
    predecessors.push_back(slot.endpoint);
  }
  return predecessors;
}

// `current`, with every PAIRED draw's geometry and colour moved to `t` along the interval. An
// unpaired or rejected draw keeps its own endpoint, which is the only available output for it: the
// frame before did not draw it, or drew a different picture, so there is nothing to interpolate
// from.
//
// ALL FOUR RECT FIELDS ARE SAMPLED, NOT JUST THE ORIGIN, and the border is why. A border bar's
// motion is entirely in its HEIGHT (0x80018F30 ramps D_800756C0 and the bar spans 0..h /
// 240-h..240), so a sampler that moved only x and y left the bars pinned at the newest height — the
// "reconstructed" frame carried the next real frame's bar, which is the forward snap this layer was
// supposed to remove. The focused test caught that by asserting the midpoint of an 18/20 ramp and
// reading 22.
inline void sample(Overlay &current, double t, std::span<const Draw *const> predecessors) {
  for (size_t i = 0; i < current.draws.size() && i < predecessors.size(); ++i) {
    const Draw *endpoint = predecessors[i];
    if (endpoint == nullptr) {
      continue;
    }
    Draw &draw = current.draws[i];
    draw.rect.x = lerp(endpoint->rect.x, draw.rect.x, t);
    draw.rect.y = lerp(endpoint->rect.y, draw.rect.y, t);
    draw.rect.w = lerp(endpoint->rect.w, draw.rect.w, t);
    draw.rect.h = lerp(endpoint->rect.h, draw.rect.h, t);
    draw.r = lerpChannel(endpoint->r, draw.r, t);
    draw.g = lerpChannel(endpoint->g, draw.g, t);
    draw.b = lerpChannel(endpoint->b, draw.b, t);
  }
}

// The whole interval: pair, then sample. `t` outside [0,1] is refused rather than clamped, because
// a clamped sample is a lie about where in the interval the picture was taken and the presenter
// never asks for one.
inline Status interpolate(Overlay &current, const Overlay &previous, double t, Census &census) {
  if (!(t >= 0.0 && t <= 1.0)) {
    return Status::InvalidCount;
  }
  const auto predecessors = pair(previous, current, census);
  sample(current, t, std::span<const Draw *const>(predecessors));
  return Status::Ready;
}

} // namespace spyro::field_2d_overlay_recipe
