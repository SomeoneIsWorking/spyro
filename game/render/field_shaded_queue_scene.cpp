#include "field_shaded_queue_scene.h"

#include "actor_recipe_capture.h"
#include "actor_scene_builder.h"
#include "actor_transform_math.h"
#include "core.h"
#include "gpu_vk.h"

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
    // THE 0x50 BIT-7 SKIP IS GONE, and it was never justified. It was added in a bulk commit with
    // no comment and no cited evidence, and it contradicts the guest's own struct: byte 0x50 is
    // `m_RenderRadius` -- "Radius of the Moby for clipping purposes (>> 2)", a `u_char`
    // (external/spyro-1/include/moby.h:133) -- with `m_WasDrawn` at 0x51. Bit 7 of a clipping
    // radius is simply a large radius, not a flag.
    //
    // It was not cosmetic. `hud_text_builder.cpp` writes 0xFF at 0x50 for every glyph, because a
    // glyph wants the largest radius, so EVERY HUD glyph moby was dropped here -- which is why the
    // pause menu drew a panel and a border and NO CAPTIONS, and why the level-transition tally's
    // captions are missing too (same route). Any moby whose render radius is >= 128 was dropped as
    // well.
    //
    // The counter is kept, renamed to what it can honestly mean, because a diagnostic that silently
    // stops reporting a population is worse than one that reports it under a truthful name.
    if ((core->mem_r8(actor + 0x50u) & 0x80u) != 0u) {
      ++frame.largeRadiusActors;
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
    const FirstGate first = firstViewGate(view, radius, clipRight);
    if (!first.drawn) {
      ++frame.culled;
      continue;
    }
    if (!mesh) {
      return reset(frame, Status::InvalidMesh);
    }
    // 0x80022C2C..0x80022C44 appends the shadow after the horizontal plane and before the vertical
    // one, when m_ShadowDistance is negative and the view depth passes the staging limit.
    //
    // THE SIGN IS RETAIL'S OWN AND IS DELIBERATELY PRESERVED. 0x80022C30 computes
    // `addi $a0,$v1,-0x1100` and branches `bgez` past the append, and view[2] is a positive depth,
    // so this pair is unsatisfiable and this pass stages NO shadow — at 4:3 as much as at 16:9.
    // That is a real fidelity defect against retail, but it is not a widescreen one: it is visible
    // at the native aspect, where this change must be a no-op. Fixing it is a separate finding, so
    // the negated limit is passed through verbatim rather than quietly corrected here.
    const moby_shadow_list::Entry shadow{
        .moby = actor, .radius = (uint32_t)(int8_t)core->mem_r8(mesh->address + 2u)};
    const bool stages =
        actor_scene::stages_shadow((int32_t)core->mem_r32(actor + 0x1cu), view[2], -0x1100);
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
    record.vertices.reserve(mesh->vertexCount);
    for (uint32_t i = 0; i < mesh->vertexCount; ++i) {
      record.vertices.push_back(decodeVertex(core, mesh->vertices + i * 3u));
    }
    record.primitives.reserve(mesh->primitiveCount);
    for (uint32_t i = 0; i < mesh->primitiveCount; ++i) {
      field_shaded_queue_recipe::Primitive primitive{
          .indices = core->mem_r32(mesh->stream + i * 8u),
          .normal = core->mem_r32(mesh->stream + i * 8u + 4u)};
      if ((primitive.indices & 3u) == 0u) {
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
  case Status::UnsupportedVertexLighting:
    return "unsupported vertex lighting";
  }
  return "unknown";
}

} // namespace spyro::field_shaded_queue_scene
