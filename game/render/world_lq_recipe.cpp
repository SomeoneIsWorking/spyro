#include "world_lq_recipe.h"

#include "world_projection_math.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace spyro::world_lq_recipe {
namespace {

using psxport::native_projection::FixedAffine;
using psxport::native_projection::NativeProjectedVertex;
using psxport::native_projection::ProjectionParams;
using spyro::world_recipe::Face;
using spyro::world_recipe::Family;
using spyro::world_recipe::Origin;
using spyro::world_recipe::Recipe;
using spyro::world_recipe::Vertex;

constexpr uint32_t kEnvironment = 0x800785A8u;
constexpr uint32_t kCamera = 0x80076DD0u;
constexpr uint32_t kSkipLow = 0x8007591Cu;
constexpr size_t kFaceLimit = 16384;

uint8_t clipCode(int16_t sx, int16_t sy, int right) {
  uint8_t out = 0;
  if (sy <= 0) {
    out |= 1u;
  }
  if (sy >= 256) {
    out |= 2u;
  }
  if (sx < 0 || sx >= right) {
    out |= 4u;
  }
  return out;
}

std::array<uint32_t, 4> indices(uint32_t word) {
  return {(word >> 26) & 0x3fu, (word >> 20) & 0x3fu, (word >> 14) & 0x3fu, (word >> 8) & 0x3fu};
}

bool projectVertices(const world_chunk_codec::LowChunk &chunk,
                     const FixedAffine &cameraMatrix,
                     const ProjectionParams &projection,
                     int32_t cameraX,
                     int32_t cameraY,
                     int32_t cameraZ,
                     uint8_t tags,
                     int clipRight,
                     std::vector<Vertex> &out) {
  out.clear();
  out.reserve(chunk.vertices.size());
  uint8_t common = 0xffu;
  for (uint32_t packed : chunk.vertices) {
    const int32_t vx =
        cameraY - (int32_t)(uint16_t)chunk.originWord - (int32_t)((packed >> 10) & 0x7ffu);
    const int32_t vy = cameraZ - (int32_t)chunk.originZ - (int32_t)(packed & 0x3ffu);
    const int32_t vz =
        (int32_t)(packed >> 21) + (int32_t)(uint16_t)(chunk.originWord >> 16) - cameraX;
    const auto input = world_projection_math::packProjectionInput(vx, vy, vz);
    const NativeProjectedVertex projected =
        psxport::native_projection::project(cameraMatrix, projection, input);
    Vertex vertex{};
    vertex.sx = projected.sx;
    vertex.sy = projected.sy;
    vertex.sz = projected.sz;
    vertex.clip = tags & 1u ? clipCode(vertex.sx, vertex.sy, clipRight) : 0u;
    vertex.screenX = projected.px;
    vertex.screenY = projected.py;
    vertex.viewZ = projected.pz;
    common &= vertex.clip;
    out.push_back(vertex);
  }
  return !(tags & 1u) || !(common & 0x0fu);
}

bool appendFace(const world_chunk_codec::LowChunk &chunk,
                const world_chunk_codec::LowFace &source,
                const std::vector<Vertex> &vertices,
                uint8_t tags,
                uint32_t farLimit,
                uint32_t lodBase,
                uint32_t &ordinal,
                Recipe &out,
                const char *&why) {
  ++out.candidates;
  const auto vertexIndices = indices(source.vertexWord);
  const auto colorIndices = indices(source.materialWord);
  const bool triangle = vertexIndices[2] == vertexIndices[3];
  const uint32_t count = triangle ? 3u : 4u;

  Face face{};
  face.family = triangle ? Family::G3 : Family::G4;
  face.origin = Origin::LowDirect;
  face.vertexCount = (uint8_t)count;
  face.sector = chunk.address;
  face.source = source.address;
  face.sourceOrdinal = ordinal++;
  uint8_t clips = 0xffu;
  for (uint32_t i = 0; i < count; ++i) {
    if (vertexIndices[i] >= vertices.size() || colorIndices[i] >= chunk.colors.size()) {
      why = "low_face_index";
      return false;
    }
    face.vertices[i] = vertices[vertexIndices[i]];
    face.vertices[i].rgb = chunk.colors[colorIndices[i]];
    clips &= face.vertices[i].clip;
  }
  if ((tags & 1u) && (clips & (triangle ? 0x1fu : 0x0fu))) {
    ++out.rejected;
    return true;
  }

  const uint32_t flags = source.vertexWord & 0xffu;
  const int32_t firstArea =
      world_projection_math::nclip(face.vertices[0], face.vertices[1], face.vertices[2]);
  bool facing = false;
  if (triangle) {
    facing = (int32_t)((uint32_t)firstArea + ((flags & 0x80u) << 23)) > 0;
  } else if (firstArea > 0 || (flags & 0x80u)) {
    facing = true;
  } else {
    facing = world_projection_math::nclip(face.vertices[3], face.vertices[1], face.vertices[2]) < 0;
  }
  if (!facing) {
    ++out.rejected;
    return true;
  }

  const uint32_t depthSum = face.vertices[0].sz + face.vertices[1].sz + face.vertices[2].sz +
                            face.vertices[triangle ? 2 : 3].sz;
  const uint32_t depth = depthSum >> 5;
  if ((int32_t)(depth - farLimit) >= 0) {
    ++out.rejected;
    return true;
  }

  const uint32_t lodClass = (flags & 0x1fu) << 3;
  const int32_t phase = (int32_t)(depth + lodClass - lodBase);
  if (phase < 0) {
    if (phase + 32 <= 0) {
      ++out.rejected;
      return true;
    }
    const uint32_t threshold = (lodBase - lodClass) << 3;
    uint32_t signCommon = 0xffffffffu;
    for (uint32_t i = 0; i < count; ++i) {
      signCommon &= (uint32_t)face.vertices[i].sz - threshold;
    }
    if ((int32_t)signCommon < 0) {
      ++out.rejected;
      return true;
    }
  }

  const uint8_t material = (uint8_t)source.materialWord;
  const uint32_t otBin = depth + (material & 0xf8u) + 0x40u;
  if (otBin >= 0x800u) {
    why = "low_ot_bin";
    return false;
  }
  face.otBin = (uint16_t)otBin;
  face.material.textured = false;
  face.material.semiTransparent = (material & 4u) != 0;
  // The LQ DR_MODE packet carries ABR in draw-mode bits 5..6. Retaining
  // those bits in tpage lets the single queue submitter reproduce the state
  // change without a second material interpretation.
  face.material.tpage = (uint16_t)((material & 3u) << 5);
  if (!world_recipe::appendLinked(out, face, kFaceLimit)) {
    why = "face_capacity";
    return false;
  }
  return true;
}

} // namespace

bool append(const world_chunk_codec::RamView &ram,
            const world_scene_prepare::Prepared &prepared,
            const ProjectionParams &projection,
            int clipRight,
            uint32_t farLimit,
            Recipe &out,
            const char *&why) {
  if (ram.r32(kSkipLow)) {
    return true;
  }

  const FixedAffine cameraMatrix = world_projection_math::decodeMatrix(ram, kCamera);
  const int32_t cameraX = (int32_t)ram.r32(kCamera + 0x28u) >> 4;
  const int32_t cameraY = (int32_t)ram.r32(kCamera + 0x2cu) >> 4;
  const int32_t cameraZ = (int32_t)ram.r32(kCamera + 0x30u) >> 4;
  const uint32_t lodBase = (ram.r32(kEnvironment + 0x24u) >> 7) - 32u;
  uint32_t ordinal = 0;
  std::vector<Vertex> vertices;
  for (const world_scene_prepare::TaggedSector &selected : prepared.low) {
    world_chunk_codec::LowChunk chunk{};
    if (world_chunk_codec::decodeLow(ram, selected.address, chunk) !=
        world_chunk_codec::Status::Ok) {
      why = "low_chunk_decode";
      return false;
    }
    if (!projectVertices(chunk,
                         cameraMatrix,
                         projection,
                         cameraX,
                         cameraY,
                         cameraZ,
                         selected.tags,
                         clipRight,
                         vertices)) {
      continue;
    }
    for (const world_chunk_codec::LowFace &source : chunk.faces) {
      if (!appendFace(
              chunk, source, vertices, selected.tags, farLimit, lodBase, ordinal, out, why)) {
        return false;
      }
    }
  }
  return true;
}

} // namespace spyro::world_lq_recipe
