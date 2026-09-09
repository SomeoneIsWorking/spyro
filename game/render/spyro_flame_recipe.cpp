#include "spyro_flame_recipe.h"

#include "actor_transform_math.h"
#include "core.h"
#include "proj_params.h"
#include "world_projection_math.h"

#include <algorithm>
#include <array>

namespace spyro::flame_recipe {
namespace {

// Addresses recovered from 0x80058D64's own instruction encodings (asm/renderers/r_flame.s).
constexpr std::uint32_t kFlame = 0x800786C8u;
constexpr std::uint32_t kCamera = 0x80076DD0u;
constexpr std::uint32_t kPartDescriptors = 0x8006D94Cu; // one word per part length
constexpr std::uint32_t kPartPoints = 0x8006DAA8u;      // the eight parts' cross-section arrays
constexpr std::uint32_t kTipColours = 0x8006E1A8u;      // four Gouraud words, +0x10 when superflame

// Flame fields. The orientation matrix is five packed words in the GTE's own R11R12..R33 order.
constexpr std::uint32_t kFlamePosition = 0x00u;
constexpr std::uint32_t kFlameUv = 0x10u; // +0x18 when superflame
constexpr std::uint32_t kFlameLengths = 0x20u;
constexpr std::uint32_t kFlameLimits = 0x28u;
constexpr std::uint32_t kFlameSuper = 0x9Cu;
constexpr std::uint32_t kFlameMatrix = 0xB8u;

constexpr std::uint32_t kParts = 8u;
// Parts 0..3 hold 0xC0 bytes from the array base; parts 4..7 hold 0x100 bytes after a 0x300 gap.
constexpr std::uint32_t kNearPartStride = 0xC0u;
constexpr std::uint32_t kFarPartStride = 0x100u;
constexpr std::uint32_t kFarPartBase = 0x300u;
constexpr std::uint32_t kFarPartDescriptorGap = 0x80u;
constexpr std::uint32_t kCrossSectionBytes = 0x10u;
constexpr std::int32_t kOtBias = 2;
constexpr std::uint32_t kTipOtShift = 7u;
constexpr std::uint32_t kRibbonOtShift = 8u;
// Texture rows and grey the ribbon advances by, halved for the two closing steps.
constexpr std::int32_t kRibbonVStep = 0xA00;
constexpr std::int32_t kRibbonDarken = 0x0A0A0A;
constexpr std::int32_t kClosingVStep = 0x500;
constexpr std::int32_t kClosingDarken = 0x050505;
constexpr std::int32_t kGreyPerRow = 5;

bool span(std::uint32_t address, std::uint32_t bytes) {
  const std::uint32_t mapped = address & 0x1fffffffu;
  if (mapped >= 0x800000u) {
    return false;
  }
  const std::uint32_t offset = mapped & 0x1fffffu;
  return bytes <= 0x200000u - offset;
}

std::int64_t nclip(const psxport::native_projection::NativeProjectedVertex &a,
                   const psxport::native_projection::NativeProjectedVertex &b,
                   const psxport::native_projection::NativeProjectedVertex &c) {
  const std::int64_t x0 = a.sx, y0 = a.sy;
  const std::int64_t x1 = b.sx, y1 = b.sy;
  const std::int64_t x2 = c.sx, y2 = c.sy;
  return x0 * (y1 - y2) + x1 * (y2 - y0) + x2 * (y0 - y1);
}

Vertex toVertex(const psxport::native_projection::NativeProjectedVertex &projected) {
  return {(std::int16_t)projected.sx,
          (std::int16_t)projected.sy,
          projected.px,
          projected.py,
          projected.pz,
          projected.sz};
}

// A cross-section entry is two words: `x | z << 16` low-half-first, then `y | angle << 16`.
struct CrossSection {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t z = 0;
  std::uint32_t sineByteOffset = 0;
};

CrossSection readCrossSection(Core *core, std::uint32_t address) {
  const std::uint32_t first = core->mem_r32(address);
  const std::uint32_t second = core->mem_r32(address + 4u);
  return {(std::int32_t)first >> 16,
          (std::int32_t)(std::int16_t)(second & 0xffffu),
          (std::int32_t)(std::int16_t)(first & 0xffffu),
          (second >> 15) & 0x1feu};
}

} // namespace

std::int32_t ringScale(bool wide, bool superFlame) {
  if (!wide) {
    return 8;
  }
  return superFlame ? 0x40 : 0x2C;
}

std::uint8_t rampGrey(std::uint32_t rampIndex) {
  return (std::uint8_t)(0x80 - (std::int32_t)rampIndex * kGreyPerRow);
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
    return "invalid flame state";
  case Status::InvalidProjection:
    return "invalid projection";
  }
  return "unknown";
}

Recipe derive(Core *core) {
  Recipe recipe{};
  if (core == nullptr || core->game == nullptr) {
    recipe.status = Status::InvalidCore;
    return recipe;
  }
  if (!span(kFlame, 0x138u) || !span(kCamera, 0x34u) || !span(kPartDescriptors, 0x100u) ||
      !span(kPartPoints, 0x700u) || !span(kTipColours, 0x20u)) {
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

  // The flame origin enters the GTE through the camera rotation with a zero translation, and the
  // resulting view-space point becomes the translation every flame-local point is projected with.
  const auto camera = actor_transform_math::readCameraMatrix(core);
  psxport::native_projection::FixedAffine cameraAffine{};
  cameraAffine.m = camera.value;
  const auto originInput = world_projection_math::packProjectionInput(
      (std::int32_t)core->mem_r32(kCamera + 0x2Cu) - (std::int32_t)core->mem_r32(kFlame + 4u),
      (std::int32_t)core->mem_r32(kCamera + 0x30u) - (std::int32_t)core->mem_r32(kFlame + 8u),
      (std::int32_t)core->mem_r32(kFlame + kFlamePosition) -
          (std::int32_t)core->mem_r32(kCamera + 0x28u));
  const auto origin = psxport::native_projection::transform(cameraAffine, originInput);

  psxport::native_projection::FixedAffine flameAffine{};
  for (std::size_t row = 0; row < 3u; ++row) {
    for (std::size_t column = 0; column < 3u; ++column) {
      const std::size_t index = row * 3u + column;
      const std::uint32_t word = core->mem_r32(kFlame + kFlameMatrix + (index / 2u) * 4u);
      flameAffine.m[row][column] =
          (std::int16_t)((index % 2u) == 0u ? (word & 0xffffu) : (word >> 16));
    }
  }
  flameAffine.t = {(std::int32_t)(origin.raw_view_fixed[0] >> 12),
                   (std::int32_t)(origin.raw_view_fixed[1] >> 12),
                   (std::int32_t)(origin.raw_view_fixed[2] >> 12)};

  const bool superFlame = core->mem_r32(kFlame + kFlameSuper) != 0u;
  const std::uint32_t tipColourBase = kTipColours + (superFlame ? 0x10u : 0u);
  const std::uint32_t uvBase = kFlame + kFlameUv + (superFlame ? 8u : 0u);

  const auto reject = [&recipe](Reject reason) {
    ++recipe.rejects[(std::size_t)reason];
  };
  const auto project = [&](std::int32_t x, std::int32_t y, std::int32_t z) {
    return psxport::native_projection::project(
        flameAffine, projection, world_projection_math::packProjectionInput(x, y, z));
  };

  // Retail walks the parts from the last to the first, so a lower part index is linked later.
  for (std::uint32_t walk = 0; walk < kParts; ++walk) {
    const std::uint32_t part = kParts - 1u - walk;
    const std::int32_t length = (std::int8_t)core->mem_r8(kFlame + kFlameLengths + part);
    if (length <= 0) {
      reject(Reject::EmptyPart);
      continue;
    }
    ++recipe.parts;
    const bool farPart = part >= 4u;
    const std::uint32_t stride = farPart ? kFarPartStride : kNearPartStride;
    const std::uint32_t arrayBase =
        kPartPoints + stride * (farPart ? part - 4u : part) + (farPart ? kFarPartBase : 0u);
    const std::uint32_t arrayEnd = arrayBase + stride;
    const std::uint32_t descriptor =
        core->mem_r32(kPartDescriptors + (std::uint32_t)(length - 1) * 4u +
                      (farPart ? kFarPartDescriptorGap : 0u));

    std::uint32_t cursor = arrayBase + (((descriptor >> 24) << 3) & 0x7f8u);
    const std::uint32_t limit =
        arrayBase + ((std::uint32_t)(std::int8_t)core->mem_r8(kFlame + kFlameLimits + part) << 3);
    std::uint32_t faceOrdinal = 0;
    // `gp` in retail: whether anything of this part has been projected yet. The tip sets it, and
    // the ribbon's wide ring is only used once it is set.
    bool projectedAnything = false;

    if (cursor <= limit && arrayEnd > cursor && span(cursor, 8u) && span(cursor - 0x10u, 8u)) {
      const auto tip = readCrossSection(core, cursor);
      const auto tipVertex = project(-tip.x, -tip.y, tip.z);
      const auto ring = readCrossSection(core, cursor - kCrossSectionBytes);
      const auto trig = actor_transform_math::sineCosine(core, ring.sineByteOffset);
      const std::int32_t scale = ringScale(true, superFlame);
      std::int32_t cosine = (scale * trig.cosine) >> 11;
      std::int32_t sine = (scale * trig.sine) >> 11;
      const std::int32_t ax = cosine - ring.x, ay = sine - ring.y;
      const std::int32_t bx = ax - 2 * cosine, by = ay - 2 * sine;
      // The negation happens in a branch-delay slot, so it applies whether or not the halving does.
      cosine = -cosine;
      if (superFlame) {
        cosine >>= 1;
        sine >>= 1;
      }
      const std::int32_t cx = sine - ring.x, cy = cosine - ring.y;
      const auto p0 = project(cx, cy, ring.z);
      const auto p1 = project(ax, ay, ring.z);
      const auto p2 = project(bx, by, ring.z);

      const std::int32_t bin = (std::int32_t)(tipVertex.sz >> kTipOtShift) - kOtBias;
      if (bin < 0) {
        reject(Reject::TipBehindCamera);
      } else {
        projectedAnything = true;
        ++recipe.tips;
        std::array<std::uint32_t, 4> colours{};
        for (std::size_t i = 0; i < colours.size(); ++i) {
          colours[i] = core->mem_r32(tipColourBase + (std::uint32_t)i * 4u) & 0xffffffu;
        }
        // The four triangles of the tip fan, each with retail's own vertex and colour selection.
        // The ring indices follow the RTPT lanes: SXY0 is the point built from the negated cosine,
        // SXY1 the first ring point and SXY2 the second, and the fan winds through them in that
        // order. Reading them in any other order reverses the winding, and NCLIP then rejects every
        // triangle — which is exactly what a first pass here did, silently and completely.
        const std::array<std::array<int, 3>, 4> vertexPick = {
            std::array<int, 3>{-1, 1, 0}, {-1, 0, 2}, {-1, 2, 1}, {0, 1, 2}};
        const std::array<std::array<int, 3>, 4> colourPick = {
            std::array<int, 3>{0, 2, 1}, {0, 1, 3}, {0, 3, 2}, {1, 2, 3}};
        const std::array<psxport::native_projection::NativeProjectedVertex, 3> ringPoints = {
            p0, p1, p2};
        for (std::size_t triangle = 0; triangle < 4u; ++triangle) {
          std::array<psxport::native_projection::NativeProjectedVertex, 3> points{};
          for (std::size_t i = 0; i < 3u; ++i) {
            const int pick = vertexPick[triangle][i];
            points[i] = pick < 0 ? tipVertex : ringPoints[(std::size_t)pick];
          }
          if (nclip(points[0], points[1], points[2]) <= 0) {
            reject(Reject::TipBackfacing);
            continue;
          }
          Face face{};
          face.nv = 3;
          face.otBin = (std::uint16_t)std::min(bin, 0xffff);
          face.part = part;
          face.faceOrdinal = faceOrdinal++;
          for (std::size_t i = 0; i < 3u; ++i) {
            face.vertices[i] = toVertex(points[i]);
            const std::uint32_t colour = colours[(std::size_t)colourPick[triangle][i]];
            face.red[i] = (std::uint8_t)colour;
            face.green[i] = (std::uint8_t)(colour >> 8);
            face.blue[i] = (std::uint8_t)(colour >> 16);
          }
          recipe.faces.push_back(face);
        }
      }
    } else {
      reject(Reject::TipCursorPastLimit);
    }

    cursor -= kCrossSectionBytes;
    std::int32_t rampIndex = (std::int32_t)((descriptor >> 8) & 0xffu);
    std::int32_t remaining = (std::int32_t)((descriptor >> 16) & 0xffu);
    if (cursor > limit) {
      const std::int32_t skipped = (std::int32_t)((cursor - limit) >> 3);
      rampIndex += skipped;
      remaining -= skipped;
      cursor = limit;
    }
    std::int32_t colour = rampGrey((std::uint32_t)rampIndex) * 0x010101;
    const std::uint32_t uvA = core->mem_r32(uvBase) + ((std::uint32_t)rampIndex << 8);
    const std::uint32_t uvB = core->mem_r32(uvBase + 4u) + ((std::uint32_t)rampIndex << 8);
    std::int32_t uvALow = (std::int32_t)(uvA & 0xffffu);
    std::int32_t uvBLow = (std::int32_t)(uvB & 0xffffu);
    const std::uint16_t clut = (std::uint16_t)(uvA >> 16);
    const std::uint16_t tpage = (std::uint16_t)(uvB >> 16);
    std::int32_t vStep = kRibbonVStep;
    std::int32_t darken = kRibbonDarken;

    bool havePrevious = false;
    Vertex previous[2]{};
    while (true) {
      if (!span(cursor, 8u)) {
        recipe.status = Status::InvalidState;
        return recipe;
      }
      const auto section = readCrossSection(core, cursor);
      cursor -= kCrossSectionBytes;
      const auto trig = actor_transform_math::sineCosine(core, section.sineByteOffset);
      const bool wide = remaining > 0 && projectedAnything;
      const std::int32_t scale = ringScale(wide, superFlame);
      const std::int32_t cosine = (scale * trig.cosine) >> 11;
      const std::int32_t sine = (scale * trig.sine) >> 11;
      const std::int32_t ax = cosine - section.x, ay = sine - section.y;
      const std::int32_t bx = ax - 2 * cosine, by = ay - 2 * sine;
      const auto q0 = project(ax, ay, section.z);
      const auto q1 = project(bx, by, section.z);
      projectedAnything = true;

      if (havePrevious) {
        const std::int32_t bin =
            (std::int32_t)(((std::uint32_t)q0.sz + q1.sz) >> kRibbonOtShift) - kOtBias;
        const std::int32_t nextColour = colour - darken;
        const std::int32_t nextUvA = uvALow + vStep;
        const std::int32_t nextUvB = uvBLow + vStep;
        if (bin < 0) {
          reject(Reject::RibbonNegativeBin);
        } else {
          ++recipe.ribbons;
          Face face{};
          face.nv = 4;
          face.textured = true;
          face.semi = true;
          face.clut = clut;
          face.tpage = tpage;
          face.otBin = (std::uint16_t)std::min(bin, 0xffff);
          face.part = part;
          face.faceOrdinal = faceOrdinal++;
          face.vertices[0] = previous[0];
          face.vertices[1] = previous[1];
          face.vertices[2] = toVertex(q0);
          face.vertices[3] = toVertex(q1);
          const std::int32_t cornerColour[4] = {colour, colour, nextColour, nextColour};
          const std::int32_t cornerUv[4] = {uvALow, uvBLow, nextUvA, nextUvB};
          for (std::size_t i = 0; i < 4u; ++i) {
            face.red[i] = (std::uint8_t)cornerColour[i];
            face.green[i] = (std::uint8_t)(cornerColour[i] >> 8);
            face.blue[i] = (std::uint8_t)(cornerColour[i] >> 16);
            face.u[i] = (std::uint8_t)cornerUv[i];
            face.v[i] = (std::uint8_t)(cornerUv[i] >> 8);
          }
          recipe.faces.push_back(face);
        }
        // Retail advances the shared edge whether or not the quad reached the ordering table, so a
        // dropped bin leaves a gap rather than shifting every later quad's texture row and shade.
        colour = nextColour;
        uvALow = nextUvA;
        uvBLow = nextUvB;
      }
      previous[0] = toVertex(q0);
      previous[1] = toVertex(q1);
      havePrevious = true;

      if (remaining <= 0) {
        break;
      }
      --remaining;
      if (remaining <= 0) {
        // The final two steps close the ribbon at half the texture advance and half the shading
        // step, and skip a cross-section forward rather than continuing to walk backward.
        --remaining;
        cursor += 8u;
        vStep = kClosingVStep;
        darken = kClosingDarken;
      }
    }
  }

  recipe.status = recipe.faces.empty() ? Status::ValidEmpty : Status::Ready;
  return recipe;
}

} // namespace spyro::flame_recipe
