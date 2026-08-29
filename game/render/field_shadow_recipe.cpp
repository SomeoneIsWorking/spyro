#include "field_shadow_recipe.h"

#include "actor_transform_math.h"
#include "core.h"
#include "game.h"
#include "proj_params.h"
#include "world_projection_math.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace spyro::field_shadow_recipe {
namespace {

constexpr std::uint32_t kSpyro = 0x80078A58u;
constexpr std::uint32_t kCamera = 0x80076DD0u;
constexpr std::uint32_t kModels = 0x80076378u;
constexpr std::uint32_t kShadowState = 0x8007AA10u;
constexpr std::uint32_t kDirectionTable = 0x8006E268u;

bool span(std::uint32_t address, std::uint32_t bytes) {
  const std::uint32_t mapped = address & 0x1fffffffu;
  if (mapped < 0x800000u) {
    const std::uint32_t offset = mapped & 0x1fffffu;
    return bytes <= 0x200000u - offset;
  }
  return mapped >= 0x1f800000u && mapped <= 0x1f800400u - bytes;
}

bool frameData(Core *core,
               std::uint32_t modelBase,
               std::uint32_t animation,
               std::uint32_t frame,
               std::uint32_t &out) {
  // The branch-delay load before both animation paths dereferences g_Models. The resulting model
  // set is then indexed by animation before its animation-table pointer is read at record+0x38.
  const std::uint32_t model = modelBase + animation * 4u;
  if (!span(model + 0x38u, 4u)) {
    return false;
  }
  const std::uint32_t animationTable = core->mem_r32(model + 0x38u);
  if (!span(animationTable, 0x28u) || !span(animationTable + 0x24u + frame * 4u, 4u)) {
    return false;
  }
  const std::uint32_t frameRef = core->mem_r32(animationTable + 0x24u + frame * 4u);
  out = (frameRef << 11u) >> 10u;
  return span(out, 8u);
}

bool loadFrame(Core *core, std::uint32_t address, std::array<std::uint8_t, 8> &out) {
  if (!span(address, 8u)) {
    return false;
  }
  for (std::size_t i = 0; i < out.size(); ++i) {
    out[i] = core->mem_r8(address + (std::uint32_t)i);
  }
  return true;
}

std::array<std::uint8_t, 16> expandUnsigned(const std::array<std::uint8_t, 8> &raw) {
  std::array<std::uint8_t, 16> out{};
  for (std::size_t i = 0; i < raw.size(); ++i) {
    out[i * 2u] = raw[i];
    out[i * 2u + 1u] = (std::uint8_t)(((std::uint32_t)raw[i] + raw[(i + 1u) & 7u]) >> 1u);
  }
  return out;
}

std::array<std::int8_t, 16> expandSigned(const std::array<std::int8_t, 8> &raw) {
  std::array<std::int8_t, 16> out{};
  for (std::size_t i = 0; i < raw.size(); ++i) {
    out[i * 2u] = raw[i];
    out[i * 2u + 1u] = (std::int8_t)(((std::int32_t)raw[i] + raw[(i + 1u) & 7u]) >> 1u);
  }
  return out;
}

std::array<std::int8_t, 8> readSignedBytes(Core *core, std::uint32_t address) {
  std::array<std::int8_t, 8> out{};
  for (std::size_t i = 0; i < out.size(); ++i) {
    out[i] = (std::int8_t)core->mem_r8(address + (std::uint32_t)i);
  }
  return out;
}

psxport::native_projection::NativeProjectedVertex
projectPoint(const actor_transform_math::Matrix &orientation,
             const psxport::native_projection::ProjectionParams &projection,
             const std::array<std::int32_t, 3> &translation,
             std::int32_t radial,
             std::int32_t lift,
             std::int32_t directionLow,
             std::int32_t directionHigh) {
  const auto input = world_projection_math::packProjectionInput(
      (radial * directionHigh) >> 10u, lift * 4, (radial * directionLow) >> 10u);
  psxport::native_projection::FixedAffine affine{};
  affine.m = orientation.value;
  affine.t = translation;
  return psxport::native_projection::project(affine, projection, input);
}

} // namespace

std::int32_t
otBin(std::uint16_t firstSz, std::uint16_t secondSz, std::uint16_t anchorSz, std::int32_t bias) {
  const std::uint32_t pair = (std::uint32_t)firstSz + secondSz;
  return (std::int32_t)((pair + (pair >> 1u) + anchorSz) >> 9u) - bias;
}

std::uint8_t interpolateRadius(std::uint8_t current, std::uint8_t next, std::uint8_t progress) {
  // The retained GPF/GPL sequence uses IR0=(16-progress), then IR0=progress, and shifts the
  // resulting MAC by four when publishing the byte.
  const std::int32_t value =
      ((16 - (std::int32_t)progress) * current + (std::int32_t)progress * next) >> 4;
  return (std::uint8_t)value;
}

Recipe derive(Core *core) {
  Recipe recipe{};
  if (core == nullptr || core->game == nullptr) {
    recipe.status = Status::InvalidCore;
    return recipe;
  }
  if (!span(kSpyro, 0x178u) || !span(kCamera, 0x34u) || !span(kShadowState, 0x28u) ||
      !span(kDirectionTable, kFanPoints * 4u)) {
    recipe.status = Status::InvalidState;
    return recipe;
  }
  if (core->mem_r32(kShadowState + 0x24u) != 0u) {
    recipe.status = Status::ValidEmpty;
    return recipe;
  }
  if (!span(kModels, 4u)) {
    recipe.status = Status::InvalidState;
    return recipe;
  }
  const std::uint32_t modelBase = core->mem_r32(kModels);
  if (!span(modelBase, 0x438u)) {
    recipe.status = Status::InvalidState;
    return recipe;
  }

  const std::uint8_t progress = core->mem_r8(kSpyro + 0x24u);
  const std::uint16_t animationPair = core->mem_r16(kSpyro + 0x18u);
  const std::uint16_t framePair = core->mem_r16(kSpyro + 0x1eu);
  std::uint32_t currentData = 0;
  if (!frameData(core, modelBase, animationPair & 0xffu, framePair & 0xffu, currentData)) {
    recipe.status = Status::InvalidState;
    return recipe;
  }
  std::array<std::uint8_t, 8> current{};
  if (!loadFrame(core, currentData, current)) {
    recipe.status = Status::InvalidState;
    return recipe;
  }
  std::array<std::uint8_t, 8> raw = current;
  if (progress != 0u) {
    std::uint32_t nextData = 0;
    if (!frameData(
            core, modelBase, (animationPair >> 8) & 0xffu, (framePair >> 8) & 0xffu, nextData)) {
      recipe.status = Status::InvalidState;
      return recipe;
    }
    std::array<std::uint8_t, 8> next{};
    if (!loadFrame(core, nextData, next)) {
      recipe.status = Status::InvalidState;
      return recipe;
    }
    for (std::size_t i = 0; i < raw.size(); ++i) {
      raw[i] = interpolateRadius(current[i], next[i], progress);
    }
  }

  const auto radius = expandUnsigned(raw);
  const auto ringHeights = expandSigned(readSignedBytes(core, kShadowState));
  bool shrink = false;
  for (std::size_t i = 0; i < 8u; ++i) {
    shrink |= core->mem_r8(kShadowState + 8u + (std::uint32_t)i) != 0u;
  }
  const auto camera = actor_transform_math::readCameraMatrix(core);
  // The shadow body rotates the camera's X/Z columns by Spyro's standalone yaw byte. The shared
  // Moby helper encodes that rotation in packed-angle bits 16..23, which makes its Y-rotation
  // branch use the same sine-table offset (yaw << 1) as the retained body.
  const auto orientation = actor_transform_math::rotateForMoby(
      core, camera, (std::uint32_t)core->mem_r8(kSpyro + 0x0eu) << 16u);
  psxport::native_projection::ProjectionParams projection{};
  // 0x80059A48 does not program OFX/OFY/H itself. It consumes the live GTE controls left by the
  // preceding actor pass, so the recipe must use that same per-call state instead of the game's
  // persistent SetGeom values or a widescreen replacement.
  projection.ofx = (std::int32_t)core->game->gte.REG[56];
  projection.ofy = (std::int32_t)core->game->gte.REG[57];
  projection.h = (std::uint16_t)core->game->gte.REG[58];
  if (!core->rsub.projParams.geomValid() || projection.h == 0u) {
    recipe.status = Status::InvalidProjection;
    return recipe;
  }

  const std::int32_t cameraX = (std::int32_t)core->mem_r32(kCamera + 0x28u);
  const std::int32_t cameraY = (std::int32_t)core->mem_r32(kCamera + 0x2cu);
  const std::int32_t cameraZ = (std::int32_t)core->mem_r32(kCamera + 0x30u);
  const auto anchorInput = world_projection_math::packProjectionInput(
      cameraY - (std::int32_t)core->mem_r32(kShadowState + 0x14u),
      cameraZ - (std::int32_t)core->mem_r32(kShadowState + 0x18u),
      (std::int32_t)core->mem_r32(kShadowState + 0x10u) - cameraX);
  psxport::native_projection::FixedAffine cameraAffine{};
  cameraAffine.m = camera.value;
  const auto anchor = psxport::native_projection::project(cameraAffine, projection, anchorInput);
  if ((anchor.flags & 0x80000000u) != 0u) {
    recipe.status = Status::InvalidProjection;
    return recipe;
  }
  const std::int32_t bias = (std::int32_t)core->mem_r32(kShadowState + 0x1cu);
  const std::array<std::int32_t, 3> shadowTranslation = {
      (std::int32_t)(anchor.raw_view_fixed[0] >> 12u),
      (std::int32_t)(anchor.raw_view_fixed[1] >> 12u),
      (std::int32_t)(anchor.raw_view_fixed[2] >> 12u)};
  for (std::size_t i = 0; i < kFanPoints; ++i) {
    const std::uint32_t direction = core->mem_r32(kDirectionTable + (std::uint32_t)i * 4u);
    const std::int16_t directionLow = (std::int16_t)direction;
    const std::int16_t directionHigh = (std::int16_t)(direction >> 16);
    const std::int32_t lift = shrink ? ((std::int32_t)ringHeights[i] * 3) >> 2 : ringHeights[i];
    const std::int32_t radial = shrink ? ((std::int32_t)radius[i] * 3) >> 2 : radius[i];
    const std::size_t next = (i + 1u) & (kFanPoints - 1u);
    const auto point = projectPoint(
        orientation, projection, shadowTranslation, radial, lift, directionLow, directionHigh);
    const std::uint32_t nextDirection = core->mem_r32(kDirectionTable + (std::uint32_t)next * 4u);
    const std::int32_t nextLift =
        shrink ? ((std::int32_t)ringHeights[next] * 3) >> 2 : ringHeights[next];
    const std::int32_t nextRadial = shrink ? ((std::int32_t)radius[next] * 3) >> 2 : radius[next];
    const auto nextPoint = projectPoint(orientation,
                                        projection,
                                        shadowTranslation,
                                        nextRadial,
                                        nextLift,
                                        (std::int16_t)nextDirection,
                                        (std::int16_t)(nextDirection >> 16));
    const std::uint16_t nextSz = nextPoint.sz;
    const std::int32_t bucket = otBin(point.sz, nextSz, anchor.sz, bias);
    if (bucket < 0) {
      continue;
    }
    Face &face = recipe.faces[recipe.faceCount++];
    face.otBin = (std::uint16_t)std::min(bucket, 0xffff);
    face.fanOrdinal = (std::uint32_t)i;
    face.vertices[0] = {(std::int16_t)anchor.sx,
                        (std::int16_t)anchor.sy,
                        anchor.px,
                        anchor.py,
                        anchor.pz,
                        anchor.sz};
    face.vertices[1] = {
        (std::int16_t)point.sx, (std::int16_t)point.sy, point.px, point.py, point.pz, point.sz};
    face.vertices[2] = {(std::int16_t)nextPoint.sx,
                        (std::int16_t)nextPoint.sy,
                        nextPoint.px,
                        nextPoint.py,
                        nextPoint.pz,
                        nextPoint.sz};
  }
  recipe.status = recipe.faceCount == 0u ? Status::ValidEmpty : Status::Ready;
  return recipe;
}

const char *statusName(Status status) {
  switch (status) {
  case Status::Ready:
    return "ready";
  case Status::ValidEmpty:
    return "valid empty";
  case Status::InvalidCore:
    return "invalid core";
  case Status::InvalidState:
    return "invalid state";
  case Status::InvalidProjection:
    return "invalid projection";
  }
  return "unknown";
}

} // namespace spyro::field_shadow_recipe
