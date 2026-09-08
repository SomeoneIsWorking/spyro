#include "world_hq_recipe.h"

#include "world_hq_refinement.h"
#include "world_material_codec.h"
#include "world_projection_math.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace spyro::world_hq_recipe {
namespace {

using psxport::native_projection::FixedAffine;
using psxport::native_projection::ProjectionParams;
using spyro::world_hq_refinement::HighVertex;
using spyro::world_hq_refinement::Parent;
using spyro::world_hq_refinement::Work;
using spyro::world_recipe::Face;
using spyro::world_recipe::Family;
using spyro::world_recipe::Origin;
using spyro::world_recipe::Recipe;

constexpr size_t kFaceLimit = 16384;

std::array<uint32_t, 4> highOffsets(uint32_t word) {
  return {(word >> 22) & 0x3fcu, (word >> 14) & 0x3fcu, (word >> 6) & 0x3fcu, (word << 2) & 0x3fcu};
}

bool appendDirect(const world_source::Source &input,
                  const world_chunk_codec::HighChunk &chunk,
                  const world_chunk_codec::HighFace &source,
                  const Parent &parent,
                  uint32_t textureCount,
                  uint32_t lqTextures,
                  Recipe &out,
                  const char *&why) {
  Face face{};
  face.family = parent.count == 4 ? Family::GT4 : Family::GT3;
  face.origin = Origin::HighDirect;
  face.vertexCount = parent.count;
  face.otBin = parent.otBin;
  face.sector = parent.sector;
  face.source = parent.source;
  face.sourceOrdinal = parent.ordinal;
  const auto colorOffsets = highOffsets(source.colorWord);
  const uint32_t fogEnd = (input.selection.lodDistance >> 2) + ((parent.flags & 1u) ? 0x2000u : 0u);
  for (uint32_t i = 0; i < parent.count; ++i) {
    // The quad packet is authored as 0,1,3,2. The guest uses 0,1,3 for
    // NCLIP, then writes t0 (source 3) before a3 (source 2) at
    // 0x80026DC8..0x80026DD4. The triangle arm stays 0,1,2.
    const uint32_t sourceIndex = parent.count == 4 && i >= 2 ? 5u - i : i;
    const uint32_t color = colorOffsets[sourceIndex] / 4u;
    if (color >= chunk.farColors.size() || color >= chunk.nearColors.size()) {
      why = "high_color_index";
      return false;
    }
    face.vertices[i] = parent.vertices[sourceIndex].projected;
    face.vertices[i].rgb = world_material_codec::fogColor(
        chunk.farColors[color], chunk.nearColors[color], fogEnd, face.vertices[i].sz);
  }

  const auto key =
      world_material_codec::classify((int8_t)parent.materialWord, (parent.materialWord >> 8) & 3u);
  face.material.textured = key.textured;
  face.material.semiTransparent = key.semiTransparent;
  if (key.textured) {
    const uint32_t record = lqTextures + (uint32_t)key.index * 16u;
    if (key.index >= textureCount || !input.materials.contains(record, 16u)) {
      why = "lq_texture_bounds";
      return false;
    }
    const auto fade = world_material_codec::directFade(
        world_hq_refinement::depthSum(parent.vertices, parent.count), fogEnd);
    const uint32_t tileAddress = record + fade.halfOffset;
    const world_material_codec::Tile tile{input.materials.r32(tileAddress),
                                          input.materials.r32(tileAddress + 4u)};
    world_hq_refinement::applyTile(
        face,
        parent.count == 4 ? world_material_codec::decodeQuad(tile, fade.step)
                          : world_material_codec::decodeTriangle(tile, key.orientation, fade.step));
  } else {
    face.family = parent.count == 4 ? Family::G4 : Family::G3;
  }
  if (!world_recipe::appendLinked(out, face, kFaceLimit)) {
    why = "face_capacity";
    return false;
  }
  return true;
}

AuditEntry auditEntry(const Parent &parent,
                      const FixedAffine &cameraMatrix,
                      const ProjectionParams &projection,
                      uint8_t chunkCommon,
                      uint8_t common,
                      Decision decision,
                      uint32_t depth = 0) {
  AuditEntry entry{.source = parent.source,
                   .sector = parent.sector,
                   .flags = parent.flags,
                   .depth = depth,
                   .tags = parent.tags,
                   .chunkCommonClip = chunkCommon,
                   .commonClip = common,
                   .decision = decision,
                   .cameraMatrix = cameraMatrix,
                   .projection = projection};
  for (uint32_t i = 0; i < parent.count; ++i) {
    const auto &vertex = parent.vertices[i];
    entry.vertices[i] = {.modelX = (int16_t)vertex.position.x,
                         .modelY = (int16_t)vertex.position.y,
                         .modelZ = (int16_t)vertex.position.z,
                         .sx = vertex.projected.sx,
                         .sy = vertex.projected.sy,
                         .sz = vertex.projected.sz};
  }
  return entry;
}

world_hq_refinement::Position projectionPosition(const world_chunk_codec::HighChunk &chunk,
                                                 const world_source::Camera &camera,
                                                 uint32_t packed) {
  const int32_t cameraX = camera.position[0] >> 2;
  const int32_t cameraY = camera.position[1] >> 2;
  const int32_t cameraZ = camera.position[2] >> 2;
  const int32_t x =
      (int32_t)(chunk.originWord >> 14) - cameraX + (int32_t)((packed >> 19) & 0x1ffcu);
  const int32_t y =
      cameraY - (int32_t)((chunk.originWord & 0xffffu) << 2) - (int32_t)((packed >> 8) & 0x1ffcu);
  const int32_t z =
      cameraZ - (int32_t)(chunk.originAndOffset >> 14) - (int32_t)((packed << 2) & 0x0ffcu);
  return {y, z, x};
}

bool classify(const world_source::Source &previous,
              const world_source::Source &input,
              const world_scene_prepare::Prepared &prepared,
              const world_projection_math::ProjectionStream &stream,
              int clipRight,
              Work &work,
              Recipe &out,
              const char *&why,
              Audit *audit) {
  const auto &cameraMatrix = stream.currentMatrix();
  const auto &projection = stream.parameters();
  const uint32_t lodDistance = input.selection.lodDistance;
  const uint32_t textureCount = input.materials.count();
  const uint32_t lqTextures = input.materials.lowBase();
  uint32_t ordinal = (uint32_t)out.faces.size();
  for (const world_scene_prepare::TaggedSector &selected : prepared.high) {
    const auto &sector = input.sectors[selected.index];
    const auto &previousSector = previous.sectors[selected.index];
    if (!sector || !previousSector || sector->highStatus != world_chunk_codec::Status::Ok ||
        previousSector->highStatus != world_chunk_codec::Status::Ok) {
      why = "high_chunk_decode";
      return false;
    }
    const auto &chunk = sector->high;
    const auto &previousChunk = previousSector->high;
    if (previousChunk.vertices.size() != chunk.vertices.size()) {
      why = "high_vertex_count";
      return false;
    }
    std::vector<HighVertex> vertices;
    vertices.reserve(chunk.vertices.size());
    for (size_t i = 0; i < chunk.vertices.size(); ++i) {
      const auto projected = world_hq_refinement::projectVertex(
          stream,
          projectionPosition(previousChunk, previous.selection.camera, previousChunk.vertices[i]),
          projectionPosition(chunk, input.selection.camera, chunk.vertices[i]),
          selected.tags,
          clipRight);
      if (!projected) {
        why = "high_projection_sample";
        return false;
      }
      vertices.push_back(*projected);
    }

    uint8_t chunkCommon = 0xffu;
    for (const HighVertex &vertex : vertices) {
      chunkCommon &= vertex.projected.clip;
    }
    if (selected.tags != 0u && (chunkCommon & 0x0fu)) {
      out.candidates += (uint32_t)chunk.faces.size();
      out.rejected += (uint32_t)chunk.faces.size();
      if (audit) {
        for (const world_chunk_codec::HighFace &source : chunk.faces) {
          audit->push_back({.source = source.address,
                            .sector = chunk.address,
                            .flags = source.flags,
                            .tags = selected.tags,
                            .chunkCommonClip = chunkCommon,
                            .commonClip = chunkCommon,
                            .decision = Decision::CommonClip});
        }
      }
      continue;
    }

    const uint32_t statusBase = 0x0006fcf4u + (chunk.originAndOffset & 0xffffu);
    for (uint32_t faceIndex = 0; faceIndex < chunk.faces.size(); ++faceIndex) {
      const world_chunk_codec::HighFace &source = chunk.faces[faceIndex];
      ++out.candidates;
      const auto offsets = highOffsets(source.vertexWord);
      Parent parent{};
      parent.sector = chunk.address;
      parent.source = source.address;
      parent.ordinal = ordinal++;
      parent.statusAddress = statusBase + faceIndex;
      parent.materialWord = source.materialWord;
      parent.flags = source.flags;
      parent.tags = selected.tags;
      parent.count = offsets[2] == offsets[3] ? 3u : 4u;
      uint8_t common = 0xffu;
      for (uint32_t i = 0; i < parent.count; ++i) {
        const uint32_t index = offsets[i] / 4u;
        if (index >= vertices.size()) {
          why = "high_vertex_index";
          return false;
        }
        parent.vertices[i] = vertices[index];
        parent.recheckFacing |= parent.vertices[i].requiresFacingCheck;
        common &= parent.vertices[i].projected.clip;
      }
      const auto colorOffsets = highOffsets(source.colorWord);
      for (uint32_t i = 0; i < parent.count; ++i) {
        const uint32_t color = colorOffsets[i] / 4u;
        if (color >= chunk.farColors.size() || color >= chunk.nearColors.size()) {
          why = "high_color_index";
          return false;
        }
        // Medium/near refinement rebuilds its color lattice from the first
        // HQ color plane at 0x8002792C/0x800286F0. Direct faces take the
        // separate per-vertex fog path in appendDirect().
        parent.vertices[i].projected.rgb = chunk.farColors[color];
      }
      // Every tagged path retains the negative status pointer that enables the
      // guest's per-face common-outcode test at 0x80026B9C. Only tag 0 clears
      // the sign after projection and branches around this predicate.
      if (selected.tags != 0u && (common & 0x0fu)) {
        if (audit) {
          audit->push_back(auditEntry(
              parent, cameraMatrix, projection, chunkCommon, common, Decision::CommonClip));
        }
        ++out.rejected;
        continue;
      }
      if (!world_hq_refinement::facing(parent.vertices, parent.count, parent.flags)) {
        if (audit) {
          audit->push_back(auditEntry(
              parent, cameraMatrix, projection, chunkCommon, common, Decision::Backface));
        }
        ++out.rejected;
        continue;
      }
      const uint32_t depth = world_hq_refinement::depthSum(parent.vertices, parent.count);
      if (!depth || depth >= lodDistance) {
        if (audit) {
          audit->push_back(auditEntry(parent,
                                      cameraMatrix,
                                      projection,
                                      chunkCommon,
                                      common,
                                      Decision::DepthRejected,
                                      depth));
        }
        ++out.rejected;
        continue;
      }
      const uint32_t bin = (depth >> 7) + ((parent.flags & 0x38u) >> 1);
      if (bin >= 0x800u || parent.statusAddress >= work.status.size()) {
        why = "high_bin_or_status";
        return false;
      }
      parent.otBin = (uint16_t)bin;
      if (depth >= 0x2000u || (parent.flags & 0x80u)) {
        if (audit) {
          audit->push_back(auditEntry(
              parent, cameraMatrix, projection, chunkCommon, common, Decision::Direct, depth));
        }
        work.status[parent.statusAddress] = 1u;
        if (!appendDirect(input, chunk, source, parent, textureCount, lqTextures, out, why)) {
          return false;
        }
        continue;
      }

      bool near = false;
      for (uint32_t i = 0; !(parent.flags & 0x40u) && i < parent.count; ++i) {
        near |= parent.vertices[i].projected.sz < 0x140u;
      }
      if (near) {
        if (audit) {
          audit->push_back(auditEntry(
              parent, cameraMatrix, projection, chunkCommon, common, Decision::Near, depth));
        }
        work.status[parent.statusAddress] = 4u;
        work.near.push_back(parent);
      } else {
        if (audit) {
          audit->push_back(auditEntry(
              parent, cameraMatrix, projection, chunkCommon, common, Decision::Medium, depth));
        }
        work.status[parent.statusAddress] = 2u;
        work.medium.push_back(parent);
      }
    }
  }
  return true;
}

} // namespace

bool append(const world_source::Source &previous,
            const world_source::Source &input,
            const world_scene_prepare::Prepared &prepared,
            const world_projection_math::ProjectionStream &projection,
            int clipRight,
            Recipe &out,
            const char *&why,
            Audit *audit) {
  Work work{};
  if (audit) {
    audit->clear();
  }
  return classify(previous, input, prepared, projection, clipRight, work, out, why, audit) &&
         world_hq_refinement::append(input.materials, projection, clipRight, work, out, why);
}

bool append(const world_source::Source &input,
            const world_scene_prepare::Prepared &prepared,
            const ProjectionParams &projection,
            int clipRight,
            Recipe &out,
            const char *&why,
            Audit *audit) {
  const world_projection_math::ProjectionStream stream(input.selection.camera.projectionMatrix,
                                                       projection);
  return append(input, input, prepared, stream, clipRight, out, why, audit);
}

} // namespace spyro::world_hq_recipe
