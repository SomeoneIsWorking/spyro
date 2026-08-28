#pragma once

#include "native_projection.h"

#include <array>
#include <cstdint>
#include <vector>

class Core;

namespace spyro::cyclorama_portal_mesh {

constexpr uint32_t kProducerKey = 0x80050240u;
constexpr uint32_t kNearProducerKey = 0x8004f4bcu;
constexpr uint32_t kCamera = 0x80076dd0u;
constexpr uint32_t kLevelTicks = 0x800758c8u;
constexpr uint32_t kSineTable = 0x8006cbf8u;
constexpr uint32_t kMagnitudeTable = 0x80074b84u;
constexpr uint32_t kPortalPointCapacity = 16u;
constexpr uint32_t kObjectCapacity = 256u;
constexpr uint32_t kFaceCapacity = 16384u;

enum class Status : uint8_t {
  Ready,
  ValidEmpty,
  InvalidCore,
  ProjectionUnset,
  InvalidPortal,
  InvalidPointCount,
  InvalidAsset,
  InvalidObject,
  InvalidVertexSpan,
  InvalidFaceSpan,
  InvalidFaceIndex,
  InvalidClipRegion,
  NearFamilyUnsupported,
  CapacityExceeded,
};

struct ClipEdge {
  int32_t x0 = 0;
  int32_t y0 = 0;
  int32_t x1 = 0;
  int32_t y1 = 0;
};

struct Vertex {
  int16_t sx = 0;
  int16_t sy = 0;
  uint16_t sz = 0;
  float screenX = 0.0f;
  float screenY = 0.0f;
  float viewZ = 0.0f;
  uint32_t rgb = 0;
  uint8_t clip = 0;
};

struct Face {
  uint32_t object = 0;
  uint32_t source = 0;
  uint32_t sourceOrdinal = 0;
  std::array<Vertex, 3> vertices{};
  bool gouraud = false;
};

struct PortalFrame {
  Status status = Status::InvalidCore;
  const char *refusal = "invalid_core";
  uint32_t portal = 0;
  uint32_t asset = 0;
  uint32_t portalOrdinal = 0;
  uint32_t pointCount = 0;
  uint32_t distance = 0;
  uint32_t distanceShift = 0;
  uint32_t fadeFactor = 0;
  uint32_t tintColor = 0;
  bool maskVisible = false;
  int32_t clipLeft = 0;
  int32_t clipTop = 0;
  int32_t clipRight = 0;
  int32_t clipBottom = 0;
  uint16_t otBin = 0;
  psxport::native_projection::FixedAffine cullMatrix{};
  psxport::native_projection::FixedAffine projectionMatrix{};
  std::vector<ClipEdge> edges;
};

struct Recipe {
  Status status = Status::InvalidCore;
  const char *refusal = "invalid_core";
  uint32_t portal = 0;
  uint32_t asset = 0;
  uint32_t assetObjects = 0;
  uint32_t authoredCandidates = 0;
  uint32_t survivingObjects = 0;
  uint32_t projectedVertices = 0;
  uint32_t candidates = 0;
  uint32_t boxRejected = 0;
  uint32_t apertureRejected = 0;
  uint32_t sourceAccepted = 0;
  uint32_t emittedTriangles = 0;
  std::vector<Face> faces;
};

// Pure/read-only transcription of the reached mid-distance portal path in
// resident owner 0x80050BD0. It projects and contracts the authored portal
// aperture, constructs the exact 0x80077EA0 half-plane class, and derives the
// spin/projection matrices passed to 0x80050240.
PortalFrame prepareFrame(
    Core *core, uint32_t portal, uint32_t portalOrdinal, uint32_t nextYaw, int32_t nextPitch);

// Pure/read-only native recipe for the tinted static mesh family 0x80050240.
// Source faces are clipped to the prepared portal aperture before publication.
Recipe build(Core *core, const PortalFrame &frame);

const char *statusName(Status status);

} // namespace spyro::cyclorama_portal_mesh
