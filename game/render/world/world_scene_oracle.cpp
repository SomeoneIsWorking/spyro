#include "world_scene_oracle.h"

#include <string_view>

namespace spyro::world_scene_oracle {
namespace {

Record fromFace(const world_recipe::Face &face) {
  Record out{};
  out.family = face.family;
  out.vertexCount = face.vertexCount;
  out.otBin = face.otBin;
  out.material = face.material;
  for (uint32_t i = 0; i < face.vertexCount; ++i) {
    const auto &source = face.vertices[i];
    out.vertices[i] = {source.sx, source.sy, source.rgb & 0x00ffffffu, source.u, source.v};
  }
  return out;
}

bool equalMaterial(const Record &left, const Record &right) {
  if (left.material.textured != right.material.textured ||
      left.material.semiTransparent != right.material.semiTransparent) {
    return false;
  }
  if (left.material.textured) {
    return left.material.clut == right.material.clut && left.material.tpage == right.material.tpage;
  }
  // An opaque untextured GP0 primitive does not carry a tpage or CLUT. For a
  // semi-transparent untextured primitive, the preceding DR_MODE packet makes
  // the ABR bits observable and therefore comparable.
  return !left.material.semiTransparent || left.material.tpage == right.material.tpage;
}

bool validVertexCount(const world_recipe::Face &face) {
  switch (face.family) {
  case world_recipe::Family::G3:
  case world_recipe::Family::GT3:
    return face.vertexCount == 3;
  case world_recipe::Family::G4:
  case world_recipe::Family::GT4:
    return face.vertexCount == 4;
  }
  return false;
}

} // namespace

bool expected(std::span<const world_recipe::Face> faces, std::vector<Record> &records) {
  std::vector<size_t> indices;
  records.clear();
  for (const auto &face : faces) {
    if (!validVertexCount(face)) {
      return false;
    }
  }
  if (!world_recipe::paintOrder(faces, indices)) {
    return false;
  }
  records.reserve(faces.size());
  for (const size_t index : indices) {
    records.push_back(fromFace(faces[index]));
  }
  return true;
}

Difference compare(std::span<const Record> retail, std::span<const Record> semantic) {
  Difference out{};
  if (retail.size() != semantic.size()) {
    out.equal = false;
    out.field = "record_count";
    return out;
  }
  for (size_t record = 0; record < retail.size(); ++record) {
    out.record = record;
    const Record &left = retail[record];
    const Record &right = semantic[record];
    if (left.vertexCount > left.vertices.size() || right.vertexCount > right.vertices.size()) {
      out.equal = false;
      out.field = "vertex_count";
      return out;
    }
#define WORLD_ORACLE_RECORD_FIELD(name)                                                            \
  if (left.name != right.name) {                                                                   \
    out.equal = false;                                                                             \
    out.field = #name;                                                                             \
    return out;                                                                                    \
  }
    WORLD_ORACLE_RECORD_FIELD(family)
    WORLD_ORACLE_RECORD_FIELD(vertexCount)
    WORLD_ORACLE_RECORD_FIELD(otBin)
#undef WORLD_ORACLE_RECORD_FIELD
    if (!equalMaterial(left, right)) {
      out.equal = false;
      out.field = "material";
      return out;
    }
    for (uint32_t vertex = 0; vertex < left.vertexCount; ++vertex) {
      const Vertex &a = left.vertices[vertex];
      const Vertex &b = right.vertices[vertex];
#define WORLD_ORACLE_VERTEX_FIELD(name)                                                            \
  if (a.name != b.name) {                                                                          \
    out.equal = false;                                                                             \
    out.field = #name;                                                                             \
    return out;                                                                                    \
  }
      WORLD_ORACLE_VERTEX_FIELD(sx)
      WORLD_ORACLE_VERTEX_FIELD(sy)
      WORLD_ORACLE_VERTEX_FIELD(rgb)
      WORLD_ORACLE_VERTEX_FIELD(u)
      WORLD_ORACLE_VERTEX_FIELD(v)
#undef WORLD_ORACLE_VERTEX_FIELD
    }
  }
  return out;
}

bool corruptionSelftest() {
  Record source{};
  source.family = world_recipe::Family::GT3;
  source.vertexCount = 3;
  source.otBin = 47;
  source.material = {.textured = true, .semiTransparent = false, .clut = 0x31, .tpage = 0x82};
  source.vertices[0] = {.sx = -7, .sy = 11, .rgb = 0x102030, .u = 4, .v = 5};
  source.vertices[1] = {.sx = 8, .sy = 12, .rgb = 0x405060, .u = 6, .v = 7};
  source.vertices[2] = {.sx = 2, .sy = 19, .rgb = 0x708090, .u = 8, .v = 9};
  const std::array<Record, 1> reference{source};
  if (!compare(reference, reference).equal) {
    return false;
  }
  auto corrupted = reference;
  corrupted[0].vertices[1].rgb ^= 1u;
  const Difference difference = compare(reference, corrupted);
  return !difference.equal && difference.record == 0 && difference.field == std::string_view("rgb");
}

} // namespace spyro::world_scene_oracle
