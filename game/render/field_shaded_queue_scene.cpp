#include "field_shaded_queue_scene.h"

#include "actor_recipe_capture.h"
#include "actor_scene_builder.h"
#include "actor_transform_math.h"
#include "core.h"
#include "gpu_vk.h"
#include "native_projection.h"
#include "shaded_moby_light.h"
#include "wide_screen_space.h"

#include <lucent/log.h>

#include <array>
#include <cstdlib>
#include <optional>
#include <utility>

namespace spyro::field_shaded_queue_scene {
namespace {

constexpr uint32_t kQueue = 0x800720f4u;
constexpr uint32_t kQueueCapacity = 256u;
constexpr uint32_t kMeshTable = 0x80076378u;
constexpr uint32_t kLightTable = 0x8006e44cu;
constexpr uint32_t kLightTableVariantOne = 0x8006e3d8u;
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

// 0x80022BE4..0x80022C20: depth, then the horizontal plane. Both answers come from one view: the
// guest's is retail's 512-px plane, the drawn one widens it with the rule every pass shares.
struct FirstGate {
  bool guest = false;
  bool drawn = false;
};

FirstGate firstViewGate(std::array<int32_t, 3> view, int32_t radius, int32_t drawWidth) {
  const bool depth = view[2] - radius < 0 && view[2] + 128 > 0;
  const int32_t extent = std::abs(view[0]) - 102, planeDepth = view[2] + 77;
  return {.guest = depth && wide::viewHorizontalInside(extent, planeDepth, wide::kNativeClipWidth),
          .drawn = depth && wide::drawnHorizontalInside(extent, planeDepth, drawWidth)};
}

bool finalViewGate(std::array<int32_t, 3> view) {
  return view[2] + 40 - (std::abs(view[1]) - 121) * 3 > 0;
}

bool whollyInside(std::array<int32_t, 3> view, int32_t drawWidth) {
  return wide::viewHorizontalInside(std::abs(view[0]) + 102, view[2] - 77, drawWidth) &&
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
  uint32_t lightEntry = 0;
  int32_t lightEntryIndex = 0;
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
  // The other arm's table, from the SAME guest word at a different shift (`>> 22`, signed, matching
  // 0x80023548's `sra $t7, $t7, 22`). One word, not a pair.
  {
    const int32_t variantOneOffset = (int32_t)core->mem_r32(actor + 0x4cu) >> 22;
    const uint32_t lightOne = kLightTableVariantOne + (uint32_t)variantOneOffset;
    if (!actor_recipe_capture::physical_span(lightOne, 4u)) {
      return std::nullopt;
    }
    source.lightEntry = core->mem_r32(lightOne);
    source.lightEntryIndex = variantOneOffset;
  }
  if (source.vertices == source.address + 16u) {
    source.vertexColourBase = kScratchVertices + source.vertexCount * 4u;
  }
  return source;
}

// The per-vertex colour words a Gouraud primitive indexes, by BYTE offset (`vertex * 4`, the
// guest's own stride). A model with its vertex list behind the header keeps them in guest memory at
// `vertexColourBase`; any other model has normals behind the header and retail lights them
// (`shaded_moby_light.h`), so they are derived here from the record's own rotation.
class VertexColours {
public:
  VertexColours(Core *core,
                const MeshSource &mesh,
                const field_shaded_queue_recipe::Record &record,
                const shaded_light::Matrix3 &colourMatrix)
      : core_(core), base_(mesh.vertexColourBase) {
    if (base_ != 0u) {
      return;
    }
    std::vector<std::array<std::int8_t, 3>> normals(mesh.vertexCount);
    for (uint32_t i = 0; i < mesh.vertexCount; ++i) {
      const uint32_t at = mesh.address + 16u + i * 3u;
      normals[i] = {(std::int8_t)core->mem_r8(at),
                    (std::int8_t)core->mem_r8(at + 1u),
                    (std::int8_t)core->mem_r8(at + 2u)};
    }
    lit_ = shaded_light::vertexColours(
        {.rotation = record.affine.m, .colourMatrix = colourMatrix, .entry = mesh.lightEntry},
        normals);
  }

  std::optional<uint32_t> at(uint32_t byteOffset) const {
    if (base_ != 0u) {
      const uint32_t address = base_ + byteOffset;
      if (address < kScratchVertices || address > kScratchEnd - 4u) {
        return std::nullopt;
      }
      return core_->mem_r32(address);
    }
    if (byteOffset / 4u >= lit_.size()) {
      return std::nullopt;
    }
    return lit_[byteOffset / 4u];
  }

private:
  Core *core_;
  uint32_t base_;
  std::vector<uint32_t> lit_;
};

// The model's vertices and primitives, read from the guest's mesh exactly as 0x80022FEC onward
// does. That tail is shared by the world path and the screen-space path, so both records come
// through here.
Status fillGeometry(Core *core,
                    const MeshSource &mesh,
                    const shaded_light::Matrix3 &colourMatrix,
                    field_shaded_queue_recipe::Record &record) {
  record.vertices.reserve(mesh.vertexCount);
  for (uint32_t i = 0; i < mesh.vertexCount; ++i) {
    record.vertices.push_back(decodeVertex(core, mesh.vertices + i * 3u));
  }
  if (mesh.vertexColourBase == 0u &&
      !actor_recipe_capture::physical_span(mesh.address + 16u, mesh.vertexCount * 3u)) {
    return Status::InvalidMesh;
  }
  const VertexColours colours(core, mesh, record, colourMatrix);
  record.primitives.reserve(mesh.primitiveCount);
  for (uint32_t i = 0; i < mesh.primitiveCount; ++i) {
    field_shaded_queue_recipe::Primitive primitive{.indices = core->mem_r32(mesh.stream + i * 8u),
                                                   .normal =
                                                       core->mem_r32(mesh.stream + i * 8u + 4u)};
    if ((primitive.indices & 3u) == 0u) {
      const std::array<uint32_t, 4> offsets = {(primitive.normal >> 21) & 508u,
                                               (primitive.normal >> 14) & 508u,
                                               (primitive.normal >> 7) & 508u,
                                               primitive.normal & 508u};
      for (uint32_t vertex = 0; vertex < 4u; ++vertex) {
        const auto colour = colours.at(offsets[vertex]);
        if (!colour) {
          return Status::InvalidMesh;
        }
        primitive.vertexColours[vertex] = *colour;
      }
    }
    record.primitives.push_back(primitive);
  }
  return Status::Ready;
}

// The record of a screen-space Moby: the lighting words every Moby carries, and the two things the
// screen-space path changes, its view (`screenSpaceAffine`) and its projection centre.
field_shaded_queue_recipe::Record
screenSpaceRecord(Core *core,
                  uint32_t actor,
                  uint32_t ordinal,
                  const MeshSource &mesh,
                  const psxport::native_projection::ProjectionParams &frameProjection) {
  const auto centre = actor_transform_math::screenSpaceCentre(core, actor);
  // The guest's x is in its own 512-wide frame. The drawn frame's projection is about the widened
  // centre, so this moves by the same horizontal offset every world Moby already carries and
  // `field_shaded_queue_submitter`'s anchor correction (defined against exactly that shift)
  // applies.
  auto projection = frameProjection;
  projection.ofx = (centre.x + wide_screen_space::horizontalOffsetDelta(core)) << 16;
  projection.ofy = centre.y << 16;
  lucent::debug("shadedscreen",
                "actor=0x{:08X} mesh={} centre=({}, {}) drawnCentreX={} viewZ={} vertexColours={}",
                actor,
                mesh.index,
                centre.x,
                centre.y,
                projection.ofx >> 16,
                (int32_t)core->mem_r32(actor + 20u) >> 1,
                mesh.vertexColourBase != 0u ? "authored" : "lit");
  return {.actor = actor,
          .actorOrdinal = ordinal,
          .meshIndex = mesh.index,
          .clipMode = false,
          .lightBase = mesh.lightBase,
          .lightScale = mesh.lightScale,
          .lightEntry = mesh.lightEntry,
          .lightEntryIndex = mesh.lightEntryIndex,
          .affine = actor_transform_math::screenSpaceAffine(core, actor),
          .projection = projection,
          .depthOffset = (int32_t)(int8_t)core->mem_r8(actor + 0x47u)};
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
  const int32_t center = gpu_vk_wide_engine(core) ? gpu_vk_wide_engine_ofx(core)
                                                  : (int32_t)core->rsub.projParams.geomOfx();
  frame.input.projection = {.ofx = center << 16,
                            .ofy = (int32_t)(core->rsub.projParams.geomOfy() * 65536.0f),
                            .h = (uint16_t)core->rsub.projParams.geomH(),
                            .dqa = 0,
                            .dqb = 0};
  const int16_t colourA = (int16_t)core->mem_r32(kColourMatrix);
  const int16_t colourB = (int16_t)core->mem_r32(kColourMatrix + 4u);
  const int16_t colourC = (int16_t)core->mem_r32(kColourMatrix + 8u);
  frame.input.colourMatrix = {
      {{colourA, colourB, colourC}, {colourA, colourB, colourC}, {colourA, colourB, colourC}}};
  frame.shadowCursor = core->mem_r32(moby_shadow_list::kCursor);
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
    // Byte 0x50 is `m_RenderRadius` (external/spyro-1/include/moby.h:133), and bit 7 of it is what
    // retail branches on: `sll $a0,$a0,24; bltz $a0, .L80022D1C` at 0x80022B34/0x80022B38 sends the
    // Moby down the SCREEN-SPACE path before any culling. Every `g_Hud` Moby and every HUD glyph
    // carries 0xFF there. The skip that used to sit here dropped them, and its removal alone sent
    // them into the WORLD path below, which culls them against the world camera: their positions
    // are screen coordinates, so they come out some 40000 units from it and nothing was drawn.
    const bool screenSpace = actor_transform_math::isScreenSpace(core->mem_r8(actor + 0x50u));
    if (screenSpace) {
      ++frame.screenSpaceActors;
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
    if (screenSpace) {
      if (!mesh) {
        return reset(frame, Status::InvalidMesh);
      }
      // 0x80022D98 marks the Moby drawn; there is no shadow and no clip gate on this path.
      frame.transformedActors.push_back(actor);
      auto record = screenSpaceRecord(core, actor, qi, *mesh, frame.input.projection);
      if (const Status geometry = fillGeometry(core, *mesh, frame.input.colourMatrix, record);
          geometry != Status::Ready) {
        return reset(frame, geometry);
      }
      frame.primitiveCandidates += mesh->primitiveCount;
      frame.input.records.push_back(std::move(record));
      continue;
    }
    const int32_t radius = extentRadius(core->mem_r16(actor + 0x50u));
    const auto relative = actor_transform_math::cameraRelativePosition(core, actor);
    if (!coarseVisible(relative, radius)) {
      ++frame.culled;
      continue;
    }
    std::array<int32_t, 3> view{};
    auto affine = actor_transform_math::worldAffine(core, actor, camera, view);
    const FirstGate first = firstViewGate(view, radius, clipRight);
    if (!first.drawn) {
      ++frame.culled;
      continue;
    }
    if (!mesh) {
      return reset(frame, Status::InvalidMesh);
    }
    // 0x80022C2C..0x80022C44 appends the shadow after the horizontal plane and before the vertical
    // one, when m_ShadowDistance is negative and the view depth is nearer than the staging limit.
    //
    // THE LIMIT IS 0x1100, AND THAT IS READ FROM RETAIL'S OWN WORDS, NOT INFERRED. The two passes
    // emit the identical idiom, and differ only in the constant:
    //
    //   regular  0x8001F344  bgez $t3, skip        ; 0x8001F34C addi $t3,$v1,-0x1200
    //                                      0x8001F350  bgez $t3, skip   -> stage iff viewZ < 0x1200
    //   shaded   0x80022C2C  bgez $a0, skip        ; 0x80022C30  addi $a0,$v1,-0x1100
    //                                      0x80022C34  bgez $a0, skip   -> stage iff viewZ < 0x1100
    //
    // `$v1` is a positive GTE depth — `mfc2 $v1, $k1, 0` at 0x80022BD8, and the far bounds around
    // it are `sub $a1, $v1, $a0; bgez` (skip when the difference is >= 0), which only reads as a
    // far bound if the depth is positive. So both pairs are NEAR bounds and the shaded pass stages
    // shadows. An earlier revision of this file claimed the pair was unsatisfiable and that the
    // shaded pass staged nothing; that was read off this project's own prose comment rather than
    // off the bytes, and the disassembly above refutes it. Issue 0154.
    const moby_shadow_list::Entry shadow{
        .moby = actor, .radius = (uint32_t)(int8_t)core->mem_r8(mesh->address + 2u)};
    const bool stages = actor_scene::stages_shadow(
        (int32_t)core->mem_r32(actor + 0x1cu), view[2], actor_scene::kShadedShadowStagingDepth);
    if (stages && first.guest) {
      if (!actor_recipe_capture::physical_span(
              frame.shadowCursor + (uint32_t)frame.shadows.size() * 8u, 8u)) {
        return reset(frame, Status::InvalidShadowCursor);
      }
      frame.shadows.push_back({.actor = shadow.moby, .modelByte = shadow.radius});
    }
    if (stages) {
      frame.drawnShadows.push_back(shadow);
    }
    if (!finalViewGate(view)) {
      ++frame.culled;
      continue;
    }
    if (first.guest) {
      frame.transformedActors.push_back(actor);
    }
    field_shaded_queue_recipe::Record record{.actor = actor,
                                             .actorOrdinal = qi,
                                             .meshIndex = mesh->index,
                                             .clipMode = !whollyInside(view, clipRight),
                                             .lightBase = mesh->lightBase,
                                             .lightScale = mesh->lightScale,
                                             .lightEntry = mesh->lightEntry,
                                             .lightEntryIndex = mesh->lightEntryIndex,
                                             .affine = affine,
                                             .depthOffset =
                                                 (int32_t)(int8_t)core->mem_r8(actor + 0x47u)};
    if (const Status geometry = fillGeometry(core, *mesh, frame.input.colourMatrix, record);
        geometry != Status::Ready) {
      return reset(frame, geometry);
    }
    frame.primitiveCandidates += mesh->primitiveCount;
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
  core->mem_w32(moby_shadow_list::kCursor,
                frame.shadowCursor + (uint32_t)frame.shadows.size() * 8u);
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
  }
  return "unknown";
}

} // namespace spyro::field_shaded_queue_scene
