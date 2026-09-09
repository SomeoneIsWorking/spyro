#include "moby_shadow_recipe.h"

#include "actor_transform_math.h"
#include "core.h"
#include "proj_params.h"
#include "world_projection_math.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace spyro::moby_shadow_recipe {
namespace {

// Addresses recovered from 0x80059F8C's own instruction encodings; see docs/issues/0103.
constexpr std::uint32_t kShadowList = 0x800724F4u;  // D_8006FCF4 + 0x2800
constexpr std::uint32_t kMobyShadows = 0x80075EF8u; // +0/+4 UVs, +8 the list-end cursor
constexpr std::uint32_t kCamera = 0x80076DD0u;
constexpr std::uint32_t kMobyShadowDistance = 0x1Cu;
constexpr std::uint32_t kMobyDepthOffset = 0x47u;
constexpr std::uint32_t kFarViewZ = 0x1000u;
constexpr std::uint32_t kFadeViewZ = 0xC00u;
constexpr std::int32_t kNudgeViewZ = 0x400;
constexpr std::uint32_t kOtShift = 7u;
// A shadow list longer than this is a corrupt cursor, not a busy frame; the level Moby array it is
// built from is itself bounded well below this.
constexpr std::uint32_t kMaxEntries = 4096u;

bool span(std::uint32_t address, std::uint32_t bytes) {
  const std::uint32_t mapped = address & 0x1fffffffu;
  if (mapped >= 0x800000u) {
    return false;
  }
  const std::uint32_t offset = mapped & 0x1fffffu;
  return bytes <= 0x200000u - offset;
}

// The screen-space cross product NCLIP computes. Negative means the fan winds away from the viewer
// and retail drops the whole shadow rather than the individual triangle.
std::int64_t nclip(const std::array<psxport::native_projection::NativeProjectedVertex, 3> &ring) {
  const std::int64_t x0 = ring[0].sx, y0 = ring[0].sy;
  const std::int64_t x1 = ring[1].sx, y1 = ring[1].sy;
  const std::int64_t x2 = ring[2].sx, y2 = ring[2].sy;
  return x0 * (y1 - y2) + x1 * (y2 - y0) + x2 * (y0 - y1);
}

} // namespace

std::int32_t otBin(std::uint16_t firstSz,
                   std::uint16_t secondSz,
                   std::uint16_t anchorSz,
                   std::int32_t bias,
                   std::uint32_t shift) {
  const std::uint32_t pair = (std::uint32_t)firstSz + secondSz;
  return (std::int32_t)((pair + (pair >> 1u) + anchorSz) >> shift) - bias;
}

std::uint8_t distanceGrey(std::int32_t anchorViewZ) {
  if (anchorViewZ < (std::int32_t)kFadeViewZ) {
    return 0x80u;
  }
  // Retail negates the overshoot, adds 0x400 and shifts by three, so the ramp reaches zero exactly
  // at the 0x1000 rejection limit that has already been applied above this call.
  const std::int32_t ramp = ((std::int32_t)kFadeViewZ - anchorViewZ + 0x400) >> 3;
  return (std::uint8_t)std::clamp(ramp, 0, 0xff);
}

Recipe derive(Core *core) {
  Recipe recipe{};
  if (core == nullptr || core->game == nullptr) {
    recipe.status = Status::InvalidCore;
    return recipe;
  }
  if (!span(kMobyShadows, 12u) || !span(kCamera, 0x34u)) {
    recipe.status = Status::InvalidState;
    return recipe;
  }
  const std::uint32_t listEnd = core->mem_r32(kMobyShadows + 8u);
  if (listEnd < kShadowList || ((listEnd - kShadowList) & 7u) != 0u ||
      (listEnd - kShadowList) / 8u > kMaxEntries || !span(kShadowList, listEnd - kShadowList)) {
    recipe.status = Status::InvalidState;
    return recipe;
  }
  recipe.entries = (listEnd - kShadowList) / 8u;
  if (recipe.entries == 0u) {
    recipe.status = Status::ValidEmpty;
    return recipe;
  }

  const auto &geometry = core->rsub.projParams;
  if (!geometry.geomValid()) {
    recipe.status = Status::InvalidProjection;
    return recipe;
  }
  psxport::native_projection::ProjectionParams projection{};
  projection.ofx = (std::int32_t)((std::uint32_t)(std::int32_t)geometry.geomOfx() << 16u);
  projection.ofy = (std::int32_t)((std::uint32_t)(std::int32_t)geometry.geomOfy() << 16u);
  projection.h = (std::uint16_t)(std::int32_t)geometry.geomH();
  if (projection.h == 0u) {
    recipe.status = Status::InvalidProjection;
    return recipe;
  }

  const auto camera = actor_transform_math::readCameraMatrix(core);
  const std::int32_t cameraX = (std::int32_t)core->mem_r32(kCamera + 0x28u) >> 2;
  const std::int32_t cameraY = (std::int32_t)core->mem_r32(kCamera + 0x2Cu) >> 2;
  const std::int32_t cameraZ = (std::int32_t)core->mem_r32(kCamera + 0x30u) >> 2;

  const auto reject = [&recipe](Reject reason) {
    ++recipe.rejects[(std::size_t)reason];
  };

  for (std::uint32_t entry = 0; entry < recipe.entries; ++entry) {
    const std::uint32_t record = kShadowList + entry * 8u;
    const std::uint32_t moby = core->mem_r32(record);
    const std::int32_t radius = (std::int32_t)core->mem_r32(record + 4u);
    if (!span(moby, 0x58u)) {
      recipe.status = Status::InvalidState;
      return recipe;
    }
    const std::uint32_t shadowWord = core->mem_r32(moby + kMobyShadowDistance);
    const std::uint32_t plane = shadowWord & 0xffffu;
    if (plane == 0u) {
      reject(Reject::NoShadowPlane);
      continue;
    }
    // The shadow sits on its own plane height, not on the Moby's Z, and the camera components are
    // already quarter-scale here while that plane height is stored at that scale.
    const auto anchorInput = world_projection_math::packProjectionInput(
        cameraY - ((std::int32_t)core->mem_r32(moby + 0x10u) >> 2),
        cameraZ - (std::int32_t)plane,
        ((std::int32_t)core->mem_r32(moby + 0x0Cu) >> 2) - cameraX);
    psxport::native_projection::FixedAffine cameraAffine{};
    cameraAffine.m = camera.value;
    const auto anchor = psxport::native_projection::project(cameraAffine, projection, anchorInput);
    const std::int32_t anchorViewZ = (std::int32_t)(anchor.raw_view_fixed[2] >> 12);
    if (anchorViewZ >= (std::int32_t)kFarViewZ) {
      reject(Reject::BehindCamera);
      continue;
    }

    // The two plane rotations are the same column replacements the shared Moby helper performs, at
    // the sine table's eight-byte stride. Retail's own recombination drops R12 when only the first
    // one runs (0x8005A158 adds the SECOND angle, which is zero exactly when its block is skipped),
    // so that lane is cleared here rather than normalised away.
    auto oriented = camera;
    const std::uint32_t pitch = ((shadowWord >> 16) & 0x3fu) * 8u;
    const std::uint32_t roll = ((shadowWord >> 22) & 0x3fu) * 8u;
    if (pitch != 0u) {
      oriented =
          actor_transform_math::rotateAxis(core, oriented, actor_transform_math::Axis::X, pitch);
      oriented.value[0][1] = 0;
    }
    if (roll != 0u) {
      oriented =
          actor_transform_math::rotateAxis(core, oriented, actor_transform_math::Axis::Z, roll);
    }

    psxport::native_projection::FixedAffine planeAffine{};
    planeAffine.m = oriented.value;
    planeAffine.t = {(std::int32_t)(anchor.raw_view_fixed[0] >> 12),
                     (std::int32_t)(anchor.raw_view_fixed[1] >> 12),
                     anchorViewZ};
    const std::array<psxport::native_projection::ModelVertex, kFanPoints> ringModel = {
        psxport::native_projection::ModelVertex{0, 0, (std::int16_t)radius},
        psxport::native_projection::ModelVertex{(std::int16_t)radius, 0, 0},
        psxport::native_projection::ModelVertex{0, 0, (std::int16_t)-radius},
        psxport::native_projection::ModelVertex{(std::int16_t)-radius, 0, 0}};
    std::array<psxport::native_projection::NativeProjectedVertex, kFanPoints> ring{};
    for (std::size_t i = 0; i < kFanPoints; ++i) {
      ring[i] = psxport::native_projection::project(planeAffine, projection, ringModel[i]);
    }
    if (nclip({ring[0], ring[1], ring[2]}) < 0) {
      reject(Reject::Backfacing);
      continue;
    }
    if (anchor.sy <= 0 || anchor.sy >= 240 || anchor.sx <= -8 || anchor.sx >= 0x208) {
      reject(Reject::OffScreen);
      continue;
    }

    // Retail nudges each ring vertex one pixel toward or away from the anchor by depth, including
    // the wrap copy, so the closing triangle agrees with the opening one.
    std::array<std::int16_t, kFanPoints> nudgedY{};
    for (std::size_t i = 0; i < kFanPoints; ++i) {
      nudgedY[i] = ring[i].sy;
      if (anchorViewZ > kNudgeViewZ) {
        if (ring[i].sz < anchor.sz) {
          nudgedY[i] = (std::int16_t)(nudgedY[i] + 1);
        } else if (ring[i].sz > anchor.sz) {
          nudgedY[i] = (std::int16_t)(nudgedY[i] - 1);
        }
      }
    }

    const std::int32_t bias = (std::int32_t)(std::int8_t)core->mem_r8(moby + kMobyDepthOffset) - 1;
    const std::uint8_t grey = distanceGrey(anchorViewZ);
    const auto anchorVertex = Vertex{(std::int16_t)anchor.sx,
                                     (std::int16_t)anchor.sy,
                                     anchor.px,
                                     anchor.py,
                                     anchor.pz,
                                     anchor.sz};
    bool emitted = false;
    for (std::size_t i = 0; i < kFanPoints; ++i) {
      const std::size_t next = (i + 1u) % kFanPoints;
      const std::int32_t bucket = otBin(ring[i].sz, ring[next].sz, anchor.sz, bias, kOtShift);
      if (bucket < 0) {
        continue;
      }
      Face face{};
      face.otBin = (std::uint16_t)std::min(bucket, 0xffff);
      face.fanOrdinal = (std::uint32_t)i;
      face.moby = moby;
      face.grey = grey;
      face.vertices[0] = anchorVertex;
      face.vertices[1] = {
          (std::int16_t)ring[i].sx, nudgedY[i], ring[i].px, ring[i].py, ring[i].pz, ring[i].sz};
      face.vertices[2] = {(std::int16_t)ring[next].sx,
                          nudgedY[next],
                          ring[next].px,
                          ring[next].py,
                          ring[next].pz,
                          ring[next].sz};
      recipe.faces.push_back(face);
      emitted = true;
    }
    if (emitted) {
      ++recipe.drawn;
    } else {
      reject(Reject::NegativeBin);
    }
  }
  recipe.status = recipe.faces.empty() ? Status::ValidEmpty : Status::Ready;
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

} // namespace spyro::moby_shadow_recipe
