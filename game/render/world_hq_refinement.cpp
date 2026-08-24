#include "world_hq_refinement.h"

#include "world_material_codec.h"
#include "world_projection_math.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace spyro::world_hq_refinement {

using psxport::native_projection::FixedAffine;
using psxport::native_projection::NativeProjectedVertex;
using psxport::native_projection::ProjectionParams;
using spyro::world_chunk_codec::RamView;
using spyro::world_recipe::Face;
using spyro::world_recipe::Family;
using spyro::world_recipe::Origin;
using spyro::world_recipe::Recipe;
using spyro::world_recipe::Vertex;
using HighParent = Parent;
using HighWork = Work;
using Vec3s = Position;

namespace {
constexpr uint32_t kEnvironment = 0x800785a8u;
constexpr uint32_t kCamera = 0x80076dd0u;
constexpr size_t kFaceLimit = 16384;

uint8_t highClipCode(int16_t sx, int16_t sy, uint16_t depth, uint8_t tags, int clipRight) {
  // Tags 2/3 only use the precision encoder below SZ 0x600. At
  // 0x80026A00, larger vertices branch to 0x80026AF8 and use the same coarse
  // packed-horizontal encoder as tag 1.
  if ((tags & 2u) && depth < 0x600u) {
    return world_recipe::clipCode(sx, sy, clipRight);
  }
  if (tags == 0u) {
    return 0u;
  }

  // RenderWorldChunks' tag-1 arm at 0x80026930 and far tag-2/3 arm at
  // 0x80026AF8 pack both horizontal sides into bits 2 and 3 together. Unlike
  // the near precision arm, x == 0 is inside. The original 512-wide test is
  // `(sxy & 0xfe00) != 0`; spelling out its signed interval keeps the same
  // boundary when clipRight is widened.
  const uint32_t packed = (uint16_t)sx | ((uint32_t)(uint16_t)sy << 16);
  uint8_t clip = 0;
  if ((int32_t)(packed - 0x00010000u) <= 0) {
    clip |= 1u;
  }
  if ((int32_t)(packed - 0x01000000u) >= 0) {
    clip |= 2u;
  }
  if (sx < 0 || sx >= clipRight) {
    clip |= 0x0cu;
  }
  return clip;
}

bool mapped(const RamView &ram, uint32_t address, uint32_t size) {
  return ram.contains(address, size);
}

uint32_t averageRgb(uint32_t left, uint32_t right) {
  return ((left & 0xfffefeffu) + (right & 0xfffefeffu)) >> 1;
}

} // namespace

HighVertex projectVertex(const FixedAffine &cameraMatrix,
                         const ProjectionParams &projection,
                         Vec3s position,
                         uint8_t tags,
                         int clipRight) {
  const auto input = world_projection_math::packProjectionInput(position.x, position.y, position.z);
  NativeProjectedVertex projected =
      psxport::native_projection::project(cameraMatrix, projection, input);
  const uint16_t originalDepth = projected.sz;
  const float originalViewDepth = projected.pz;
  if ((tags & 2u) && projected.sz < 0x100u && projected.raw_view[0] > -256.0f &&
      projected.raw_view[0] < 256.0f && projected.raw_view[1] > -256.0f &&
      projected.raw_view[1] < 256.0f) {
    const Vec3s scaled{(int16_t)((uint16_t)position.x << 4),
                       (int16_t)((uint16_t)position.y << 4),
                       (int16_t)((uint16_t)position.z << 4)};
    const auto scaledInput =
        world_projection_math::packProjectionInput(scaled.x, scaled.y, scaled.z);
    projected = psxport::native_projection::project(cameraMatrix, projection, scaledInput);
    projected.sz = originalDepth;
    projected.pz = originalViewDepth;
  }

  HighVertex out{};
  out.position = {(int16_t)position.x, (int16_t)position.y, (int16_t)position.z};
  out.projected.sx = projected.sx;
  out.projected.sy = projected.sy;
  out.projected.sz = projected.sz;
  out.projected.clip = highClipCode(projected.sx, projected.sy, projected.sz, tags, clipRight);
  out.projected.screenX = projected.px;
  out.projected.screenY = projected.py;
  out.projected.viewZ = projected.pz;
  // Only the tag-2/3 precision arm at 0x80026A8C folds RTPS' checksum flag
  // into bit 4 of the cached clip word. RenderWorldChunks later stores that
  // bit in the queued parent pointer, where it gates child NCLIP. The coarse
  // arm reached at the original SZ 0x600 boundary never records FLAG.
  out.requiresFacingCheck =
      (tags & 2u) != 0 && originalDepth < 0x600u && (projected.flags & 0x80000000u) != 0;
  return out;
}

bool facing(const std::array<HighVertex, 4> &vertices, uint32_t count, uint32_t flags) {
  if (flags & 4u) {
    return true;
  }
  int32_t first = world_projection_math::nclip(
      vertices[0].projected, vertices[1].projected, vertices[count == 4 ? 3 : 2].projected);
  if (flags & 2u) {
    first = -first;
  }
  if (count == 3) {
    return first >= 0;
  }
  if (first >= 0) {
    return true;
  }
  const int32_t second = world_projection_math::nclip(
      vertices[2].projected, vertices[1].projected, vertices[3].projected);
  return flags & 2u ? second >= 0 : second <= 0;
}

uint32_t depthSum(const std::array<HighVertex, 4> &vertices, uint32_t count) {
  return vertices[0].projected.sz + vertices[1].projected.sz + vertices[2].projected.sz +
         vertices[count == 4 ? 3 : 2].projected.sz;
}

void applyTile(Face &face, const world_material_codec::DecodedTile &tile) {
  face.material.clut = tile.clut;
  face.material.tpage = tile.tpage;
  for (uint32_t i = 0; i < face.vertexCount; ++i) {
    face.vertices[i].u = tile.u[i];
    face.vertices[i].v = tile.v[i];
  }
}

std::array<uint32_t, 25> nearQuadColorLattice(const std::array<uint32_t, 4> &corners) {
  std::array<uint32_t, 25> colors{};
  colors[0] = corners[0];
  colors[4] = corners[1];
  colors[20] = corners[3];
  colors[24] = corners[2];

  // The graph at 0x80028A98..0x80028BCC first averages the authored
  // top-right/bottom-left diagonal into one center color. Edge quarters use
  // their edge midpoint, while the four inner corners average directly with
  // that center. It is not the geometry lattice's recursive midpoint graph;
  // using that graph loses one in channels that truncate at different steps.
  colors[12] = averageRgb(colors[4], colors[20]);

  colors[2] = averageRgb(colors[0], colors[4]);
  colors[1] = averageRgb(colors[0], colors[2]);
  colors[3] = averageRgb(colors[4], colors[2]);
  colors[7] = averageRgb(colors[12], colors[2]);

  colors[10] = averageRgb(colors[0], colors[20]);
  colors[5] = averageRgb(colors[0], colors[10]);
  colors[15] = averageRgb(colors[20], colors[10]);
  colors[11] = averageRgb(colors[12], colors[10]);

  colors[14] = averageRgb(colors[4], colors[24]);
  colors[9] = averageRgb(colors[4], colors[14]);
  colors[19] = averageRgb(colors[24], colors[14]);
  colors[13] = averageRgb(colors[12], colors[14]);

  colors[22] = averageRgb(colors[20], colors[24]);
  colors[21] = averageRgb(colors[20], colors[22]);
  colors[23] = averageRgb(colors[24], colors[22]);
  colors[17] = averageRgb(colors[12], colors[22]);

  colors[6] = averageRgb(colors[0], colors[12]);
  colors[8] = averageRgb(colors[4], colors[12]);
  colors[16] = averageRgb(colors[20], colors[12]);
  colors[18] = averageRgb(colors[24], colors[12]);
  return colors;
}

namespace {

bool appendFace(Recipe &out, Face face, const char *&why) {
  if (face.otBin >= 0x800u) {
    why = "high_ot_bin";
    return false;
  }
  if (!world_recipe::appendLinked(out, face, kFaceLimit)) {
    why = "face_capacity";
    return false;
  }
  return true;
}

HighVertex midpoint(const HighVertex &left, const HighVertex &right) {
  HighVertex out{};
  out.position.x = (int16_t)(((int32_t)left.position.x + (int32_t)right.position.x) >> 1);
  out.position.y = (int16_t)(((int32_t)left.position.y + (int32_t)right.position.y) >> 1);
  out.position.z = (int16_t)(((int32_t)left.position.z + (int32_t)right.position.z) >> 1);
  out.projected.rgb = averageRgb(left.projected.rgb, right.projected.rgb);
  return out;
}

void projectLattice(const FixedAffine &cameraMatrix,
                    const ProjectionParams &projection,
                    uint8_t tags,
                    int clipRight,
                    std::span<HighVertex> vertices,
                    bool nonpositiveOutside = false) {
  for (HighVertex &vertex : vertices) {
    const uint32_t rgb = vertex.projected.rgb;
    vertex = projectVertex(cameraMatrix, projection, vertex.position, tags, clipRight);
    vertex.projected.rgb = rgb;
    // The refinement projectors at 0x80027AE8 and 0x80028A70 always write
    // directional top/bottom/left/right outcodes. The depth-dependent coarse
    // encoder belongs only to the initial HQ vertex cache.
    vertex.projected.clip =
        world_recipe::clipCode(vertex.projected.sx, vertex.projected.sy, clipRight);
    if (nonpositiveOutside && vertex.projected.viewZ <= 0.0f) {
      vertex.projected.clip = 0x0fu;
    }
  }
}

int32_t sign12(uint32_t value) {
  return (int32_t)(value << 20) >> 20;
}

std::array<int32_t, 4> edgeReferences(const HighParent &parent) {
  if (parent.count == 4) {
    return {sign12(parent.materialWord >> 20),
            sign12(parent.materialWord >> 8),
            sign12(parent.flags >> 20),
            sign12(parent.flags >> 8)};
  }
  return {
      sign12(parent.materialWord >> 20), sign12(parent.flags >> 8), sign12(parent.flags >> 20), 0};
}

bool edgeStatus(const HighWork &work,
                const HighParent &parent,
                int32_t reference,
                uint8_t &status) {
  const int64_t address = (int64_t)parent.statusAddress + reference;
  if (address < 0 || address >= (int64_t)work.status.size()) {
    return false;
  }
  status = work.status[(size_t)address];
  return true;
}

world_material_codec::DecodedTile packedTile(std::array<uint32_t, 4> words, uint32_t count) {
  world_material_codec::DecodedTile out{};
  for (uint32_t i = 0; i < count; ++i) {
    out.u[i] = (uint8_t)words[i];
    out.v[i] = (uint8_t)(words[i] >> 8);
  }
  out.clut = (uint16_t)(words[0] >> 16);
  out.tpage = (uint16_t)(words[1] >> 16);
  return out;
}

bool makeChild(const HighParent &parent,
               std::span<const HighVertex> lattice,
               std::span<const uint8_t> indices,
               const world_material_codec::DecodedTile &tile,
               Origin origin,
               Face &face) {
  face = {};
  face.family = indices.size() == 4 ? Family::GT4 : Family::GT3;
  face.origin = origin;
  face.vertexCount = (uint8_t)indices.size();
  face.sector = parent.sector;
  face.source = parent.source;
  face.sourceOrdinal = parent.ordinal;
  uint8_t common = 0xffu;
  std::array<HighVertex, 4> selected{};
  for (uint32_t i = 0; i < indices.size(); ++i) {
    if (indices[i] >= lattice.size()) {
      return false;
    }
    selected[i] = lattice[indices[i]];
    face.vertices[i] = selected[i].projected;
    common &= face.vertices[i].clip;
  }
  if (common & 0x0fu) {
    face.vertexCount = 0;
    return true;
  }
  if (origin != Origin::EdgeFiller && parent.recheckFacing &&
      !facing(selected, (uint32_t)indices.size(), parent.flags)) {
    face.vertexCount = 0;
    return true;
  }
  const uint32_t bin =
      (depthSum(selected, (uint32_t)indices.size()) >> 7) + ((parent.flags & 0x38u) >> 1);
  if (bin >= 0x800u) {
    return false;
  }
  face.otBin = (uint16_t)bin;
  const auto key =
      world_material_codec::classify((int8_t)parent.materialWord, (parent.materialWord >> 8) & 3u);
  face.material = world_material_codec::painterMaterial(key, tile);
  applyTile(face, tile);
  return true;
}

std::array<uint8_t, 3> edgeIndices(const RamView &ram, uint32_t descriptor) {
  const uint32_t w0 = ram.r32(descriptor);
  const uint32_t w1 = ram.r32(descriptor + 4u);
  return {(uint8_t)((uint16_t)(w0 >> 16) / 16u),
          (uint8_t)((uint16_t)w0 / 16u),
          (uint8_t)((uint16_t)(w1 >> 16) / 16u)};
}

world_material_codec::DecodedTile
edgeTile(const RamView &ram, uint32_t descriptor, uint32_t material) {
  const uint32_t w1 = ram.r32(descriptor + 4u);
  const uint32_t w2 = ram.r32(descriptor + 8u);
  return packedTile({ram.r32(material) + (uint16_t)w1,
                     ram.r32(material + 4u) + (uint32_t)(int32_t)(int16_t)(w2 >> 16),
                     ram.r32(material) + (uint32_t)(int32_t)(int16_t)w2,
                     0},
                    3);
}

bool appendTransition(const RamView &ram,
                      const HighParent &parent,
                      std::span<const HighVertex> lattice,
                      uint32_t descriptor,
                      uint32_t material,
                      Recipe &out,
                      const char *&why) {
  if (!mapped(ram, descriptor, 12u) || !mapped(ram, material, 8u)) {
    why = "transition_table_bounds";
    return false;
  }
  Face face{};
  if (!makeChild(parent,
                 lattice,
                 edgeIndices(ram, descriptor),
                 edgeTile(ram, descriptor, material),
                 Origin::EdgeFiller,
                 face)) {
    why = "transition_child";
    return false;
  }
  return !face.vertexCount || appendFace(out, face, why);
}

bool refinedQuadTile(const RamView &ram,
                     uint32_t pair,
                     uint8_t extent,
                     world_material_codec::DecodedTile &out) {
  uint32_t first = ram.r32(pair);
  uint32_t second = ram.r32(pair + 4u);
  uint32_t third = first + ((uint32_t)extent << 8);
  uint32_t fourth = third + extent;
  const uint32_t attribute = second >> 25;
  if (attribute) {
    const uint32_t adjustment = 0x8006d058u + attribute;
    if (!mapped(ram, adjustment, 8u)) {
      return false;
    }
    const uint32_t a = ram.r32(adjustment);
    const uint32_t b = ram.r32(adjustment + 4u);
    third = first + (uint16_t)b;
    fourth = first + (uint16_t)(b >> 16);
    first += (uint16_t)a;
    second += (uint32_t)(int32_t)(int16_t)(a >> 16);
  }
  out = packedTile({first, second, third, fourth}, 4);
  return true;
}

world_material_codec::DecodedTile
triangleTile(const RamView &ram, uint32_t descriptor, uint8_t orientation, uint32_t pair) {
  const uint32_t first = ram.r32(pair);
  const uint32_t second = ram.r32(pair + 4u);
  const uint32_t rotation = ((uint32_t)orientation + descriptor) & 3u;
  const uint32_t attribute = (second >> 25) & 0x78u;
  const uint32_t deltaAddress = 0x8006d3c8u + (rotation << 7) + attribute;
  const uint32_t delta = ram.r32(deltaAddress);
  const int16_t delta2 = ram.r16(deltaAddress + 4u);
  return packedTile({first + (uint16_t)delta,
                     second + (uint32_t)(int32_t)(int16_t)(delta >> 16),
                     first + (uint32_t)(int32_t)delta2,
                     0},
                    3);
}

std::array<uint8_t, 3> topology(uint32_t descriptor) {
  return {(uint8_t)(((descriptor >> 20) & 0xff0u) / 16u),
          (uint8_t)(((descriptor >> 12) & 0xff0u) / 16u),
          (uint8_t)(((descriptor >> 4) & 0xff0u) / 16u)};
}

void correctCenter(std::array<HighVertex, 9> &vertices) {
  Vertex &center = vertices[4].projected;
  if (center.clip) {
    return;
  }
  const int32_t d = (center.sz & 0x0fffu) - 0x700;
  if (d <= 0) {
    return;
  }
  const int16_t self = (int16_t)(0x100 - d);
  const int16_t side = (int16_t)((uint32_t)d >> 1);
  const int16_t diagonalX =
      (int16_t)((int32_t)vertices[2].projected.sx + (int32_t)vertices[6].projected.sx);
  const int16_t diagonalY =
      (int16_t)((int32_t)vertices[2].projected.sy + (int32_t)vertices[6].projected.sy);
  center.sx = (int16_t)(((int32_t)self * center.sx + (int32_t)side * diagonalX) >> 8);
  center.sy = (int16_t)(((int32_t)self * center.sy + (int32_t)side * diagonalY) >> 8);
}

bool appendMedium(const RamView &ram,
                  const ProjectionParams &projection,
                  int clipRight,
                  const HighWork &work,
                  Recipe &out,
                  const char *&why) {
  static constexpr std::array<uint8_t, 4> kQuadTopLeft = {0, 1, 3, 4};
  const FixedAffine cameraMatrix = world_projection_math::decodeMatrix(ram, kCamera);
  const uint32_t textureCount = ram.r32(kEnvironment + 0x20u);
  const uint32_t hqTextures = ram.r32(kEnvironment + 0x1cu);
  for (const HighParent &parent : work.medium) {
    const auto key = world_material_codec::classify((int8_t)parent.materialWord,
                                                    (parent.materialWord >> 8) & 3u);
    if (!key.textured || key.index >= textureCount) {
      why = "medium_material";
      return false;
    }
    const uint32_t material = hqTextures + (uint32_t)key.index * 0xa8u;
    if (!mapped(ram, material, 0xa8u)) {
      why = "hq_texture_bounds";
      return false;
    }
    const auto references = edgeReferences(parent);
    if (parent.count == 4) {
      std::array<HighVertex, 9> lattice{};
      lattice[0] = parent.vertices[0];
      lattice[2] = parent.vertices[1];
      // Quad refinement consumes the same 0,1,3,2 corner order as the guest
      // GT4 packet. The source face word itself remains 0,1,2,3.
      lattice[6] = parent.vertices[3];
      lattice[8] = parent.vertices[2];
      lattice[1] = midpoint(lattice[0], lattice[2]);
      lattice[3] = midpoint(lattice[0], lattice[6]);
      lattice[5] = midpoint(lattice[2], lattice[8]);
      lattice[7] = midpoint(lattice[6], lattice[8]);
      lattice[4] = midpoint(lattice[3], lattice[5]);
      // The position center is the mean of the side midpoints, but the guest
      // builds the packed RGB center directly from this diagonal. Keeping the
      // averaging order matters because each packed channel truncates.
      lattice[4].projected.rgb = averageRgb(lattice[2].projected.rgb, lattice[6].projected.rgb);
      projectLattice(cameraMatrix, projection, parent.tags, clipRight, lattice);
      correctCenter(lattice);
      for (uint32_t edge = 0; edge < 4; ++edge) {
        uint8_t status = 0;
        if (!edgeStatus(work, parent, references[edge], status)) {
          why = "medium_edge_status";
          return false;
        }
        if ((status & 1u) &&
            !appendTransition(ram, parent, lattice, 0x8006cf98u + edge * 12u, material, out, why)) {
          return false;
        }
      }
      for (uint32_t child = 0; child < kQuadTopLeft.size(); ++child) {
        const uint8_t p = kQuadTopLeft[child];
        const std::array<uint8_t, 4> indices = {
            p, (uint8_t)(p + 1), (uint8_t)(p + 3), (uint8_t)(p + 4)};
        const uint32_t textureSource = material + 8u + child * 8u;
        world_material_codec::DecodedTile tile{};
        if (!refinedQuadTile(ram, textureSource, 0x1fu, tile)) {
          why = "medium_quad_texture";
          return false;
        }
        Face face{};
        if (!makeChild(parent, lattice, indices, tile, Origin::Medium, face)) {
          why = "medium_quad_child";
          return false;
        }
        face.textureSource = textureSource;
        if (face.vertexCount && !appendFace(out, face, why)) {
          return false;
        }
      }
      continue;
    }

    std::array<HighVertex, 6> lattice{};
    lattice[0] = parent.vertices[0];
    lattice[2] = parent.vertices[1];
    lattice[5] = parent.vertices[2];
    lattice[1] = midpoint(lattice[0], lattice[2]);
    lattice[3] = midpoint(lattice[0], lattice[5]);
    lattice[4] = midpoint(lattice[2], lattice[5]);
    projectLattice(cameraMatrix, projection, parent.tags, clipRight, lattice);
    const uint8_t orientation = (parent.materialWord >> 8) & 3u;
    for (uint32_t edge = 0; edge < 3; ++edge) {
      uint8_t status = 0;
      if (!edgeStatus(work, parent, references[edge], status)) {
        why = "medium_edge_status";
        return false;
      }
      if ((status & 1u) && !appendTransition(ram,
                                             parent,
                                             lattice,
                                             0x8006d138u + orientation * 0x24u + edge * 12u,
                                             material,
                                             out,
                                             why)) {
        return false;
      }
    }
    for (uint32_t child = 0; child < 4; ++child) {
      const uint32_t descriptor = ram.r32(0x8006d0e8u + child * 4u);
      const uint32_t selector = (descriptor & 0x0cu) + orientation;
      const int8_t pairOffset = (int8_t)ram.r8(0x8006d378u + selector);
      const uint32_t pair = material + 8u + (int32_t)pairOffset;
      if (!mapped(ram, pair, 8u)) {
        why = "medium_triangle_texture";
        return false;
      }
      Face face{};
      if (!makeChild(parent,
                     lattice,
                     topology(descriptor),
                     triangleTile(ram, descriptor, orientation, pair),
                     Origin::Medium,
                     face)) {
        why = "medium_triangle_child";
        return false;
      }
      if (face.vertexCount && !appendFace(out, face, why)) {
        return false;
      }
    }
  }
  return true;
}

void buildNearQuadLattice(const HighParent &parent, std::array<HighVertex, 25> &lattice) {
  lattice[0] = parent.vertices[0];
  lattice[4] = parent.vertices[1];
  lattice[20] = parent.vertices[3];
  lattice[24] = parent.vertices[2];

  lattice[2] = midpoint(lattice[0], lattice[4]);
  lattice[1] = midpoint(lattice[0], lattice[2]);
  lattice[3] = midpoint(lattice[4], lattice[2]);
  lattice[22] = midpoint(lattice[20], lattice[24]);
  lattice[21] = midpoint(lattice[20], lattice[22]);
  lattice[23] = midpoint(lattice[24], lattice[22]);

  lattice[10] = midpoint(lattice[0], lattice[20]);
  lattice[5] = midpoint(lattice[0], lattice[10]);
  lattice[15] = midpoint(lattice[20], lattice[10]);
  lattice[14] = midpoint(lattice[4], lattice[24]);
  lattice[9] = midpoint(lattice[4], lattice[14]);
  lattice[19] = midpoint(lattice[24], lattice[14]);

  lattice[12] = midpoint(lattice[10], lattice[14]);
  lattice[11] = midpoint(lattice[10], lattice[12]);
  lattice[13] = midpoint(lattice[14], lattice[12]);
  lattice[7] = midpoint(lattice[5], lattice[9]);
  lattice[6] = midpoint(lattice[5], lattice[7]);
  lattice[8] = midpoint(lattice[9], lattice[7]);
  lattice[17] = midpoint(lattice[15], lattice[19]);
  lattice[16] = midpoint(lattice[15], lattice[17]);
  lattice[18] = midpoint(lattice[19], lattice[17]);

  const auto colors = nearQuadColorLattice({parent.vertices[0].projected.rgb,
                                            parent.vertices[1].projected.rgb,
                                            parent.vertices[2].projected.rgb,
                                            parent.vertices[3].projected.rgb});
  for (uint32_t i = 0; i < lattice.size(); ++i) {
    lattice[i].projected.rgb = colors[i];
  }
}

void correctNearQuadInterior(std::array<HighVertex, 25> &vertices) {
  const std::array<uint8_t, 4> corners = {0, 4, 20, 24};
  int32_t minimum = 0x1000;
  for (uint8_t corner : corners) {
    const int32_t distance = (vertices[corner].projected.sz & 0x0fffu) - 0x40;
    if (distance <= 0) {
      return;
    }
    minimum = minimum < distance ? minimum : distance;
  }
  const int16_t selfWeight = (int16_t)(0x100 - minimum);
  const int16_t sideWeight = (int16_t)((uint32_t)minimum >> 1);
  static constexpr std::array<uint8_t, 4> kInterior = {6, 8, 16, 18};
  for (uint8_t index : kInterior) {
    Vertex &vertex = vertices[index].projected;
    if (vertex.clip) {
      continue;
    }
    const int16_t sideX = (int16_t)((int32_t)vertices[index - 4].projected.sx +
                                    (int32_t)vertices[index + 4].projected.sx);
    const int16_t sideY = (int16_t)((int32_t)vertices[index - 4].projected.sy +
                                    (int32_t)vertices[index + 4].projected.sy);
    vertex.sx = (int16_t)(((int32_t)selfWeight * vertex.sx + (int32_t)sideWeight * sideX) >> 8);
    vertex.sy = (int16_t)(((int32_t)selfWeight * vertex.sy + (int32_t)sideWeight * sideY) >> 8);
  }
}

void buildNearTriangleLattice(const HighParent &parent, std::array<HighVertex, 15> &lattice) {
  lattice[0] = parent.vertices[0];
  lattice[4] = parent.vertices[1];
  lattice[14] = parent.vertices[2];
  lattice[2] = midpoint(lattice[0], lattice[4]);
  lattice[1] = midpoint(lattice[0], lattice[2]);
  lattice[3] = midpoint(lattice[4], lattice[2]);
  lattice[9] = midpoint(lattice[0], lattice[14]);
  lattice[5] = midpoint(lattice[0], lattice[9]);
  lattice[12] = midpoint(lattice[14], lattice[9]);
  lattice[11] = midpoint(lattice[4], lattice[14]);
  lattice[8] = midpoint(lattice[4], lattice[11]);
  lattice[13] = midpoint(lattice[14], lattice[11]);
  lattice[6] = midpoint(lattice[0], lattice[11]);
  lattice[7] = midpoint(lattice[4], lattice[9]);
  lattice[10] = midpoint(lattice[14], lattice[2]);
}

bool oversizedTriangle(const Face &face) {
  static constexpr std::array<std::array<uint8_t, 2>, 3> kPairs = {
      std::array<uint8_t, 2>{0, 1}, {0, 2}, {1, 2}};
  for (const auto &pair : kPairs) {
    const Vertex &a = face.vertices[pair[0]], &b = face.vertices[pair[1]];
    const int32_t dy = (int32_t)a.sy - (int32_t)b.sy;
    if (dy >= 512 || dy <= -512) {
      return true;
    }
  }
  for (const auto &pair : kPairs) {
    const Vertex &a = face.vertices[pair[0]], &b = face.vertices[pair[1]];
    const int32_t dx = (int32_t)a.sx - (int32_t)b.sx;
    if (dx >= 1024 || dx <= -1024) {
      return true;
    }
  }
  return false;
}

bool oversizedQuad(const Face &face) {
  static constexpr std::array<std::array<uint8_t, 2>, 5> kPairs = {
      std::array<uint8_t, 2>{0, 1}, {0, 2}, {1, 2}, {1, 3}, {2, 3}};
  for (const auto &pair : kPairs) {
    const Vertex &a = face.vertices[pair[0]], &b = face.vertices[pair[1]];
    const int32_t dy = (int32_t)a.sy - (int32_t)b.sy;
    if (dy >= 512 || dy <= -512) {
      return true;
    }
  }
  for (const auto &pair : kPairs) {
    const Vertex &a = face.vertices[pair[0]], &b = face.vertices[pair[1]];
    const int32_t dx = (int32_t)a.sx - (int32_t)b.sx;
    if (dx >= 1024 || dx <= -1024) {
      return true;
    }
  }
  return false;
}

bool appendNearFace(Recipe &out, Face face, bool oversized, int clipRight, const char *&why) {
  if (!oversized) {
    return appendFace(out, face, why);
  }
  face.paintGroup = world_recipe::reservePaintGroup(out);
  if (!world_recipe::adaptiveSubdivide(face, out.faces, kFaceLimit, clipRight)) {
    why = "adaptive_capacity";
    return false;
  }
  return true;
}

bool appendNearTransitions(const RamView &ram,
                           const HighParent &parent,
                           std::span<const HighVertex> lattice,
                           const HighWork &work,
                           uint32_t table,
                           uint32_t material,
                           Recipe &out,
                           const char *&why) {
  const auto references = edgeReferences(parent);
  for (uint32_t edge = 0; edge < parent.count; ++edge) {
    uint8_t status = 0;
    if (!edgeStatus(work, parent, references[edge], status)) {
      why = "near_edge_status";
      return false;
    }
    status &= 3u;
    if (!status) {
      continue;
    }
    const uint32_t count = 2u + (status & 1u);
    for (uint32_t child = 0; child < count; ++child) {
      if (!appendTransition(
              ram, parent, lattice, table + edge * 0x24u + child * 12u, material, out, why)) {
        return false;
      }
    }
  }
  return true;
}

bool appendNearQuads(const RamView &ram,
                     const ProjectionParams &projection,
                     int clipRight,
                     const HighWork &work,
                     Recipe &out,
                     const char *&why) {
  static constexpr std::array<uint8_t, 16> kTopLeft = {
      0, 1, 2, 3, 5, 6, 7, 8, 10, 11, 12, 13, 15, 16, 17, 18};
  const FixedAffine cameraMatrix = world_projection_math::decodeMatrix(ram, kCamera);
  const uint32_t textureCount = ram.r32(kEnvironment + 0x20u);
  const uint32_t hqTextures = ram.r32(kEnvironment + 0x1cu);
  for (const HighParent &parent : work.near) {
    if (parent.count != 4) {
      continue;
    }
    const auto key = world_material_codec::classify((int8_t)parent.materialWord,
                                                    (parent.materialWord >> 8) & 3u);
    if (!key.textured || key.index >= textureCount) {
      why = "near_material";
      return false;
    }
    const uint32_t material = hqTextures + (uint32_t)key.index * 0xa8u;
    if (!mapped(ram, material, 0xa8u)) {
      why = "hq_texture_bounds";
      return false;
    }
    std::array<HighVertex, 25> lattice{};
    buildNearQuadLattice(parent, lattice);
    projectLattice(cameraMatrix, projection, parent.tags, clipRight, lattice, true);
    correctNearQuadInterior(lattice);
    if (!appendNearTransitions(ram, parent, lattice, work, 0x8006cfc8u, material, out, why)) {
      return false;
    }
    for (uint32_t child = 0; child < kTopLeft.size(); ++child) {
      const uint8_t p = kTopLeft[child];
      const std::array<uint8_t, 4> indices = {
          p, (uint8_t)(p + 1), (uint8_t)(p + 5), (uint8_t)(p + 6)};
      const uint32_t textureSource = material + 0x28u + child * 8u;
      world_material_codec::DecodedTile tile{};
      if (!refinedQuadTile(ram, textureSource, 0x0fu, tile)) {
        why = "near_quad_texture";
        return false;
      }
      Face face{};
      if (!makeChild(parent, lattice, indices, tile, Origin::Near, face)) {
        why = "near_quad_child";
        return false;
      }
      face.textureSource = textureSource;
      if (!face.vertexCount) {
        continue;
      }
      const bool oversized = oversizedQuad(face);
      if (!appendNearFace(out, face, oversized, clipRight, why)) {
        return false;
      }
    }
  }
  return true;
}

bool appendNearTriangles(const RamView &ram,
                         const ProjectionParams &projection,
                         int clipRight,
                         const HighWork &work,
                         Recipe &out,
                         const char *&why) {
  const FixedAffine cameraMatrix = world_projection_math::decodeMatrix(ram, kCamera);
  const uint32_t textureCount = ram.r32(kEnvironment + 0x20u);
  const uint32_t hqTextures = ram.r32(kEnvironment + 0x1cu);
  for (const HighParent &parent : work.near) {
    if (parent.count != 3) {
      continue;
    }
    const auto key = world_material_codec::classify((int8_t)parent.materialWord,
                                                    (parent.materialWord >> 8) & 3u);
    if (!key.textured || key.index >= textureCount) {
      why = "near_material";
      return false;
    }
    const uint32_t material = hqTextures + (uint32_t)key.index * 0xa8u;
    if (!mapped(ram, material, 0xa8u)) {
      why = "hq_texture_bounds";
      return false;
    }
    std::array<HighVertex, 15> lattice{};
    buildNearTriangleLattice(parent, lattice);
    projectLattice(cameraMatrix, projection, parent.tags, clipRight, lattice, true);
    const uint8_t orientation = (parent.materialWord >> 8) & 3u;
    if (!appendNearTransitions(
            ram, parent, lattice, work, 0x8006d1c8u + orientation * 0x6cu, material, out, why)) {
      return false;
    }
    for (uint32_t child = 0; child < 16; ++child) {
      const uint32_t descriptor = ram.r32(0x8006d0f8u + child * 4u);
      const uint32_t selector = (descriptor & 0x3cu) + orientation;
      const int8_t pairOffset = (int8_t)ram.r8(0x8006d388u + selector);
      const uint32_t pair = material + 0x28u + (int32_t)pairOffset;
      if (!mapped(ram, pair, 8u)) {
        why = "near_triangle_texture";
        return false;
      }
      Face face{};
      if (!makeChild(parent,
                     lattice,
                     topology(descriptor),
                     triangleTile(ram, descriptor, orientation, pair),
                     Origin::Near,
                     face)) {
        why = "near_triangle_child";
        return false;
      }
      if (!face.vertexCount) {
        continue;
      }
      const bool oversized = oversizedTriangle(face);
      if (!appendNearFace(out, face, oversized, clipRight, why)) {
        return false;
      }
    }
  }
  return true;
}

} // namespace

bool append(const RamView &ram,
            const ProjectionParams &projection,
            int clipRight,
            const Work &work,
            Recipe &out,
            const char *&why) {
  if (!appendMedium(ram, projection, clipRight, work, out, why) ||
      !appendNearQuads(ram, projection, clipRight, work, out, why) ||
      !appendNearTriangles(ram, projection, clipRight, work, out, why)) {
    return false;
  }
  return true;
}

} // namespace spyro::world_hq_refinement
