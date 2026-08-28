#include "cyclorama_portal_mesh_recipe.h"

#include "actor_model_codec.h"
#include "core.h"
#include "gpu_vk.h"
#include "world_chunk_codec.h"
#include "world_projection_math.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <span>
#include <tuple>
#include <utility>

namespace spyro::cyclorama_portal_mesh {
namespace {

using psxport::native_projection::FixedAffine;
using psxport::native_projection::ModelVertex;
using psxport::native_projection::NativeProjectedVertex;
using psxport::native_projection::ProjectionParams;
using world_chunk_codec::RamView;

constexpr uint32_t kPortalAsset = 0x00u;
constexpr uint32_t kPortalPointCount = 0x04u;
constexpr uint32_t kPortalNormal = 0x08u;
constexpr uint32_t kPortalCenter = 0x20u;
constexpr uint32_t kPortalPoints = 0x20u;
constexpr uint32_t kNearDistanceEnd = 0x3000u;
constexpr uint32_t kPortalDistanceEnd = 0x4000u;

struct Point3 {
  int32_t x = 0;
  int32_t y = 0;
  int32_t z = 0;
};

struct ClipVertex {
  Vertex vertex{};
  double x = 0.0;
  double y = 0.0;
};

template <typename T> T clampCast(int64_t value) {
  return (T)std::clamp<int64_t>(
      value, (int64_t)std::numeric_limits<T>::min(), (int64_t)std::numeric_limits<T>::max());
}

PortalFrame refuse(PortalFrame frame, Status status, const char *why) {
  frame.status = status;
  frame.refusal = why;
  frame.edges.clear();
  return frame;
}

Recipe refuse(Recipe recipe, Status status, const char *why) {
  recipe.status = status;
  recipe.refusal = why;
  recipe.faces.clear();
  return recipe;
}

Point3 point(const RamView &ram, uint32_t address) {
  return {
      (int32_t)ram.r32(address), (int32_t)ram.r32(address + 4u), (int32_t)ram.r32(address + 8u)};
}

int32_t trig(const RamView &ram, uint32_t base, int32_t angle) {
  const uint32_t wrapped = (uint32_t)angle & 0xfffu;
  const uint32_t index = wrapped >> 4;
  const uint32_t fraction = wrapped & 0xfu;
  const int32_t first = (int16_t)ram.r16(base + index * 2u);
  if (fraction == 0u) {
    return first;
  }
  const int32_t second = (int16_t)ram.r16(base + (index + 1u) * 2u);
  return first + (fraction * (second - first) >> 4);
}

int32_t sine(const RamView &ram, int32_t angle) {
  return trig(ram, kSineTable, angle);
}

int32_t cosine(const RamView &ram, int32_t angle) {
  return trig(ram, kSineTable + 0x80u, angle);
}

FixedAffine multiply(const FixedAffine &left, const FixedAffine &right) {
  FixedAffine out{};
  for (uint32_t row = 0; row < 3u; ++row) {
    for (uint32_t column = 0; column < 3u; ++column) {
      int64_t sum = 0;
      for (uint32_t inner = 0; inner < 3u; ++inner) {
        sum += (int64_t)left.m[row][inner] * right.m[inner][column];
      }
      out.m[row][column] = clampCast<int16_t>(sum >> 12);
    }
  }
  return out;
}

std::pair<FixedAffine, FixedAffine>
portalMatrices(const RamView &ram, uint32_t nextYaw, int32_t nextPitch) {
  const int32_t roll = (int16_t)ram.r16(kCamera + 0x4cu);
  const int32_t pitch = (int16_t)ram.r16(kCamera + 0x4eu) - nextPitch;
  const int32_t yaw = (int16_t)ram.r16(kCamera + 0x50u) + (int32_t)nextYaw;
  const int32_t sx = sine(ram, pitch), cx = cosine(ram, pitch);
  const int32_t sy = sine(ram, yaw), cy = cosine(ram, yaw);
  const int32_t sz = sine(ram, roll), cz = cosine(ram, roll);
  FixedAffine x{};
  x.m = {{{4096, 0, 0}, {0, (int16_t)cx, (int16_t)-sx}, {0, (int16_t)sx, (int16_t)cx}}};
  FixedAffine y{};
  y.m = {{{(int16_t)cy, 0, (int16_t)sy}, {0, 4096, 0}, {(int16_t)-sy, 0, (int16_t)cy}}};
  FixedAffine z{};
  z.m = {{{(int16_t)cz, (int16_t)sz, 0}, {(int16_t)-sz, (int16_t)cz, 0}, {0, 0, 4096}}};
  FixedAffine cull = multiply(multiply(x, y), z);
  FixedAffine projection = cull;
  for (uint32_t column = 0; column < 3u; ++column) {
    int32_t value = (int32_t)projection.m[1][column] * 0x140;
    if (value < 0) {
      value += 0x1ff;
    }
    projection.m[1][column] = (int16_t)(value >> 9);
  }
  return {cull, projection};
}

uint32_t vectorMagnitude(const RamView &ram, Point3 value) {
  const int32_t x = (int16_t)value.x;
  const int32_t y = (int16_t)value.y;
  const int32_t z = (int16_t)value.z;
  const uint32_t squared = (uint32_t)(x * x + y * y + z * z);
  if (squared == 0u) {
    return 0u;
  }
  const uint32_t leading = std::countl_zero(squared) & ~1u;
  const uint32_t outputShift = (31u - leading) >> 1;
  uint32_t normalized = 0;
  if (leading >= 24u) {
    normalized = squared << (leading - 24u);
  } else {
    normalized = squared >> (24u - leading);
  }
  const uint32_t tableIndex = (normalized - 0x40u) * 2u;
  if (!ram.contains(kMagnitudeTable + tableIndex, 2u)) {
    return 0u;
  }
  const uint32_t root = (uint16_t)ram.r16(kMagnitudeTable + tableIndex);
  return (root << outputShift) >> 12;
}

uint32_t distanceShift(Point3 value) {
  const uint32_t magnitude = (uint32_t)(std::abs((int64_t)value.x) + std::abs((int64_t)value.y) +
                                        std::abs((int64_t)value.z));
  if (magnitude == 0u) {
    return 0u;
  }
  const uint32_t topBit = 31u - std::countl_zero(magnitude);
  return topBit > 13u ? (topBit - 13u) >> 1 : 0u;
}

ProjectionParams projectionParams(Core *core, int clipRight) {
  ProjectionParams out{};
  out.ofx = (int32_t)(core->rsub.projParams.geomOfx() * 65536.0f);
  out.ofy = (int32_t)(core->rsub.projParams.geomOfy() * 65536.0f);
  out.h = (uint16_t)core->rsub.projParams.geomH();
  if (core->game != nullptr && gpu_vk_wide_engine(core)) {
    out.ofx = (clipRight / 2) << 16;
  }
  return out;
}

NativeProjectedVertex projectPortalPoint(const FixedAffine &camera,
                                         const ProjectionParams &projection,
                                         Point3 cameraPosition,
                                         Point3 world,
                                         uint32_t shift) {
  const ModelVertex input{
      (int16_t)((cameraPosition.y - world.y) >> shift),
      (int16_t)((cameraPosition.z - world.z) >> shift),
      (int16_t)((world.x - cameraPosition.x) >> shift),
  };
  return psxport::native_projection::project(camera, projection, input);
}

uint32_t colorLerp(uint32_t source, uint32_t target, int16_t factor) {
  const std::array<int32_t, 3> far = {
      (int32_t)(target & 0xffu) << 4,
      (int32_t)((target >> 8) & 0xffu) << 4,
      (int32_t)((target >> 16) & 0xffu) << 4,
  };
  return actor_model_codec::depthCueRgb(source, far, factor).rgb;
}

uint8_t boxClip(const NativeProjectedVertex &vertex, const PortalFrame &frame) {
  uint8_t out = 0;
  if (vertex.sy <= frame.clipTop) {
    out |= 1u;
  }
  if (vertex.sy >= frame.clipBottom) {
    out |= 2u;
  }
  if (vertex.sx <= frame.clipLeft) {
    out |= 4u;
  }
  if (vertex.sx >= frame.clipRight) {
    out |= 8u;
  }
  return out;
}

double side(const ClipEdge &edge, double x, double y) {
  return (double)(edge.x1 - edge.x0) * (y - edge.y0) - (double)(edge.y1 - edge.y0) * (x - edge.x0);
}

ClipVertex
intersection(const ClipVertex &from, const ClipVertex &to, double fromSide, double toSide) {
  const double denominator = fromSide - toSide;
  const double t = denominator == 0.0 ? 0.0 : fromSide / denominator;
  ClipVertex out{};
  out.x = from.x + (to.x - from.x) * t;
  out.y = from.y + (to.y - from.y) * t;
  out.vertex.sx = clampCast<int16_t>((int64_t)std::llround(out.x));
  out.vertex.sy = clampCast<int16_t>((int64_t)std::llround(out.y));
  out.vertex.sz = (uint16_t)std::clamp<long long>(
      std::llround(from.vertex.sz + ((double)to.vertex.sz - from.vertex.sz) * t), 0, 65535);
  out.vertex.screenX = (float)(from.vertex.screenX + (to.vertex.screenX - from.vertex.screenX) * t);
  out.vertex.screenY = (float)(from.vertex.screenY + (to.vertex.screenY - from.vertex.screenY) * t);
  out.vertex.viewZ = (float)(from.vertex.viewZ + (to.vertex.viewZ - from.vertex.viewZ) * t);
  uint32_t rgb = 0;
  for (uint32_t channel = 0; channel < 3u; ++channel) {
    const double a = (double)((from.vertex.rgb >> (channel * 8u)) & 0xffu);
    const double b = (double)((to.vertex.rgb >> (channel * 8u)) & 0xffu);
    const uint32_t value = (uint32_t)std::clamp<long long>(std::llround(a + (b - a) * t), 0, 255);
    rgb |= value << (channel * 8u);
  }
  out.vertex.rgb = rgb;
  return out;
}

std::vector<ClipVertex> clipTriangle(std::array<ClipVertex, 3> triangle,
                                     std::span<const ClipEdge> edges) {
  std::vector<ClipVertex> polygon(triangle.begin(), triangle.end());
  for (const ClipEdge &edge : edges) {
    if (polygon.empty()) {
      break;
    }
    std::vector<ClipVertex> next;
    next.reserve(polygon.size() + 1u);
    ClipVertex previous = polygon.back();
    double previousSide = side(edge, previous.x, previous.y);
    bool previousInside = previousSide > 0.0;
    for (const ClipVertex &current : polygon) {
      const double currentSide = side(edge, current.x, current.y);
      const bool currentInside = currentSide > 0.0;
      if (currentInside != previousInside) {
        next.push_back(intersection(previous, current, previousSide, currentSide));
      }
      if (currentInside) {
        next.push_back(current);
      }
      previous = current;
      previousSide = currentSide;
      previousInside = currentInside;
    }
    polygon = std::move(next);
  }
  return polygon;
}

} // namespace

PortalFrame prepareFrame(
    Core *core, uint32_t portal, uint32_t portalOrdinal, uint32_t nextYaw, int32_t nextPitch) {
  PortalFrame frame{};
  frame.portal = portal;
  frame.portalOrdinal = portalOrdinal;
  if (core == nullptr) {
    return frame;
  }
  if (!core->rsub.projParams.geomValid()) {
    return refuse(std::move(frame), Status::ProjectionUnset, "projection_unset");
  }
  const RamView ram(std::span<const uint8_t>(core->ram, sizeof(core->ram)));
  if ((portal & 3u) != 0u || !ram.contains(portal, kPortalPoints + 12u)) {
    return refuse(std::move(frame), Status::InvalidPortal, "portal_bounds");
  }
  frame.asset = ram.r32(portal + kPortalAsset);
  frame.pointCount = ram.r32(portal + kPortalPointCount);
  if (frame.pointCount < 3u || frame.pointCount > kPortalPointCapacity ||
      !ram.contains(portal + kPortalPoints, frame.pointCount * 12u)) {
    return refuse(std::move(frame), Status::InvalidPointCount, "portal_points");
  }
  if ((frame.asset & 3u) != 0u || !ram.contains(frame.asset, 20u)) {
    return refuse(std::move(frame), Status::InvalidAsset, "asset_header");
  }

  const Point3 cameraPosition = point(ram, kCamera + 0x28u);
  const Point3 center = point(ram, portal + kPortalCenter);
  Point3 delta{
      center.x - cameraPosition.x, center.y - cameraPosition.y, center.z - cameraPosition.z};
  frame.distanceShift = distanceShift(delta);
  Point3 scaled{delta.x >> frame.distanceShift,
                delta.y >> frame.distanceShift,
                delta.z >> frame.distanceShift};
  frame.distance = vectorMagnitude(ram, scaled) << frame.distanceShift;
  std::tie(frame.cullMatrix, frame.projectionMatrix) = portalMatrices(ram, nextYaw, nextPitch);
  const FixedAffine cameraMatrix = world_projection_math::decodeMatrix(ram, kCamera);
  const int screenRight =
      core->game != nullptr && gpu_vk_wide_engine(core) ? gpu_vk_wide_engine_w(core) : 512;
  if (screenRight <= 0 || screenRight > INT16_MAX) {
    return refuse(std::move(frame), Status::InvalidClipRegion, "screen_width");
  }
  const ProjectionParams projection = projectionParams(core, screenRight);
  std::vector<std::array<int32_t, 3>> projected;
  projected.reserve(frame.pointCount);
  int64_t sumX = 0, sumY = 0, sumZ = 0;
  for (uint32_t i = 0; i < frame.pointCount; ++i) {
    const Point3 world = point(ram, portal + kPortalPoints + i * 12u);
    const NativeProjectedVertex p =
        projectPortalPoint(cameraMatrix, projection, cameraPosition, world, frame.distanceShift);
    projected.push_back({p.sx, p.sy, (int32_t)p.raw_view[2] << frame.distanceShift});
    sumX += p.sx;
    sumY += p.sy;
    sumZ += (int32_t)p.raw_view[2] << frame.distanceShift;
  }
  const int32_t averageX = (int32_t)(sumX / (int32_t)frame.pointCount);
  const int32_t averageY = (int32_t)(sumY / (int32_t)frame.pointCount);
  int32_t averageDepth = (int32_t)(sumZ / (int32_t)frame.pointCount) >> 7;
  if (averageDepth > 255) {
    averageDepth += 0x40;
  }
  frame.otBin = (uint16_t)std::clamp(averageDepth, 0, 0x7ff);
  for (auto &p : projected) {
    if (p[0] < averageX) {
      p[0] += 2;
    } else if (p[0] > averageX) {
      p[0] -= 2;
    }
    if (p[1] < averageY) {
      p[1] += 2;
    } else if (p[1] > averageY) {
      p[1] -= 2;
    }
  }

  const Point3 normal = point(ram, portal + kPortalNormal);
  const Point3 first = point(ram, portal + kPortalPoints + 12u);
  const int64_t orientation = (int64_t)(first.x - cameraPosition.x) * normal.x +
                              (int64_t)(first.y - cameraPosition.y) * normal.y +
                              (int64_t)(first.z - cameraPosition.z) * normal.z;
  frame.clipLeft = screenRight;
  frame.clipTop = 240;
  frame.clipRight = 0;
  frame.clipBottom = 0;
  for (uint32_t i = 0; i < frame.pointCount; ++i) {
    const uint32_t j = orientation > 0 ? (i + 1u) % frame.pointCount
                                       : (i + frame.pointCount - 1u) % frame.pointCount;
    const auto &a = projected[i];
    const auto &b = projected[j];
    frame.clipLeft = std::min(frame.clipLeft, a[0]);
    frame.clipTop = std::min(frame.clipTop, a[1]);
    frame.clipRight = std::max(frame.clipRight, a[0]);
    frame.clipBottom = std::max(frame.clipBottom, a[1]);
    const bool crossesScreen = (a[0] < screenRight || b[0] < screenRight) &&
                               (a[0] > 0 || b[0] > 0) && (a[1] < 240 || b[1] < 240) &&
                               (a[1] > 0 || b[1] > 0);
    if (crossesScreen) {
      frame.edges.push_back({a[0], a[1], b[0], b[1]});
    }
  }
  frame.clipLeft = std::clamp(frame.clipLeft, 0, screenRight);
  frame.clipRight = std::clamp(frame.clipRight, 0, screenRight);
  frame.clipTop = std::clamp(frame.clipTop, 0, 240);
  frame.clipBottom = std::clamp(frame.clipBottom, 0, 240);
  frame.maskVisible =
      !frame.edges.empty() && frame.clipLeft < frame.clipRight && frame.clipTop < frame.clipBottom;
  if (!frame.maskVisible || frame.distance >= kPortalDistanceEnd) {
    frame.status = Status::ValidEmpty;
    frame.refusal = "none";
    return frame;
  }
  if (frame.distance <= kNearDistanceEnd) {
    return refuse(std::move(frame), Status::NearFamilyUnsupported, "near_portal_family");
  }

  frame.fadeFactor = frame.distance - kNearDistanceEnd;
  const uint32_t baseColor = ram.r32(frame.asset + 0x10u);
  const uint32_t phase = (uint32_t)std::abs(cosine(
                             ram, (int32_t)(ram.r32(kLevelTicks) * 16u + portalOrdinal * 512u))) >>
                         1;
  const uint32_t animated = colorLerp(baseColor, 0x00ffffffu, (int16_t)phase);
  frame.tintColor = colorLerp(baseColor, animated, (int16_t)frame.fadeFactor);
  frame.status = Status::Ready;
  frame.refusal = "none";
  return frame;
}

Recipe build(Core *core, const PortalFrame &frame) {
  Recipe out{};
  out.portal = frame.portal;
  out.asset = frame.asset;
  if (core == nullptr) {
    return out;
  }
  if (frame.status == Status::ValidEmpty) {
    out.status = Status::ValidEmpty;
    out.refusal = "none";
    return out;
  }
  if (frame.status != Status::Ready || frame.edges.empty()) {
    return refuse(std::move(out), Status::InvalidClipRegion, "portal_frame");
  }
  const RamView ram(std::span<const uint8_t>(core->ram, sizeof(core->ram)));
  if (!ram.contains(frame.asset, 8u)) {
    return refuse(std::move(out), Status::InvalidAsset, "asset_header");
  }
  out.assetObjects = ram.r32(frame.asset);
  const uint32_t objectTable = ram.r32(frame.asset + 4u);
  if (out.assetObjects == 0u || out.assetObjects > kObjectCapacity ||
      !ram.contains(objectTable, out.assetObjects * 4u)) {
    return refuse(std::move(out), Status::InvalidAsset, "object_table");
  }
  const int clipRight =
      core->game != nullptr && gpu_vk_wide_engine(core) ? gpu_vk_wide_engine_w(core) : 512;
  const ProjectionParams projection = projectionParams(core, clipRight);
  for (uint32_t objectIndex = 0; objectIndex < out.assetObjects; ++objectIndex) {
    const uint32_t object = ram.r32(objectTable + objectIndex * 4u);
    if ((object & 3u) != 0u || !ram.contains(object, 24u)) {
      return refuse(std::move(out), Status::InvalidObject, "object_header");
    }
    const uint32_t origin = ram.r32(object);
    const uint32_t distanceWord = ram.r32(object + 4u);
    const auto cull = psxport::native_projection::project(
        frame.cullMatrix,
        projection,
        {(int16_t)origin, (int16_t)(origin >> 16), (int16_t)(distanceWord >> 16)});
    const uint32_t meta = ram.r32(object + 12u);
    const uint32_t faceMeta = ram.r32(object + 16u);
    const uint32_t vertexCount = (meta & 0xffffu) + 1u;
    const uint32_t colorBytes = faceMeta >> 14;
    const uint32_t faceBytes = (faceMeta << 3) & 0xfff8u;
    out.authoredCandidates += faceBytes / 8u;
    if ((int32_t)((uint32_t)(int32_t)cull.raw_view[2] - (uint32_t)(int16_t)distanceWord) <= 0) {
      continue;
    }
    ++out.survivingObjects;
    if (vertexCount >= 1024u || !ram.contains(object + 24u, (vertexCount + 1u) * 4u)) {
      return refuse(std::move(out), Status::InvalidVertexSpan, "vertex_span");
    }
    const uint32_t colorBase = object + 24u + (vertexCount - 1u) * 4u;
    const uint32_t faceBegin = colorBase + colorBytes;
    if ((colorBytes & 3u) != 0u || !ram.contains(colorBase, colorBytes) ||
        !ram.contains(faceBegin, faceBytes)) {
      return refuse(std::move(out), Status::InvalidFaceSpan, "face_span");
    }
    std::vector<uint32_t> colors;
    colors.reserve(colorBytes / 4u);
    const std::array<int32_t, 3> far = {
        (int32_t)(frame.tintColor & 0xffu) << 4,
        (int32_t)((frame.tintColor >> 8) & 0xffu) << 4,
        (int32_t)((frame.tintColor >> 16) & 0xffu) << 4,
    };
    for (uint32_t offset = 0; offset < colorBytes; offset += 4u) {
      colors.push_back(actor_model_codec::depthCueRgb(
                           ram.r32(colorBase + offset), far, (int16_t)frame.fadeFactor)
                           .rgb);
    }
    const uint32_t originXY = ram.r32(object + 8u);
    const int32_t originY = (int16_t)originXY;
    const int32_t originX = (int16_t)(originXY >> 16);
    const int32_t originZ = (int16_t)(meta >> 16);
    std::vector<Vertex> vertices;
    vertices.reserve(vertexCount);
    uint8_t commonClip = 0xffu;
    for (uint32_t i = 0; i < vertexCount; ++i) {
      const uint32_t packed = ram.r32(object + 24u + i * 4u);
      const int32_t packedXy = originY - (int32_t)((packed >> 10) & 0x7ffu) +
                               ((uint32_t)(originX - (int32_t)(packed & 0x3ffu)) << 16);
      const auto projected =
          psxport::native_projection::project(frame.projectionMatrix,
                                              projection,
                                              {(int16_t)packedXy,
                                               (int16_t)((uint32_t)packedXy >> 16),
                                               (int16_t)((packed >> 21) + (uint32_t)originZ)});
      Vertex vertex{};
      vertex.sx = projected.sx;
      vertex.sy = projected.sy;
      vertex.sz = projected.sz;
      vertex.screenX = projected.px;
      vertex.screenY = projected.py;
      vertex.viewZ = projected.pz;
      vertex.clip = boxClip(projected, frame);
      commonClip &= vertex.clip;
      vertices.push_back(vertex);
      ++out.projectedVertices;
    }
    if (commonClip & 0x0fu) {
      continue;
    }
    for (uint32_t source = faceBegin; source < faceBegin + faceBytes; source += 8u) {
      ++out.candidates;
      const uint32_t vertexWord = ram.r32(source);
      const uint32_t colorWord = ram.r32(source + 4u);
      const uint32_t vertexOffsets[3] = {
          vertexWord >> 20, (vertexWord >> 10) & 0x3fcu, vertexWord & 0x3fcu};
      const uint32_t colorOffsets[3] = {
          colorWord >> 20, (colorWord >> 10) & 0x3fcu, colorWord & 0x3fcu};
      std::array<ClipVertex, 3> triangle{};
      uint8_t clips = 0xffu;
      for (uint32_t i = 0; i < 3u; ++i) {
        if ((vertexOffsets[i] & 3u) != 0u || (colorOffsets[i] & 3u) != 0u ||
            vertexOffsets[i] / 4u >= vertices.size() || colorOffsets[i] / 4u >= colors.size()) {
          return refuse(std::move(out), Status::InvalidFaceIndex, "face_index");
        }
        triangle[i].vertex = vertices[vertexOffsets[i] / 4u];
        triangle[i].vertex.rgb = colors[colorOffsets[i] / 4u];
        triangle[i].x = triangle[i].vertex.sx;
        triangle[i].y = triangle[i].vertex.sy;
        clips &= triangle[i].vertex.clip;
      }
      if (clips & 0x1fu) {
        ++out.boxRejected;
        continue;
      }
      std::vector<ClipVertex> polygon = clipTriangle(triangle, frame.edges);
      if (polygon.size() < 3u) {
        ++out.apertureRejected;
        continue;
      }
      ++out.sourceAccepted;
      if (polygon.size() - 2u > kFaceCapacity - out.faces.size()) {
        return refuse(std::move(out), Status::CapacityExceeded, "face_capacity");
      }
      for (size_t i = 1; i + 1 < polygon.size(); ++i) {
        Face face{};
        face.object = object;
        face.source = source;
        face.sourceOrdinal = out.candidates - 1u;
        face.vertices = {polygon[0].vertex, polygon[i].vertex, polygon[i + 1u].vertex};
        face.gouraud = face.vertices[0].rgb != face.vertices[1].rgb ||
                       face.vertices[0].rgb != face.vertices[2].rgb;
        out.faces.push_back(face);
      }
    }
  }
  out.emittedTriangles = (uint32_t)out.faces.size();
  out.status = out.faces.empty() ? Status::ValidEmpty : Status::Ready;
  out.refusal = "none";
  return out;
}

const char *statusName(Status status) {
  switch (status) {
  case Status::Ready:
    return "ready";
  case Status::ValidEmpty:
    return "valid empty";
  case Status::InvalidCore:
    return "invalid core";
  case Status::ProjectionUnset:
    return "projection unset";
  case Status::InvalidPortal:
    return "invalid portal";
  case Status::InvalidPointCount:
    return "invalid point count";
  case Status::InvalidAsset:
    return "invalid asset";
  case Status::InvalidObject:
    return "invalid object";
  case Status::InvalidVertexSpan:
    return "invalid vertex span";
  case Status::InvalidFaceSpan:
    return "invalid face span";
  case Status::InvalidFaceIndex:
    return "invalid face index";
  case Status::InvalidClipRegion:
    return "invalid clip region";
  case Status::NearFamilyUnsupported:
    return "near family unsupported";
  case Status::CapacityExceeded:
    return "capacity exceeded";
  }
  return "unknown";
}

} // namespace spyro::cyclorama_portal_mesh
