// field_2d_overlay.h — the FIELD arm's screen-space 2D layer and its temporal source.
#pragma once

#include "actor_stage.h"
#include "field_2d_overlay_recipe.h"
#include "field_collectables.h"
#include "field_collectables_recipe.h"
#include "screen_border.h"
#include "screen_border_recipe.h"
#include "screen_fade.h"
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

// Each producer's queue-facing recipe, rebuilt from the overlay's flat, pairable draws.
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

// The sprites, in the overlay's own order. The shaded-Moby half is guest state and reaches the
// picture through the world-shaded sprite queue, which has its own temporal source.
inline field_collectables_recipe::Recipe spriteRecipe(const Overlay &overlay) {
  field_collectables_recipe::Recipe recipe;
  for (const auto &draw : overlay.draws) {
    if (draw.part != Part::Sprite) {
      continue;
    }
    // The overlay's coordinates are 32-bit because the border and the fade are laid out against a
    // widened draw area; a sprite rect is the guest's own 16-bit value with the draw offset already
    // folded in.
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

// Publish one part of `overlay` into `target` through the producer's own submitter: the logic frame
// passes this frame's overlay and a reconstruction the sampled one, down one queue path per
// producer.
inline bool publish(Core &core, RenderQueue &target, Part part, const Overlay &overlay) {
  switch (part) {
  case Part::Fade:
    return spyro::screen_fade::submit(&core, target, fadeRecipe(overlay));
  case Part::Border:
    return spyro::screen_border::submit(&core, target, borderRecipe(overlay));
  case Part::Sprite:
    return spyro::field_collectables::submit(&core, target, spriteRecipe(overlay));
  }
  return false;
}

class Frame {
public:
  // Reads the guest's pre-GTE state through the three producers' own lenses and derives the whole
  // overlay; the guest's own commits stay at its own positions, below.
  Status capture(Core &core, int32_t drawOffsetX, int32_t drawOffsetY, int32_t renderWidth) {
    state_ = readState(core, drawOffsetX, drawOffsetY, renderWidth);
    collectables_ = field_collectables_recipe::derive(state_.collectables);
    return field_2d_overlay_recipe::derive(state_, overlay_);
  }

  // The guest's own gate for a part, on the PRE-step state, and the same gate `derive` applied to
  // the endpoint: false means the guest would not have called that producer at all.
  bool armed(Part part) const {
    switch (part) {
    case Part::Fade:
      return overlay_.gates.fade;
    case Part::Border:
      return overlay_.gates.border;
    case Part::Sprite:
      return overlay_.gates.sprites;
    }
    return false;
  }

  // The guest's own guest-state write for `part`, at its own position in the FIELD draw order. A
  // reconstruction never calls this: an in-between picture advances no guest clock and appends
  // nothing to the guest's shaded-Moby queue.
  //
  // The collectables half commits from the recipe derived by the same read as the overlay, so the
  // HUD block is not read twice across a guest write.
  bool commit(Core &core, Part part) const {
    switch (part) {
    case Part::Fade:
      return true; // 0x800190D4 writes no guest state
    case Part::Border:
      core.mem_w32(kBarHeight, static_cast<uint32_t>(overlay_.barHeight));
      return true;
    case Part::Sprite:
      return spyro::field_collectables::commit(&core, collectables_);
    }
    return false;
  }

  const Overlay &overlay() const {
    return overlay_;
  }

  // Retained even when the overlay published nothing: "this frame drew no 2D layer" is a fact a
  // pair needs.
  Overlay endpoint() const {
    return overlay_;
  }

  // The HUD block is read by field_collectables' own lens, which already names its globals.
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

// The temporal source: `spyro::temporal::Pair` owns the endpoint lifecycle,
// `spyro::instance_pairing` the pairing, and the overlay owns the identity rule and the sampler.
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

  // Reconstruct the whole overlay at `t` and publish it into `target`.
  actor_stage::Temporal emit(Core &core, RenderQueue &target, double t, Census &census) const;

private:
  temporal::Pair<Overlay> pair_;
};

// The impure half, inline because CMakeLists.txt names every game/render/*.cpp explicitly.

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
  state.collectables = spyro::field_collectables::read(&core);
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
  // the same recipes.
  for (const Part part : {Part::Sprite, Part::Fade, Part::Border}) {
    if (!publish(core, target, part, sampled)) {
      return actor_stage::Temporal::Submission;
    }
  }
  const auto worst = census.worstMismatch();
  // `parts` and `gates` are both in the order fade/border/sprites, so `parts=0/0/0 gates=0/0/1`
  // reads as the correct null and `gates=1/1/1` as a layer that dropped geometry while the guest
  // was still calling it.
  const auto parts = field_2d_overlay_recipe::countParts(sampled);
  lucent::debug("field2dtemporal",
                "emit t={} draws={} parts={}/{}/{} gates={}{}{} interpolated={} unattributed={} "
                "absent={} incompatible={} refused={} worst_mismatch={}x{}",
                t,
                census.actors,
                parts.fade,
                parts.border,
                parts.sprites,
                sampled.gates.fade ? 1 : 0,
                sampled.gates.border ? 1 : 0,
                sampled.gates.sprites ? 1 : 0,
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
