#include "field_shaded_queue_scene.h"

#include "actor_recipe_capture.h"
#include "actor_transform_math.h"
#include "core.h"

#include <array>
#include <cstdlib>
#include <optional>
#include <utility>

namespace spyro::field_shaded_queue_scene {
namespace {

constexpr uint32_t kQueue = 0x800720f4u;
constexpr uint32_t kQueueCapacity = 256u;
constexpr uint32_t kMeshTable = 0x80076378u;
constexpr uint32_t kShadowCursor = 0x80075f00u;
constexpr uint32_t kLightTable = 0x8007e44cu;
constexpr uint32_t kColourMatrix = 0x800770c8u;
constexpr uint32_t kScratchVertices = 0x1f800000u;
constexpr uint32_t kScratchEnd = kScratchVertices + 1024u;

int32_t extentRadius(uint16_t extent) {
  return (int32_t)(int8_t)extent * 256 + (int32_t)(extent & 0x100u) * 2;
}

bool coarseVisible(std::array<int32_t, 3> relative, int32_t radius) {
  return relative[0] + radius > 0 && relative[0] - radius < 0 && relative[1] + radius > 0 &&
         relative[1] - radius < 0 && relative[2] + radius > 0 && relative[2] - radius < 0;
}

bool firstViewGate(std::array<int32_t, 3> view, int32_t radius) {
  return view[2] - radius < 0 && view[2] + 128 > 0 &&
         (std::abs(view[0]) - 102) * 4 - (view[2] + 77) * 3 < 0;
}

bool finalViewGate(std::array<int32_t, 3> view) {
  return view[2] + 40 - (std::abs(view[1]) - 121) * 3 > 0;
}

bool whollyInside(std::array<int32_t, 3> view) {
  return (std::abs(view[0]) + 102) * 4 - (view[2] - 77) * 3 < 0 &&
         view[2] - 40 - (std::abs(view[1]) + 121) * 3 >= 1;
}

psxport::native_projection::ModelVertex decodeVertex(Core *core, uint32_t address) {
  const uint32_t raw = core->mem_r32(address);
  const int32_t z = (int32_t)(raw << 24) >> 23;
  const int32_t x = (int32_t)(raw << 16) >> 23;
  const int32_t y = (int32_t)(raw << 8) >> 23;
  const uint32_t xy = (uint32_t)x + ((uint32_t)y << 16);
  return {(int16_t)xy, (int16_t)(xy >> 16), (int16_t)z};
}

struct MeshSource {
  uint16_t index = 0;
  uint32_t address = 0;
  uint32_t vertices = 0;
  uint32_t stream = 0;
  uint32_t vertexCount = 0;
  uint32_t primitiveCount = 0;
  int32_t lightingOffset = 0;
  uint32_t lightBase = 0;
  uint32_t lightScale = 0;
  uint32_t vertexColourBase = 0;
};

std::optional<MeshSource> inspectMesh(Core *core, uint32_t actor) {
  MeshSource source{};
  source.index = core->mem_r16(actor + 0x36u);
  source.address = core->mem_r32(kMeshTable + (uint32_t)source.index * 4u);
  if (source.address == 0u || !actor_recipe_capture::physical_span(source.address, 16u)) {
    return std::nullopt;
  }
  source.vertexCount = core->mem_r8(source.address);
  source.primitiveCount = core->mem_r8(source.address + 1u);
  source.vertices = core->mem_r32(source.address + 4u) & 0x7fffffffu;
  source.stream = core->mem_r32(source.address + 12u);
  if (source.vertexCount == 0u || source.vertexCount > 127u ||
      !actor_recipe_capture::physical_span(source.vertices, source.vertexCount * 3u + 1u) ||
      !actor_recipe_capture::physical_span(source.stream, source.primitiveCount * 8u)) {
    return std::nullopt;
  }
  source.lightingOffset = (int32_t)core->mem_r32(actor + 0x4cu) >> 21;
  const uint32_t light = kLightTable + (uint32_t)source.lightingOffset;
  if (!actor_recipe_capture::physical_span(light, 8u)) {
    return std::nullopt;
  }
  source.lightBase = core->mem_r32(light);
  source.lightScale = core->mem_r32(light + 4u);
  if (source.vertices == source.address + 16u) {
    source.vertexColourBase = kScratchVertices + source.vertexCount * 4u;
  }
  return source;
}

Status reset(Frame &frame, Status status) {
  frame = {};
  return status;
}

} // namespace

Status prepare(Core *core, int32_t clipRight, Frame &frame) {
  frame = {};
  if (core == nullptr || clipRight <= 0) {
    return Status::InvalidQueue;
  }
  frame.input.clipRight = clipRight;
  if (!core->rsub.projParams.geomValid()) {
    return Status::InvalidQueue;
  }
  frame.input.projection = {.ofx = (int32_t)(core->rsub.projParams.geomOfx() * 65536.0f),
                            .ofy = (int32_t)(core->rsub.projParams.geomOfy() * 65536.0f),
                            .h = (uint16_t)core->rsub.projParams.geomH(),
                            .dqa = 0,
                            .dqb = 0};
  const int16_t colourA = (int16_t)core->mem_r32(kColourMatrix);
  const int16_t colourB = (int16_t)core->mem_r32(kColourMatrix + 4u);
  const int16_t colourC = (int16_t)core->mem_r32(kColourMatrix + 8u);
  frame.input.colourMatrix = {
      {{colourA, colourB, colourC}, {colourA, colourB, colourC}, {colourA, colourB, colourC}}};
  frame.shadowCursor = core->mem_r32(kShadowCursor);
  if (!actor_recipe_capture::physical_span(frame.shadowCursor, 8u)) {
    return reset(frame, Status::InvalidShadowCursor);
  }
  const auto camera = actor_transform_math::readCameraMatrix(core);
  for (uint32_t qi = 0; qi < kQueueCapacity; ++qi) {
    const uint32_t actor = core->mem_r32(kQueue + qi * 4u);
    if (actor == 0u) {
      return Status::Ready;
    }
    ++frame.queueRecords;
    if (!actor_recipe_capture::physical_span(actor, 0x58u)) {
      return reset(frame, Status::InvalidActor);
    }
    if ((core->mem_r8(actor + 0x50u) & 0x80u) != 0u) {
      ++frame.screenRecords;
      continue;
    }
    frame.visitedWorldActors.push_back(actor);
    const uint32_t meshAddress =
        core->mem_r32(kMeshTable + (uint32_t)core->mem_r16(actor + 0x36u) * 4u);
    const auto mesh = inspectMesh(core, actor);
    if (meshAddress == 0u) {
      ++frame.nullMeshes;
    } else if (mesh) {
      ++frame.validMeshRecords;
      frame.validMeshPrimitiveCandidates += mesh->primitiveCount;
      frame.sourceMeshIndices.push_back(mesh->index);
      frame.sourceLightingOffsets.push_back(mesh->lightingOffset);
    }
    const int32_t radius = extentRadius(core->mem_r16(actor + 0x50u));
    const auto relative = actor_transform_math::cameraRelativePosition(core, actor);
    if (!coarseVisible(relative, radius)) {
      ++frame.culled;
      continue;
    }
    std::array<int32_t, 3> view{};
    auto affine = actor_transform_math::worldAffine(core, actor, camera, view);
    if (!firstViewGate(view, radius)) {
      ++frame.culled;
      continue;
    }
    if (!mesh) {
      return reset(frame, Status::InvalidMesh);
    }
    if ((int32_t)core->mem_r32(actor + 0x1cu) < 0 && view[2] < -0x1100) {
      if (!actor_recipe_capture::physical_span(
              frame.shadowCursor + (uint32_t)frame.shadows.size() * 8u, 8u)) {
        return reset(frame, Status::InvalidShadowCursor);
      }
      frame.shadows.push_back(
          {.actor = actor, .modelByte = (uint32_t)(int8_t)core->mem_r8(mesh->address + 2u)});
    }
    if (!finalViewGate(view)) {
      ++frame.culled;
      continue;
    }
    if (core->mem_r8(actor + 0x57u) != 0u) {
      return reset(frame, Status::UnsupportedTransformScale);
    }

    field_shaded_queue_recipe::Record record{.actor = actor,
                                             .actorOrdinal = qi,
                                             .meshIndex = mesh->index,
                                             .clipMode = !whollyInside(view),
                                             .lightingOffset = mesh->lightingOffset,
                                             .lightBase = mesh->lightBase,
                                             .lightScale = mesh->lightScale,
                                             .affine = affine};
    record.vertices.reserve(mesh->vertexCount);
    for (uint32_t i = 0; i < mesh->vertexCount; ++i) {
      record.vertices.push_back(decodeVertex(core, mesh->vertices + i * 3u));
    }
    record.primitives.reserve(mesh->primitiveCount);
    for (uint32_t i = 0; i < mesh->primitiveCount; ++i) {
      field_shaded_queue_recipe::Primitive primitive{
          .indices = core->mem_r32(mesh->stream + i * 8u),
          .normal = core->mem_r32(mesh->stream + i * 8u + 4u)};
      if ((primitive.normal & 3u) == 0u) {
        if (mesh->vertexColourBase == 0u) {
          return reset(frame, Status::UnsupportedVertexLighting);
        }
        const std::array<uint32_t, 4> offsets = {(primitive.normal >> 21) & 508u,
                                                 (primitive.normal >> 14) & 508u,
                                                 (primitive.normal >> 7) & 508u,
                                                 primitive.normal & 508u};
        for (uint32_t vertex = 0; vertex < 4u; ++vertex) {
          const uint32_t colour = mesh->vertexColourBase + offsets[vertex];
          if (colour < kScratchVertices || colour > kScratchEnd - 4u) {
            return reset(frame, Status::InvalidMesh);
          }
          primitive.vertexColours[vertex] = core->mem_r32(colour);
        }
      }
      record.primitives.push_back(primitive);
    }
    frame.primitiveCandidates += mesh->primitiveCount;
    frame.transformedActors.push_back(actor);
    frame.input.records.push_back(std::move(record));
  }
  return reset(frame, Status::UnterminatedQueue);
}

void commit(Core *core, const Frame &frame) {
  for (uint32_t actor : frame.visitedWorldActors) {
    core->mem_w8(actor + 0x51u, 0u);
  }
  for (uint32_t actor : frame.transformedActors) {
    core->mem_w8(actor + 0x51u, 1u);
  }
  for (uint32_t i = 0; i < frame.shadows.size(); ++i) {
    const uint32_t out = frame.shadowCursor + i * 8u;
    core->mem_w32(out, frame.shadows[i].actor);
    core->mem_w32(out + 4u, frame.shadows[i].modelByte);
  }
  core->mem_w32(kShadowCursor, frame.shadowCursor + (uint32_t)frame.shadows.size() * 8u);
}

const char *statusName(Status status) {
  switch (status) {
  case Status::Ready:
    return "ready";
  case Status::InvalidQueue:
    return "invalid queue";
  case Status::UnterminatedQueue:
    return "unterminated queue";
  case Status::InvalidActor:
    return "invalid actor";
  case Status::InvalidMesh:
    return "invalid mesh";
  case Status::InvalidShadowCursor:
    return "invalid shadow cursor";
  case Status::UnsupportedTransformScale:
    return "unsupported transform scale";
  case Status::UnsupportedVertexLighting:
    return "unsupported vertex lighting";
  }
  return "unknown";
}

} // namespace spyro::field_shaded_queue_scene
