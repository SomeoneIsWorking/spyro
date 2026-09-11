#include "level_transition_scene.h"

#include "actor_transform_math.h"
#include "core.h"
#include "cutscene_scene_recipe.h"
#include "fx_field_actor_composition.h"
#include "fx_paired_actor.h"
#include "guest_call.h"
#include "guest_trig.h"
#include "level_transition_tally_recipe.h"
#include "spyro_game.h"

namespace spyro::level_transition_scene {
namespace {

using actor_transform_math::Matrix;

// func_8001A050's own globals, decoded from the shipping executable with tools/re_globals.py.
constexpr std::uint32_t kSonyImage = 0x8006FCF4u;
constexpr std::uint32_t kSonyImageBytes = 0x900u;
// g_SonyImage.m_ShadedMobys — include/sony_image.h: 0x1C00 of draw state, then two 256-entry world
// queues. The 0x900 clear above covers only m_Draw.m_Moby, so this head is the ONLY thing that ends
// the previous frame's shaded queue; without it the field's 256 stale entries are still there and
// the queue reads as unterminated.
constexpr std::uint32_t kShadedMobyQueue = kSonyImage + 0x2400u;
constexpr std::uint32_t kLevelTransHudActive = 0x800756B0u;
constexpr std::uint32_t kEntranceSweep = 0x80075910u;
constexpr std::uint32_t kCamera = 0x80076DD0u;
constexpr std::uint32_t kCameraProjection = 0x00u;
constexpr std::uint32_t kCameraView = 0x14u;
constexpr std::uint32_t kCameraRotationX = 0x4Cu;
constexpr std::uint32_t kCameraRotationY = 0x4Eu;
constexpr std::uint32_t kCameraRotationZ = 0x50u;
constexpr std::uint32_t kCycloramaSectorCount = 0x80078A40u;
constexpr std::uint32_t kCopyHudMobys = 0x80018880u;

constexpr std::int16_t kOne = 4096;
// The aspect correction the substitute projection applies to its middle row, and nowhere else.
constexpr std::int32_t kAspectNumerator = 320;
constexpr std::int32_t kAspectDenominator = 512;

Matrix multiply(const Matrix &left, const Matrix &right) {
  Matrix out{};
  for (std::size_t row = 0; row < 3; ++row) {
    for (std::size_t column = 0; column < 3; ++column) {
      std::int32_t sum = 0;
      for (std::size_t k = 0; k < 3; ++k) {
        sum += (std::int32_t)left.value[row][k] * (std::int32_t)right.value[k][column];
      }
      out.value[row][column] = (std::int16_t)(sum >> 12);
    }
  }
  return out;
}

std::int16_t sin16(Core *core, std::int32_t angle) {
  return (std::int16_t)guest_trig::sine(core, angle);
}

std::int16_t cos16(Core *core, std::int32_t angle) {
  return (std::int16_t)guest_trig::cosine(core, angle);
}

} // namespace

SkyMatrices sweepSkyMatrices(Core *core, std::int32_t residual) {
  const std::int32_t rx = core->mem_r16s(kCamera + kCameraRotationX);
  const std::int32_t ry = core->mem_r16s(kCamera + kCameraRotationY);
  const std::int32_t rz = core->mem_r16s(kCamera + kCameraRotationZ);

  // The sweep is subtracted from the Y angle alone — it is the entrance's own turn, not a whole
  // extra orientation — and that angle drives the X block of the first matrix.
  const std::int16_t sy = sin16(core, ry - residual);
  const std::int16_t cy = cos16(core, ry - residual);
  Matrix view{};
  view.value[0][0] = kOne;
  view.value[1][1] = cy;
  view.value[2][1] = sy;
  view.value[1][2] = (std::int16_t)-sy;
  view.value[2][2] = cy;

  Matrix about{};
  const std::int16_t sz = sin16(core, rz);
  const std::int16_t cz = cos16(core, rz);
  about.value[0][0] = cz;
  about.value[2][0] = (std::int16_t)-sz;
  about.value[1][1] = kOne;
  about.value[0][2] = sz;
  about.value[2][2] = cz;
  view = multiply(view, about);

  about = Matrix{};
  const std::int16_t sx = sin16(core, rx);
  const std::int16_t cx = cos16(core, rx);
  about.value[0][0] = cx;
  about.value[1][0] = (std::int16_t)-sx;
  about.value[0][1] = sx;
  about.value[1][1] = cx;
  about.value[2][2] = kOne;
  view = multiply(view, about);

  // The projection is the view with its middle row narrowed for the aspect, and nothing else.
  Matrix projection = view;
  for (std::size_t column = 0; column < 3; ++column) {
    projection.value[1][column] =
        (std::int16_t)(((std::int32_t)view.value[1][column] * kAspectNumerator) /
                       kAspectDenominator);
  }
  return {actor_transform_math::packMatrix(view, 0),
          actor_transform_math::packMatrix(projection, 0)};
}

std::int32_t advanceEntranceSweep(Core *core) {
  const std::int32_t residual = (std::int32_t)core->mem_r32(kEntranceSweep);
  const std::int32_t wound = residual - 2 < 0 ? 0 : residual - 2;
  core->mem_w32(kEntranceSweep, (std::uint32_t)wound);
  return wound;
}

Refusal submit(Core *core) {
  // The guest clears the whole of g_SonyImage before any producer runs, so a previous screen's
  // lists cannot leak into this one.
  for (std::uint32_t offset = 0; offset < kSonyImageBytes; offset += 4u) {
    core->mem_w32(kSonyImage + offset, 0u);
  }
  if (core->mem_r32(kLevelTransHudActive) != 0u) {
    if (!level_transition_tally::submit(core)) {
      return Refusal::Tally;
    }
    // draw.c:797 — the tally ends by terminating the shaded queue at its head, then 0x80018880
    // appends the HUD moby range it just built and 0x80022A2C draws it. All three belong to the
    // tally and not to the stage: a cancelled tally does none of them.
    core->mem_w32(kShadedMobyQueue, 0u);
    psx::cpu::dispatchGuestToReturn0(*core,
                                     kCopyHudMobys,
                                     psx::cpu::ExecutionBudget::currentTurn(*core),
                                     "transition-hud-mobys");
    if (!spyro_field_actor_composition_submit(core, {.secondary = false, .shaded = true})) {
      return Refusal::ShadedActors;
    }
  }
  if (!spyro_paired_actor_submit_field(core, spyro_paired_actor_state(core))) {
    return Refusal::SpyroActor;
  }
  if (core->mem_r32(kCycloramaSectorCount) == 0u) {
    return Refusal::None;
  }
  const std::int32_t residual = advanceEntranceSweep(core);
  if (residual == 0) {
    if (!spyro_terrain_submit(core, -1, kCamera + kCameraView, kCamera + kCameraProjection)) {
      return Refusal::Sky;
    }
    return Refusal::None;
  }
  const SkyMatrices matrices = sweepSkyMatrices(core, residual);
  if (!spyro_terrain_submit_matrices(core, -1, matrices.view, matrices.projection)) {
    return Refusal::Sky;
  }
  return Refusal::None;
}

const char *refusalName(Refusal refusal) {
  switch (refusal) {
  case Refusal::None:
    return "none";
  case Refusal::Tally:
    return "level-transition tally 0x8001973C refused its atomic recipe";
  case Refusal::ShadedActors:
    return "shaded actor producer 0x80022A2C refused its atomic recipe";
  case Refusal::SpyroActor:
    return "Spyro actor producer 0x80023AC4 refused its atomic recipe";
  case Refusal::Sky:
    return "cyclorama producer 0x8004EBA8 refused its atomic recipe";
  }
  return "unknown";
}

} // namespace spyro::level_transition_scene
