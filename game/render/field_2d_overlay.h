// field_2d_overlay.h — the owner of the FIELD arm's screen-space 2D layer, and of its temporal
// source.
//
// WHY IT EXISTS. `docs/project-state.md` S020 measures that 82.4% of the items still replayed
// verbatim in an interpolated Spyro 1 frame sit in render-queue layer 3, "the 2D and HUD layer",
// and that no item in it can be attributed to a producer. Both halves of that were true and neither
// was a measurement of the guest:
//
//   * THE LAYER WAS NEVER OWNED AS A LAYER. Three separate producers wrote into it — the screen
//   fade
//     (0x800190D4), the screen border (0x80018F30) and the collectable orb/egg sprites (0x80019300
//     -> 0x8001919C) — each with its own recipe and its own submitter, none of them able to see the
//     other two, and none of them with a temporal source. The composition called them at three
//     different points of the guest's draw order, so they could not simply be merged, and nothing
//     owned the interval between two of their frames.
//
//   * THE REASON NO ITEM CAN BE ATTRIBUTED IS A FRAMEWORK CONSTRAINT, NOT A TITLE OMISSION. A
//     render-queue item's producer identity is `RqItem::painter_object`, and the painter planner
//     validates every item that carries one: `validateFace` refuses anything that is not
//     `RQ_WORLD` with `RQ_OM_DEPTH` (external/psxport/runtime/psx/painter_object_layer.cpp:12-14),
//     and `RenderQueue::emitItemStream` ABORTS on a refused plan
//     (external/psxport/runtime/psx/render_queue.cpp:522-537). A 2D item is `RQ_HUD` by definition,
//     so a 2D item can never carry a painter object in this framework. Every title-owned 2D
//     producer in this repository is therefore anonymous in the fps60 per-producer census, and no
//     amount of title-side work changes that. This owner consequently identifies its items by
//     LAYER, and says so where the identification is made rather than leaving a reader to assume
//     attribution.
//
// THE SCOPE IS THE FIELD ARM, AND IT IS HONEST ABOUT THAT. RQ_HUD items are also emitted by the
// front-end title menu (`fx_title_menu.cpp`), the level-transition tally and the dragon burst. None
// of those is claimed here: the temporal source's endpoint pair is only admissible inside a FIELD
// arm, and a frame's non-overlay 2D items are never claimed, because `owns` is consulted only on a
// frame whose overlay interval was admitted.
//
// WHAT THE OWNER DOES NOT DO. It does not execute guest code, it does not read the GTE, and it does
// not consume GTE output. The three producers' recipes are pure functions of pre-GTE game state
// (see field_2d_overlay_recipe.h for the measured zero-COP2 result that establishes it), and the
// only guest memory this owner writes is the two writes the guest itself makes at this layer — the
// border's stepped bar height and the collectables' shaded-Moby-queue append — both on the logic
// frame, both at the guest's own position, both through the guest's own submitter.
#pragma once

#include "actor_stage.h"
#include "field_2d_overlay_recipe.h"
#include "field_collectables_recipe.h"
#include "fx_field_collectables.h"
#include "fx_screen_border.h"
#include "fx_screen_fade.h"
#include "screen_border_recipe.h"
#include "screen_fade_recipe.h"
#include "temporal_pair.h"

#include <cstdint>
#include <memory>
#include <utility>

#include "core.h"
#include "render_queue.h"

#include <lucent/log.h>

class Core;
struct RenderQueue;

namespace spyro::field_2d_overlay {

using Overlay = field_2d_overlay_recipe::Overlay;
using Part = field_2d_overlay_recipe::Part;
using State = field_2d_overlay_recipe::State;
using Census = field_2d_overlay_recipe::Census;
using Status = field_2d_overlay_recipe::Status;

// The three guest producers' queue-facing recipes, rebuilt from the overlay's draws. The overlay is
// the flat, pairable form; each producer's own recipe is the shape its submitter already takes, and
// the conversion is total in both directions for every field the two share.
inline screen_fade_recipe::Recipe fadeRecipe(const Overlay &overlay) {
  screen_fade_recipe::Recipe recipe;
  for (const auto &draw : overlay.draws) {
    if (draw.part != Part::Fade) {
      continue;
    }
    recipe = {.visible = true,
              .x0 = draw.rect.x,
              .y0 = draw.rect.y,
              .x1 = draw.rect.x + draw.rect.w,
              .y1 = draw.rect.y + draw.rect.h,
              .r = draw.r,
              .g = draw.g,
              .b = draw.b,
              .blendMode = draw.blendMode};
    return recipe;
  }
  return recipe;
}

inline screen_border_recipe::Recipe borderRecipe(const Overlay &overlay) {
  screen_border_recipe::Recipe recipe;
  recipe.barHeight = overlay.barHeight;
  int32_t topY0 = 0, topY1 = 0, bottomY0 = 0, bottomY1 = 0, x0 = 0, x1 = 0;
  int bars = 0;
  for (const auto &draw : overlay.draws) {
    if (draw.part != Part::Border) {
      continue;
    }
    x0 = draw.rect.x;
    x1 = draw.rect.x + draw.rect.w;
    if (draw.slot == field_2d_overlay_recipe::kBorderTopSlot) {
      topY0 = draw.rect.y;
      topY1 = draw.rect.y + draw.rect.h;
    } else {
      bottomY0 = draw.rect.y;
      bottomY1 = draw.rect.y + draw.rect.h;
    }
    ++bars;
  }
  if (bars == 2) {
    recipe = {.visible = true,
              .barHeight = overlay.barHeight,
              .topY0 = topY0,
              .topY1 = topY1,
              .bottomY0 = bottomY0,
              .bottomY1 = bottomY1,
              .x0 = x0,
              .x1 = x1};
  }
  return recipe;
}

// The sprites, in the overlay's own order, and NOTHING else. The shaded-Moby half of the
// collectables producer is guest state: a reconstruction has no business appending to the queue at
// 0x800720F4, and the guest-state half reaches the picture through the world-shaded sprite queue,
// which has its own temporal source.
inline field_collectables_recipe::Recipe spriteRecipe(const Overlay &overlay) {
  field_collectables_recipe::Recipe recipe;
  for (const auto &draw : overlay.draws) {
    if (draw.part != Part::Sprite) {
      continue;
    }
    // The collectables recipe carries the guest's own 16-bit RECT. The overlay's coordinates are
    // 32-bit because the border and the fade are laid out against a WIDENED draw area, and a sprite
    // rect is the guest's own 16-bit value with the draw offset added — which the overlay already
    // folded in. The narrowing is therefore the same narrowing the sprite's own submitter performs
    // when it emits the quad, and it is checked there (a zero or negative width refuses the frame).
    field_collectables_recipe::Sprite sprite{};
    sprite.rect = {.x = static_cast<int16_t>(draw.rect.x),
                   .y = static_cast<int16_t>(draw.rect.y),
                   .w = static_cast<int16_t>(draw.rect.w),
                   .h = static_cast<int16_t>(draw.rect.h)};
    sprite.tile = {.u = draw.u0, .v = draw.v0, .clut = draw.clut, .tpage = draw.tpage};
    sprite.r = draw.r;
    sprite.g = draw.g;
    sprite.b = draw.b;
    recipe.sprites[recipe.spriteCount++] = sprite;
  }
  return recipe;
}

// Publish one part of `overlay` into `target` through the producer's own submitter. The logic frame
// passes this frame's overlay and a reconstruction passes the sampled one, so the two cannot
// diverge for a reason nobody chose: there is one queue path per producer, and it is the
// producer's.
inline bool publish(Core &core, RenderQueue &target, Part part, const Overlay &overlay) {
  switch (part) {
  case Part::Fade:
    return spyro_screen_fade_submit(&core, target, fadeRecipe(overlay));
  case Part::Border:
    return spyro_screen_border_submit(&core, target, borderRecipe(overlay));
  case Part::Sprite:
    return spyro_field_collectables_submit(&core, target, spriteRecipe(overlay));
  }
  return false;
}

class Frame {
public:
  // The only impure step on the logic frame, and it commits nothing: it reads the guest's pre-GTE
  // state through the three producers' own lenses and derives the whole overlay. Reading it up
  // front is what makes one consistent endpoint; the guest's own commits stay at its own positions,
  // below.
  Status capture(Core &core, int32_t drawOffsetX, int32_t drawOffsetY, int32_t renderWidth) {
    state_ = readState(core, drawOffsetX, drawOffsetY, renderWidth);
    collectables_ = field_collectables_recipe::derive(state_.collectables);
    return field_2d_overlay_recipe::derive(state_, overlay_);
  }

  // The guest's own gate for a part, on the PRE-step state. False means the guest would not have
  // called that producer at all, so neither its commit nor its emission runs.
  bool armed(Part part) const {
    switch (part) {
    case Part::Fade:
      return state_.fade != 0u;
    case Part::Border:
      return state_.borderEnabled != 0u || state_.barHeight != 0;
    case Part::Sprite:
      return !state_.collectables.flightLevel;
    }
    return false;
  }

  // The guest's own guest-state write for `part`, at the guest's own position in the FIELD draw
  // order. A reconstruction never calls this: an in-between picture advances no guest clock and
  // appends nothing to the guest's shaded-Moby queue. Returns false when the commit refused, which
  // the composition reports rather than continuing with the guest's queue half-written.
  //
  // The collectables half commits from the recipe derived by the SAME read as the overlay, kept as
  // a member because 0x80019300's guest-state work is expressed in its own types (the shaded-Moby
  // addresses and the gem text) and re-deriving it here would be a second read of the HUD block
  // that could straddle a guest write.
  bool commit(Core &core, Part part) const {
    switch (part) {
    case Part::Fade:
      return true; // 0x800190D4 writes no guest state
    case Part::Border:
      core.mem_w32(kBarHeight, static_cast<uint32_t>(overlay_.barHeight));
      return true;
    case Part::Sprite:
      return spyro_field_collectables_commit(&core, collectables_);
    }
    return false;
  }

  const Overlay &overlay() const {
    return overlay_;
  }

  // The endpoint the temporal source retains. It is this frame's overlay, captured even when the
  // overlay published nothing, because "this frame drew no 2D layer" is the fact a pair needs.
  Overlay endpoint() const {
    return overlay_;
  }

  // The guest globals this layer reads. 0x80075918 g_Fade, 0x8007570C g_ScreenBorderEnabled,
  // 0x800756C0 the border bar height, 0x800756CC g_DeltaTime; the HUD block is read by
  // fx_field_collectables' own lens, which already names 0x80077FA8 g_Hud, 0x8007840C m_SpriteRect,
  // 0x8007850C m_OrbAndEggSprite and 0x800770F4 D_800770C8.m_specularTime.
  static constexpr uint32_t kFade = 0x80075918u;
  static constexpr uint32_t kBorderEnabled = 0x8007570Cu;
  static constexpr uint32_t kBarHeight = 0x800756C0u;
  static constexpr uint32_t kDeltaTime = 0x800756CCu;

private:
  static State readState(Core &core, int32_t drawOffsetX, int32_t drawOffsetY, int32_t renderWidth);

  State state_{};
  Overlay overlay_{};
  field_collectables_recipe::Recipe collectables_{};
};

// The temporal source. The endpoint lifecycle and the consecutive-frame admission rule are
// `spyro::temporal::Pair`; the pairing is `spyro::instance_pairing`; the identity rule and the
// sampler are the overlay's own. It owns nothing any other layer owns.
class History {
public:
  void begin(uint64_t scene, bool reference, bool active) {
    pair_.begin(scene, reference, active);
  }
  void retain(Overlay endpoint) {
    pair_.retain(std::move(endpoint));
  }
  void refuse() {
    pair_.refuse();
  }
  void rotate() {
    pair_.rotate();
  }
  bool paired() const {
    return pair_.paired();
  }
  bool eligible() const {
    return pair_.eligible();
  }
  void admit(bool eligible) {
    pair_.admit(eligible);
  }
  uint64_t frameSerial() const {
    return pair_.serial();
  }
  const Overlay *previous() const {
    return pair_.previous();
  }
  const Overlay *current() const {
    return pair_.current();
  }

  // Reconstruct the whole overlay at `t` and publish it into `target`. Returns the same stage
  // vocabulary every other layer uses, so a refusal reads the same everywhere.
  actor_stage::Temporal emit(Core &core, RenderQueue &target, double t, Census &census) const;

private:
  temporal::Pair<Overlay> pair_;
};

// ── The impure half, inline because this repository lists its translation units explicitly
// ───────── (CMakeLists.txt names every game/render/*.cpp, so a new .cpp needs a build-file edit
// this change deliberately does not make. The recipe above is header-only for the same reason and
// because it is pure; this half is small enough that inlining it costs nothing measurable and keeps
// ONE queue path per producer rather than a second copy of it behind a build-file edit.)

inline State
Frame::readState(Core &core, int32_t drawOffsetX, int32_t drawOffsetY, int32_t renderWidth) {
  State state;
  state.fade = core.mem_r32(kFade);
  state.borderEnabled = core.mem_r32(kBorderEnabled);
  state.barHeight = static_cast<int32_t>(core.mem_r32(kBarHeight));
  state.deltaTime = static_cast<int32_t>(core.mem_r32(kDeltaTime));
  state.drawOffsetX = drawOffsetX;
  state.drawOffsetY = drawOffsetY;
  state.renderWidth = renderWidth;
  state.collectables = spyro_field_collectables_read(&core);
  return state;
}

inline actor_stage::Temporal
History::emit(Core &core, RenderQueue &target, double t, Census &census) const {
  census = {};
  if (!paired()) {
    return actor_stage::Temporal::NoEndpoints;
  }
  Overlay sampled = *current();
  if (field_2d_overlay_recipe::interpolate(sampled, *previous(), t, census) != Status::Ready) {
    return actor_stage::Temporal::Recipe;
  }
  // One part at a time, in the guest's own publication order, through the same submitters the logic
  // frame uses. A refusal here is a programming error at the call site, which must have preflighted
  // the same recipes: the admission interval runs this exact path.
  for (const Part part : {Part::Sprite, Part::Fade, Part::Border}) {
    if (!publish(core, target, part, sampled)) {
      return actor_stage::Temporal::Submission;
    }
  }
  const auto worst = census.worstMismatch();
  lucent::debug("field2dtemporal",
                "emit t={} draws={} interpolated={} unattributed={} absent={} incompatible={} "
                "refused={} worst_mismatch={}x{}",
                t,
                census.actors,
                census.interpolated,
                census.unattributed,
                census.absent,
                census.incompatible,
                census.refused,
                field_2d_overlay_recipe::mismatchName(worst),
                census.mismatches[static_cast<size_t>(worst)]);
  return actor_stage::Temporal::Ready;
}

} // namespace spyro::field_2d_overlay
