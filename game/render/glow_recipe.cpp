#include "glow_recipe.h"

#include "actor_transform_math.h"
#include "core.h"
#include "proj_params.h"

#include <algorithm>

namespace spyro::glow_recipe {
namespace {

// Addresses recovered from 0x800580F4's own instruction encodings.
constexpr std::uint32_t kGlows = 0x80078800u;
constexpr std::uint32_t kCamera = 0x80076DD0u;
// Record layout, all measured at the loads in the loop body.
constexpr std::uint32_t kCount = 0x00u;    // ring points; zero switches the glow off
constexpr std::uint32_t kRing = 0x04u;     // pairs of screen-space direction words, stride 8
constexpr std::uint32_t kPosition = 0x08u; // pointer to the world position the glow follows
constexpr std::uint32_t kColour = 0x0Cu;
constexpr std::uint32_t kRadius = 0x10u;
constexpr std::uint32_t kOffset = 0x14u; // world offset added to the followed position
constexpr std::uint32_t kBias = 0x20u;   // ordering-table bias, biasing TOWARD the viewer
constexpr std::int32_t kMaxShift = 4;
constexpr std::uint32_t kShiftScale = 13u;
constexpr std::uint32_t kOtShift = 7u;
constexpr std::int32_t kOtFarStep = 0x40;
constexpr std::int32_t kOtLastBin = 0x7FF;

bool span(std::uint32_t address, std::uint32_t bytes) {
  const std::uint32_t mapped = address & 0x1fffffffu;
  if (mapped >= 0x800000u) {
    return false;
  }
  const std::uint32_t offset = mapped & 0x1fffffu;
  return bytes <= 0x200000u - offset;
}

} // namespace

std::uint32_t outcode(std::int32_t x, std::int32_t y) {
  std::uint32_t code = 0;
  if (y <= 1) {
    code |= 1u;
  }
  if (y >= 0x100) {
    code |= 2u;
  }
  if (x >= 0x200) {
    code |= 4u;
  }
  if (x <= 0) {
    code |= 8u;
  }
  return code;
}

std::int32_t otBin(std::uint32_t viewZ, std::int32_t bias) {
  // The shift is logical, so a depth that came out negative wraps to the far end of the table
  // rather than sorting in front of everything, which is retail's behaviour and not an accident
  // worth normalising away.
  std::int32_t bin = (std::int32_t)(viewZ >> kOtShift) + bias;
  if (bin <= 0) {
    return bin;
  }
  if (bin > 0xFF) {
    bin += kOtFarStep;
  }
  return std::min(bin, kOtLastBin);
}

Recipe derive(Core *core) {
  Recipe recipe{};
  if (core == nullptr || core->game == nullptr) {
    recipe.status = Status::InvalidCore;
    return recipe;
  }
  if (!span(kGlows, (std::uint32_t)kRecords * kRecordStride) || !span(kCamera, 0x34u)) {
    recipe.status = Status::InvalidState;
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

  psxport::native_projection::FixedAffine affine{};
  affine.m = actor_transform_math::readCameraMatrix(core).value;
  const std::int32_t cameraX = (std::int32_t)core->mem_r32(kCamera + 0x28u);
  const std::int32_t cameraY = (std::int32_t)core->mem_r32(kCamera + 0x2Cu);
  const std::int32_t cameraZ = (std::int32_t)core->mem_r32(kCamera + 0x30u);

  const auto reject = [&recipe](Reject reason) {
    ++recipe.rejects[(std::size_t)reason];
  };

  for (std::size_t index = 0; index < kRecords; ++index) {
    const std::uint32_t record = kGlows + (std::uint32_t)index * kRecordStride;
    const std::uint32_t points = core->mem_r32(record + kCount);
    if (points == 0u) {
      reject(Reject::EmptyRecord);
      continue;
    }
    ++recipe.records;
    const std::uint32_t position = core->mem_r32(record + kPosition);
    const std::uint32_t ring = core->mem_r32(record + kRing);
    if (!span(position, 12u) || !span(ring, points * 8u)) {
      recipe.status = Status::InvalidState;
      return recipe;
    }

    std::int32_t dx =
        (std::int32_t)(core->mem_r32(position) + core->mem_r32(record + kOffset)) - cameraX;
    std::int32_t dy = cameraY - (std::int32_t)(core->mem_r32(position + 4u) +
                                               core->mem_r32(record + kOffset + 4u));
    std::int32_t dz = cameraZ - (std::int32_t)(core->mem_r32(position + 8u) +
                                               core->mem_r32(record + kOffset + 8u));
    // Retail scales the whole delta down before projecting so a distant glow cannot overflow the
    // GTE's 16-bit vector registers, then scales the resulting depth back up. The screen position
    // is unaffected because the perspective divide cancels the same factor.
    std::int32_t shift = 0;
    const std::int32_t manhattan = std::abs(dx) + std::abs(dy) + std::abs(dz);
    if (manhattan > 0) {
      shift = std::min((std::int32_t)((std::uint32_t)manhattan >> kShiftScale), kMaxShift);
      dx >>= shift;
      dy >>= shift;
      dz >>= shift;
    }
    // The guest ORs a masked low half into the packed vector word rather than adding it, so a
    // negative X does NOT borrow into Y here the way it does in the world and actor paths.
    const psxport::native_projection::ModelVertex input{
        (std::int16_t)dy, (std::int16_t)dz, (std::int16_t)dx};
    const auto centre = psxport::native_projection::project(affine, projection, input);
    const std::uint32_t viewZ =
        (std::uint32_t)((std::int32_t)(centre.raw_view_fixed[2] >> 12) << shift);
    if (viewZ == 0u) {
      reject(Reject::NoDepth);
      continue;
    }
    const std::int32_t bin = otBin(viewZ, (std::int32_t)core->mem_r32(record + kBias));
    if (bin <= 0) {
      reject(Reject::NegativeBin);
      continue;
    }

    // IR0 carries the perspective-scaled radius, and each ring direction is multiplied by it, so a
    // glow shrinks with distance without any of its points being a world position of its own.
    const std::int16_t scale =
        (std::int16_t)(((std::int32_t)core->mem_r32(record + kRadius) << 12) / (std::int32_t)viewZ);
    const std::uint32_t colour = core->mem_r32(record + kColour) & 0x00ffffffu;
    const std::uint32_t centreCode = outcode(centre.sx, centre.sy);
    const Vertex centreVertex{
        centre.sx, centre.sy, centre.px, centre.py, centre.pz * (float)(1 << shift)};

    const auto ringVertex = [&](std::uint32_t point) {
      const std::int16_t ir1 = (std::int16_t)core->mem_r32(ring + point * 8u);
      const std::int16_t ir2 = (std::int16_t)core->mem_r32(ring + point * 8u + 4u);
      const std::int32_t x = (std::int32_t)(((std::int64_t)ir1 * scale) >> 12) + centre.sx;
      const std::int32_t y = (std::int32_t)(((std::int64_t)ir2 * scale) >> 12) + centre.sy;
      return std::pair<Vertex, std::uint32_t>{
          Vertex{(std::int16_t)x, (std::int16_t)y, (float)x, (float)y, centreVertex.viewZ},
          outcode(x, y)};
    };

    // The fan is open: N ring points give N-1 triangles, because the direction table already
    // repeats its first entry when the author wanted the halo closed.
    auto previous = ringVertex(0);
    bool emitted = false;
    bool offscreen = false;
    for (std::uint32_t point = 1; point < points; ++point) {
      const auto current = ringVertex(point);
      if ((centreCode & previous.second & current.second) != 0u) {
        offscreen = true;
      } else {
        Face face{};
        face.vertices[0] = centreVertex;
        face.vertices[1] = previous.first;
        face.vertices[2] = current.first;
        face.colour = colour;
        face.otBin = (std::uint16_t)bin;
        face.recordIndex = (std::uint32_t)index;
        face.fanOrdinal = point - 1u;
        recipe.faces.push_back(face);
        emitted = true;
      }
      previous = current;
    }
    if (emitted) {
      ++recipe.drawn;
    } else if (offscreen) {
      reject(Reject::Offscreen);
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
    return "valid-empty";
  case Status::InvalidCore:
    return "invalid-core";
  case Status::InvalidState:
    return "invalid-state";
  case Status::InvalidProjection:
    return "invalid-projection";
  }
  return "unknown";
}

} // namespace spyro::glow_recipe
