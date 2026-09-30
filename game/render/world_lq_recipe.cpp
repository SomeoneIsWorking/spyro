#include "world_lq_recipe.h"

#include "world_projection_math.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace spyro::world_lq_recipe {
namespace {

using psxport::native_projection::ProjectionParams;
using spyro::world_recipe::Face;
using spyro::world_recipe::Family;
using spyro::world_recipe::Origin;
using spyro::world_recipe::Recipe;
using spyro::world_recipe::Vertex;

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

psxport::native_projection::ModelVertex projectionInput(const world_chunk_codec::LowChunk &chunk,
                                                        const world_source::Camera &camera,
                                                        uint32_t packed) {
  const int32_t cameraX = camera.position[0] >> 4;
  const int32_t cameraY = camera.position[1] >> 4;
  const int32_t cameraZ = camera.position[2] >> 4;
  const int32_t vx =
      cameraY - (int32_t)(uint16_t)chunk.originWord - (int32_t)((packed >> 10) & 0x7ffu);
  const int32_t vy = cameraZ - (int32_t)chunk.originZ - (int32_t)(packed & 0x3ffu);
  const int32_t vz =
      (int32_t)(packed >> 21) + (int32_t)(uint16_t)(chunk.originWord >> 16) - cameraX;
  return world_projection_math::packProjectionInput(vx, vy, vz);
}

enum class ProjectionResult { Visible, Culled, Refused };

ProjectionResult projectVertices(const world_chunk_codec::LowChunk &previous,
                                 const world_chunk_codec::LowChunk &current,
                                 const world_source::Camera &previousCamera,
                                 const world_source::Camera &currentCamera,
                                 const ProjectionStream &projection,
                                 uint8_t tags,
                                 int clipRight,
                                 std::vector<Vertex> &out) {
  out.clear();
  if (previous.vertices.size() != current.vertices.size()) {
    return ProjectionResult::Refused;
  }
  out.reserve(current.vertices.size());
  uint8_t common = 0xffu;
  for (size_t i = 0; i < current.vertices.size(); ++i) {
    const auto projected =
        projection.project(projectionInput(previous, previousCamera, previous.vertices[i]),
                           projectionInput(current, currentCamera, current.vertices[i]));
    if (!projected) {
      return ProjectionResult::Refused;
    }
    Vertex vertex{};
    vertex.sx = projected->sx;
    vertex.sy = projected->sy;
    vertex.sz = projected->sz;
    vertex.clip = tags & 1u ? clipCode(vertex.sx, vertex.sy, clipRight) : 0u;
    vertex.screenX = projected->px;
    vertex.screenY = projected->py;
    vertex.viewZ = projected->pz;
    common &= vertex.clip;
    out.push_back(vertex);
  }
  return !(tags & 1u) || !(common & 0x0fu) ? ProjectionResult::Visible : ProjectionResult::Culled;
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
  // THE PER-FACE COLOUR IS NOT ALWAYS THE AUTHORED COLOUR. Read out of the image at `0x8002651C`
  // (`andi $a3,$t6,4` on the face's material word) and at `0x80026544`/`0x8002654C`
  // (`ori $a1,$a1,0x600` / `ori $a1,$zero,0xE100`):
  //
  //   material bit 2 clear -> the four per-face colour words go into the primitive as its GOUROD
  //                          colours, one per vertex, indexed by the material word's four 6-bit
  //                          fields. That is what the loop below has always done.
  //   material bit 2 SET   -> the guest writes a CONSTANT colour word whose top byte is 0xE100 OR'd
  //                          in, and discards the four authored colours. 0xE1's semi-transparency
  //                          code (bits 27..24) is 1, which in GP0 is B/2 + F/2 -- a 50/50 blend --
  //                          and bit 24 is the polygon bit, so this is a semi-transparent
  //                          untextured polygon: Spyro's pool water, a tinted sheet over the pool
  //                          floor rather than a coloured surface of its own. The shift carries
  //                          material bit 2 into colour bit 7 as well, which is why the mask below
  //                          is 7 and not 3.
  //
  //   THE BLEND MODE THIS PORT ACTUALLY USES IS A DIFFERENT FIELD, and this comment previously
  //   claimed otherwise. gpu_native_raster.cpp blends with s_tp_blend, taken from the texpage, and
  //   the tpage below is a PORT-INVENTED encoding of material bits 0..1 -- not the guest's DR_MODE
  //   and not the command word above. The two agree only when those bits are clear, which is the
  //   50/50 this constant describes. See docs/issues/0140 for the measured mapping and for what
  //   would settle it against the authored data.
  //
  // Reading the authored colour for those faces is what made the water render as per-block colour
  // noise: the pool is a grid of such faces, each carrying its own colour-array entry, and the
  // console blends a constant 50% tint over the floor instead. Every other surface takes the
  // gouraud arm, which is why the actors, hedges, towers and buildings were all clean and only the
  // water was wrong.
  //
  // The value is reconstructed from those three instructions rather than pasted as a literal, so
  // the derivation stays checkable against the bytes.
  const bool translucent = (source.materialWord & 4u) != 0u;
  const uint32_t translucentColor = 0xe1000600u | ((source.materialWord & 7u) << 5);
  uint8_t clips = 0xffu;
  for (uint32_t i = 0; i < count; ++i) {
    if (vertexIndices[i] >= vertices.size() || colorIndices[i] >= chunk.colors.size()) {
      why = "low_face_index";
      return false;
    }
    face.vertices[i] = vertices[vertexIndices[i]];
    // The bound on the colour index is still checked for a translucent face: the guest computes
    // those four loads before discarding them, so a face whose indices are out of range is one the
    // guest would have read past the end of, and refusing keeps that decision in one place.
    face.vertices[i].rgb = translucent ? translucentColor : chunk.colors[colorIndices[i]];
    clips &= face.vertices[i].clip;
  }
  if ((tags & 1u) && (clips & (triangle ? 0x1fu : 0x0fu))) {
    ++out.rejected;
    return true;
  }
  // Past the reject, so this face IS appended and drawn. Its span is the answer to "did the
  // widening reach this producer", measured on what was drawn rather than on what changed colour.
  out.span.addVertices(
      face.vertices.begin(), face.vertices.begin() + count, [](const auto &vertex) {
        return vertex.sx;
      });

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
  face.material.semiTransparent = translucent;
  // The single queue submitter has no DR_MODE field, so the blend mode rides in the texpage's own
  // ABR bits ((tpage >> 5) & 3), which is what gpu_native_raster.cpp reads. This is a PORT-INVENTED
  // encoding of material bits 0..1 -- NOT the guest's draw mode and NOT the 0xE1 command word
  // above, which carry their own semi-transparency code. They coincide when these bits are clear;
  // they do not coincide in general. The same two material bits are also the low bits of the water
  // colour above, which is why this stays one reading of the same pair rather than a second one.
  // See docs/issues/0140.
  face.material.tpage = (uint16_t)((material & 3u) << 5);
  if (!world_recipe::appendLinked(out, face, kFaceLimit)) {
    why = "face_capacity";
    return false;
  }
  return true;
}

} // namespace

bool append(const world_source::Source &previous,
            const world_source::Source &input,
            const world_scene_prepare::Prepared &prepared,
            const ProjectionStream &projection,
            int clipRight,
            uint32_t farLimit,
            Recipe &out,
            const char *&why) {
  if (input.selection.skipLow) {
    return true;
  }

  const uint32_t lodBase = (input.selection.lodDistance >> 7) - 32u;
  uint32_t ordinal = 0;
  std::vector<Vertex> vertices;
  for (const world_scene_prepare::TaggedSector &selected : prepared.low) {
    const auto &sector = input.sectors[selected.index];
    const auto &previousSector = previous.sectors[selected.index];
    if (!sector || !previousSector || sector->lowStatus != world_chunk_codec::Status::Ok ||
        previousSector->lowStatus != world_chunk_codec::Status::Ok) {
      why = "low_chunk_decode";
      return false;
    }
    const auto &chunk = sector->low;
    const auto result = projectVertices(previousSector->low,
                                        chunk,
                                        previous.selection.camera,
                                        input.selection.camera,
                                        projection,
                                        selected.tags,
                                        clipRight,
                                        vertices);
    if (result == ProjectionResult::Refused) {
      why = "low_projection_sample";
      return false;
    }
    if (result == ProjectionResult::Culled) {
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

bool append(const world_source::Source &input,
            const world_scene_prepare::Prepared &prepared,
            const ProjectionParams &projection,
            int clipRight,
            uint32_t farLimit,
            Recipe &out,
            const char *&why) {
  const ProjectionStream stream(input.selection.camera.projectionMatrix, projection);
  return append(input, input, prepared, stream, clipRight, farLimit, out, why);
}

} // namespace spyro::world_lq_recipe
