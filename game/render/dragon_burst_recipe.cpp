#include "dragon_burst_recipe.h"

#include "actor_transform_math.h"
#include "core.h"
#include "proj_params.h"

namespace spyro::dragon_burst {
namespace {

// D_80076248 (asm/data/game.bss.s), laid out by include/dragon.h: the enable word, the world
// position it is centred on, the orientation RotVec8ToMatrix builds for it, and its radius.
constexpr std::uint32_t kBurst = 0x80076248u;
constexpr std::uint32_t kEnable = 0x00u;
constexpr std::uint32_t kPosition = 0x04u;
constexpr std::uint32_t kMatrix = 0x10u;
constexpr std::uint32_t kRadius = 0x24u;
constexpr std::uint32_t kColour = 0x2Bu;
constexpr std::uint32_t kCamera = 0x80076DD0u;

// The two rings walk the shared table backwards in 0x40-byte steps, which is an eighth of a turn,
// and the outer ring starts half a step further round. Its radius is shifted two bits less, so it
// reaches four times as far — those spokes are what make the burst a star and not a disc.
constexpr std::int32_t kInnerStart = 0x1C0;
constexpr std::int32_t kOuterStart = 0x1E0;
constexpr std::int32_t kStep = 0x40;
constexpr int kInnerShift = 12;
constexpr int kOuterShift = 10;

psxport::native_projection::ModelVertex
ringPoint(Core *core, std::int32_t tableOffset, std::int32_t radius, int shift) {
  const auto trig = actor_transform_math::sineCosine(core, (std::uint32_t)tableOffset);
  // mtc2 keeps only the low half for VZ0 but splits VXY0 across two lanes, so the scaled sine's
  // HIGH half becomes the vertical lane. On a negative value that is a sign-extension of -1 rather
  // than a coordinate, which is retail's own geometry and not a decode slip to normalise away.
  const std::int32_t horizontal = ((std::int32_t)trig.sine * radius) >> shift;
  const std::int32_t depth = ((std::int32_t)trig.cosine * radius) >> shift;
  return {(std::int16_t)horizontal, (std::int16_t)(horizontal >> 16), (std::int16_t)depth};
}

} // namespace

Recipe derive(Core *core) {
  Recipe recipe{};
  if (core == nullptr || core->game == nullptr) {
    return recipe;
  }
  if (core->mem_r32(kBurst + kEnable) == 0u) {
    recipe.status = Status::Inactive;
    return recipe;
  }
  const auto &geometry = core->rsub.projParams;
  if (!geometry.geomValid() || geometry.geomH() == 0) {
    recipe.status = Status::InvalidProjection;
    return recipe;
  }
  psxport::native_projection::ProjectionParams projection{};
  projection.ofx = (std::int32_t)((std::uint32_t)(std::int32_t)geometry.geomOfx() << 16u);
  projection.ofy = (std::int32_t)((std::uint32_t)(std::int32_t)geometry.geomOfy() << 16u);
  projection.h = (std::uint16_t)(std::int32_t)geometry.geomH();

  // The translation is the burst's camera-relative position rotated by the CAMERA, while the ring
  // itself is rotated by the burst's own matrix. Retail loads the two matrices in turn for exactly
  // that reason, so the star keeps its own orientation while sitting where the camera sees it.
  const auto camera = actor_transform_math::readCameraMatrix(core);
  const std::int32_t relative[3] = {(std::int32_t)core->mem_r32(kCamera + 0x2Cu) -
                                        (std::int32_t)core->mem_r32(kBurst + kPosition + 4u),
                                    (std::int32_t)core->mem_r32(kCamera + 0x30u) -
                                        (std::int32_t)core->mem_r32(kBurst + kPosition + 8u),
                                    (std::int32_t)core->mem_r32(kBurst + kPosition) -
                                        (std::int32_t)core->mem_r32(kCamera + 0x28u)};
  const auto translation =
      actor_transform_math::transform(camera, {relative[0], relative[1], relative[2]});

  psxport::native_projection::FixedAffine affine{};
  affine.m = actor_transform_math::readMatrix(core, kBurst + kMatrix).value;
  affine.t = {translation[0], translation[1], translation[2]};

  const std::int32_t radius = (std::int32_t)core->mem_r32(kBurst + kRadius);
  const auto project = [&](const psxport::native_projection::ModelVertex &vertex) {
    const auto out = psxport::native_projection::project(affine, projection, vertex);
    return Vertex{out.sx, out.sy};
  };
  const Vertex centre = project({0, 0, 0});
  std::array<Vertex, kSpokes> inner{};
  std::array<Vertex, kSpokes> outer{};
  for (std::size_t spoke = 0; spoke < kSpokes; ++spoke) {
    const std::int32_t step = (std::int32_t)spoke * kStep;
    inner[spoke] = project(ringPoint(core, kInnerStart - step, radius, kInnerShift));
    outer[spoke] = project(ringPoint(core, kOuterStart - step, radius, kOuterShift));
  }

  recipe.colour = core->mem_r8(kBurst + kColour);
  recipe.triangles.reserve(kSpokes * 2u);
  for (std::size_t spoke = 0; spoke < kSpokes; ++spoke) {
    // Retail closes the ring by copying its LAST point in front of the first, so the pair for spoke
    // 0 spans the seam rather than being dropped.
    const Vertex &from = spoke == 0 ? inner[kSpokes - 1] : inner[spoke - 1];
    const Vertex &to = inner[spoke];
    recipe.triangles.push_back({{from, to, outer[spoke]}});
    recipe.triangles.push_back({{from, to, centre}});
  }
  recipe.status = Status::Ready;
  return recipe;
}

const char *statusName(Status status) {
  switch (status) {
  case Status::Ready:
    return "ready";
  case Status::Inactive:
    return "inactive";
  case Status::InvalidCore:
    return "invalid-core";
  case Status::InvalidProjection:
    return "invalid-projection";
  }
  return "unknown";
}

} // namespace spyro::dragon_burst
