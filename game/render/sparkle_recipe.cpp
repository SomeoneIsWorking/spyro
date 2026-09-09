#include "sparkle_recipe.h"

#include "actor_transform_math.h"
#include "core.h"
#include "proj_params.h"
#include "world_projection_math.h"

#include <algorithm>

namespace spyro::sparkle_recipe {
namespace {

// Addresses recovered from 0x800584C4's own instruction encodings.
constexpr std::uint32_t kSparkles = 0x80077108u;
constexpr std::uint32_t kCamera = 0x80076DD0u;
// Record layout. The decomp header names +0x0D m_Speed and +0x14/+0x15 m_Fade/m_Size, but the
// renderer uses them as the fade denominator, the size multiplier and the far depth limit, so the
// measured roles are what the code below is written against.
constexpr std::uint32_t kLifeWord = 0x0Cu; // byte 0 life, 1 denominator, 2 angle, 3 signed spin
constexpr std::uint32_t kColour = 0x10u;
constexpr std::uint32_t kSizeWord = 0x14u; // byte 0 size multiplier, byte 1 the far SZ limit
constexpr std::uint32_t kAngleByte = 0x0Eu;
constexpr std::uint16_t kNearSz = 0x80u;
constexpr std::int32_t kOtShift = 5;
constexpr std::int32_t kOtNearPull = 6;
constexpr std::int32_t kOtFarStep = 0x46;
constexpr std::int32_t kOtFarStart = 0x100;
// The corner scale is taken from the view depth but never below this, which is what stops a sparkle
// close to the camera from growing without bound.
constexpr std::int32_t kMinCornerScale = 0x1000;
constexpr std::int32_t kFadeFull = 0x100;
constexpr int kCornerShift = 19;

bool span(std::uint32_t address, std::uint32_t bytes) {
  const std::uint32_t mapped = address & 0x1fffffffu;
  if (mapped >= 0x800000u) {
    return false;
  }
  const std::uint32_t offset = mapped & 0x1fffffu;
  return bytes <= 0x200000u - offset;
}

} // namespace

bool onScreen(std::uint32_t packedScreen) {
  const std::int32_t packed = (std::int32_t)packedScreen;
  if (packed - 0x10000 <= 0 || packed - 0x1000000 >= 0) {
    return false;
  }
  const std::int32_t x = (std::int32_t)(packedScreen << 16);
  return x > 0 && x - 0x2000000 < 0;
}

std::int32_t otBin(std::uint16_t sz) {
  std::int32_t bin = ((std::int32_t)sz >> kOtShift) - kOtNearPull;
  if (bin < 0) {
    bin = 0;
  }
  if (bin > kOtFarStart) {
    bin += kOtFarStep;
  }
  return bin;
}

Recipe derive(Core *core, std::int32_t deltaTime) {
  Recipe recipe{};
  if (core == nullptr || core->game == nullptr) {
    recipe.status = Status::InvalidCore;
    return recipe;
  }
  if (!span(kSparkles, (std::uint32_t)kRecords * kRecordStride) || !span(kCamera, 0x34u)) {
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

  psxport::native_projection::FixedAffine cameraAffine{};
  cameraAffine.m = actor_transform_math::readCameraMatrix(core).value;
  const std::int32_t cameraX = (std::int32_t)core->mem_r32(kCamera + 0x28u);
  const std::int32_t cameraY = (std::int32_t)core->mem_r32(kCamera + 0x2Cu);
  const std::int32_t cameraZ = (std::int32_t)core->mem_r32(kCamera + 0x30u);

  const auto reject = [&recipe](Reject reason) {
    ++recipe.rejects[(std::size_t)reason];
  };

  for (std::size_t index = 0; index < kRecords; ++index) {
    const std::uint32_t record = kSparkles + (std::uint32_t)index * kRecordStride;
    const std::uint32_t life = core->mem_r32(record + kLifeWord);
    const std::int32_t remaining = (std::int32_t)(life & 0xffu) - deltaTime;
    const std::int32_t spin = (std::int32_t)life >> 24;
    if (remaining <= 0) {
      // Retail writes the kill before it has computed anything else, so a sparkle whose life ran
      // out this tick leaves no line and no angle behind.
      recipe.writes.push_back({record, 0u, 0u, false});
      reject(Reject::Expired);
      continue;
    }
    ++recipe.alive;
    const std::int32_t denominator = (std::int32_t)((life >> 8) & 0xffu);
    const std::uint8_t angle =
        (std::uint8_t)(((life >> 16) & 0xffu) + (std::uint32_t)(spin * deltaTime));
    StateWrite write{record, (std::uint8_t)remaining, angle, true};
    if (denominator == 0) {
      // The fade divides by this, so a zero denominator is a corrupt record rather than a sparkle
      // that happens to be invisible. Keep the advance retail already performed and stop there.
      recipe.writes.push_back(write);
      reject(Reject::NoLifetime);
      continue;
    }
    // The elapsed fraction, scaled to 0x100. Retail shifts before taking the magnitude, so a
    // negative elapsed count folds back rather than clamping.
    const std::int32_t elapsed = (denominator - remaining) * 0x100;
    const std::int32_t fade = std::abs(elapsed) / denominator;

    const psxport::native_projection::ModelVertex input{
        (std::int16_t)((cameraY - (std::int32_t)core->mem_r32(record + 4u)) >> 2),
        (std::int16_t)((cameraZ - (std::int32_t)core->mem_r32(record + 8u)) >> 2),
        (std::int16_t)(((std::int32_t)core->mem_r32(record) - cameraX) >> 2)};
    const auto centre = psxport::native_projection::project(cameraAffine, projection, input);

    const std::uint32_t sizeWord = core->mem_r32(record + kSizeWord);
    const std::int32_t farSz = (std::int32_t)(sizeWord & 0xff00u);
    const std::uint32_t packedScreen =
        ((std::uint32_t)(std::uint16_t)centre.sy << 16) | (std::uint32_t)(std::uint16_t)centre.sx;
    const auto kill = [&](Reject reason) {
      // Every one of retail's draw-time rejections also kills the sparkle, so a spark that leaves
      // the screen does not come back when the camera turns.
      write.lifetime = 0u;
      recipe.writes.push_back(write);
      reject(reason);
    };
    if (farSz - (std::int32_t)centre.sz <= 0) {
      kill(Reject::TooFar);
      continue;
    }
    if ((std::int32_t)centre.sz - (std::int32_t)kNearSz <= 0) {
      kill(Reject::TooNear);
      continue;
    }
    if (!onScreen(packedScreen)) {
      kill(Reject::Offscreen);
      continue;
    }
    recipe.writes.push_back(write);

    // The cross is projected a second time through a diagonal matrix whose scale is the view depth
    // and a translation of twice the view position: the doubling cancels in the perspective divide
    // and the depth scale cancels the perspective shrink, so a sparkle keeps one screen size until
    // it comes closer than the 0x1000 floor.
    const std::int32_t viewX = (std::int32_t)(centre.raw_view_fixed[0] >> 12);
    const std::int32_t viewY = (std::int32_t)(centre.raw_view_fixed[1] >> 12);
    const std::int32_t viewZ = (std::int32_t)(centre.raw_view_fixed[2] >> 12);
    const std::int32_t cornerScale = viewZ < kMinCornerScale ? kMinCornerScale : viewZ;
    const auto trig = actor_transform_math::sineCosine(core, (std::uint32_t)angle * 2u);
    const std::int16_t reach =
        (std::int16_t)((std::int32_t)(kFadeFull - fade) * (std::int32_t)(sizeWord & 0xffu));
    const std::int32_t across = ((std::int32_t)trig.cosine * reach) >> kCornerShift;
    const std::int32_t up = ((std::int32_t)trig.sine * reach) >> kCornerShift;

    psxport::native_projection::FixedAffine crossAffine{};
    // ctc2 loads the diagonal as three packed words, so a scale past 16 bits spills into the lane
    // beside it. Reproduced rather than normalised: it is what the hardware does with this value.
    const std::int16_t low = (std::int16_t)cornerScale;
    const std::int16_t high = (std::int16_t)((std::uint32_t)cornerScale >> 16);
    crossAffine.m = {std::array<std::int16_t, 3>{low, high, 0}, {0, low, high}, {0, 0, low}};
    crossAffine.t = {viewX * 2, viewY * 2, viewZ * 2};
    // Unlike the glow, these packed words are built with an add, so a negative X borrows into Y.
    const std::array<psxport::native_projection::ModelVertex, kCorners> corners = {
        world_projection_math::packProjectionInput(-across, -up, 0),
        world_projection_math::packProjectionInput(up, -across, 0),
        world_projection_math::packProjectionInput(-up, across, 0),
        world_projection_math::packProjectionInput(across, up, 0)};
    std::array<psxport::native_projection::NativeProjectedVertex, kCorners> projected{};
    for (std::size_t corner = 0; corner < kCorners; ++corner) {
      projected[corner] =
          psxport::native_projection::project(crossAffine, projection, corners[corner]);
    }

    const std::uint32_t colour = core->mem_r32(record + kColour) & 0x00ffffffu;
    const std::uint16_t bin = (std::uint16_t)otBin(centre.sz);
    const auto vertex = [&](std::size_t corner) {
      return Vertex{projected[corner].sx,
                    projected[corner].sy,
                    projected[corner].px,
                    projected[corner].py,
                    centre.pz};
    };
    // Retail builds two packets in one pool slot and links the SECOND of them into the bin, whose
    // tag then points at the first, so the corner 1-2 stroke is ahead of the corner 0-3 stroke in
    // the chain. Reproduced rather than normalised: the two strokes overlap at the centre and the
    // order decides which colour wins there.
    Line leading{};
    leading.vertices = {vertex(1), vertex(2)};
    leading.colour = colour;
    leading.otBin = bin;
    leading.recordIndex = (std::uint32_t)index;
    leading.chainOrdinal = 0;
    Line trailing{};
    trailing.vertices = {vertex(0), vertex(3)};
    trailing.colour = colour;
    trailing.otBin = bin;
    trailing.recordIndex = (std::uint32_t)index;
    trailing.chainOrdinal = 1;
    recipe.lines.push_back(leading);
    recipe.lines.push_back(trailing);
    ++recipe.drawn;
  }
  recipe.status = recipe.lines.empty() ? Status::ValidEmpty : Status::Ready;
  return recipe;
}

void commit(Core *core, const Recipe &recipe) {
  if (core == nullptr) {
    return;
  }
  for (const auto &write : recipe.writes) {
    if (write.angleWritten) {
      core->mem_w8(write.record + kAngleByte, write.angle);
    }
    core->mem_w8(write.record + kLifeWord, write.lifetime);
  }
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

} // namespace spyro::sparkle_recipe
