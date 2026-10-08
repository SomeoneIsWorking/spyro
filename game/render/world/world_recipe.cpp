#include "world_recipe.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace spyro::world_recipe {
namespace {

constexpr std::array<uint32_t, 7> kTriChildren = {
    0x00103000u, 0x10304000u, 0x10204000u, 0x30405000u, 0x00201001u, 0x20504001u, 0x50003001u};
constexpr std::array<uint32_t, 12> kQuadChildren = {0x00103000u,
                                                    0x10304000u,
                                                    0x10204000u,
                                                    0x20405000u,
                                                    0x30406000u,
                                                    0x40607000u,
                                                    0x40507000u,
                                                    0x50708000u,
                                                    0x00201001u,
                                                    0x20805001u,
                                                    0x80607001u,
                                                    0x60003001u};

int16_t averageSigned(int16_t left, int16_t right) {
  return (int16_t)(((int32_t)left + (int32_t)right) >> 1);
}

uint8_t averageByte(uint8_t left, uint8_t right) {
  return (uint8_t)(((uint32_t)left + (uint32_t)right) >> 1);
}

uint32_t averageRgb(uint32_t left, uint32_t right) {
  // This is the renderer's packed-channel average. Masking before the shift is
  // what prevents blue/green carries from changing their neighbour.
  return ((left & 0xfffefeffu) + (right & 0xfffefeffu)) >> 1;
}

bool outside(const Vertex &a, const Vertex &b, const Vertex &c) {
  return (a.clip & b.clip & c.clip & 0x0fu) != 0;
}

bool oversized(const Vertex &a, const Vertex &b, const Vertex &c) {
  const auto edge = [](const Vertex &x, const Vertex &y) {
    return std::abs((int)x.sy - (int)y.sy) >= 512 || std::abs((int)x.sx - (int)y.sx) >= 1024;
  };
  return edge(a, b) || edge(b, c) || edge(c, a);
}

std::array<uint8_t, 3> indices(uint32_t descriptor) {
  return {(uint8_t)(descriptor >> 28),
          (uint8_t)((descriptor >> 20) & 0x0fu),
          (uint8_t)((descriptor >> 12) & 0x0fu)};
}

bool emitAdaptiveChildren(const Face &parent,
                          std::vector<Face> &accepted,
                          size_t faceLimit,
                          size_t &visited,
                          int clipRight) {
  if (++visited > faceLimit * 4u + 1u) {
    return false;
  }
  std::array<Vertex, 9> vertices{};
  const bool quad = parent.vertexCount == 4;
  if (quad) {
    vertices[0] = parent.vertices[0];
    vertices[2] = parent.vertices[1];
    vertices[6] = parent.vertices[2];
    vertices[8] = parent.vertices[3];
    vertices[1] = midpoint(vertices[0], vertices[2], clipRight);
    vertices[3] = midpoint(vertices[0], vertices[6], clipRight);
    vertices[5] = midpoint(vertices[2], vertices[8], clipRight);
    vertices[7] = midpoint(vertices[6], vertices[8], clipRight);
    vertices[4] = midpoint(vertices[3], vertices[5], clipRight);
  } else {
    vertices[0] = parent.vertices[0];
    vertices[2] = parent.vertices[1];
    vertices[5] = parent.vertices[2];
    vertices[1] = midpoint(vertices[0], vertices[2], clipRight);
    vertices[3] = midpoint(vertices[0], vertices[5], clipRight);
    vertices[4] = midpoint(vertices[2], vertices[5], clipRight);
  }

  const auto descriptors =
      quad ? std::span<const uint32_t>(kQuadChildren) : std::span<const uint32_t>(kTriChildren);
  for (uint32_t descriptor : descriptors) {
    const auto index = indices(descriptor);
    if (index[0] >= vertices.size() || index[1] >= vertices.size() || index[2] >= vertices.size()) {
      return false;
    }
    const Vertex &a = vertices[index[0]], &b = vertices[index[1]], &c = vertices[index[2]];
    if (outside(a, b, c)) {
      continue;
    }
    const bool filler = (descriptor & 1u) != 0;
    Face child = parent;
    child.family = parent.material.textured ? Family::GT3 : Family::G3;
    child.origin = filler ? Origin::EdgeFiller : Origin::Adaptive;
    child.vertexCount = 3;
    child.vertices = {a, b, c, {}};
    if (!filler && oversized(a, b, c)) {
      if (!emitAdaptiveChildren(child, accepted, faceLimit, visited, clipRight)) {
        return false;
      }
      continue;
    }
    if (accepted.size() == faceLimit) {
      return false;
    }
    accepted.push_back(child);
  }
  return true;
}

bool equalVertex(const Vertex &left, const Vertex &right, const char *&field) {
#define WORLD_VERTEX_FIELD(name)                                                                   \
  if (left.name != right.name) {                                                                   \
    field = #name;                                                                                 \
    return false;                                                                                  \
  }
  WORLD_VERTEX_FIELD(sx)
  WORLD_VERTEX_FIELD(sy)
  WORLD_VERTEX_FIELD(sz)
  WORLD_VERTEX_FIELD(clip)
  WORLD_VERTEX_FIELD(screenX)
  WORLD_VERTEX_FIELD(screenY)
  WORLD_VERTEX_FIELD(viewZ)
  WORLD_VERTEX_FIELD(rgb)
  WORLD_VERTEX_FIELD(u)
  WORLD_VERTEX_FIELD(v)
#undef WORLD_VERTEX_FIELD
  return true;
}

} // namespace

uint8_t clipCode(int16_t sx, int16_t sy, int clipRight) {
  const uint32_t packed = (uint16_t)sx | ((uint32_t)(uint16_t)sy << 16);
  const uint32_t horizontal = packed << 16;
  uint8_t clip = 0;
  if ((int32_t)(packed - 0x00010000u) <= 0) {
    clip |= 1u;
  }
  if ((int32_t)(packed - 0x01000000u) >= 0) {
    clip |= 2u;
  }
  if ((int32_t)horizontal <= 0) {
    clip |= 4u;
  }
  if ((int32_t)(horizontal - ((uint32_t)clipRight << 16)) >= 0) {
    clip |= 8u;
  }
  return clip;
}

Vertex midpoint(const Vertex &left, const Vertex &right, int clipRight) {
  Vertex out{};
  out.sx = averageSigned(left.sx, right.sx);
  out.sy = averageSigned(left.sy, right.sy);
  out.sz = (uint16_t)(((uint32_t)left.sz + (uint32_t)right.sz) >> 1);
  out.clip = clipCode(out.sx, out.sy, clipRight);
  out.screenX = (left.screenX + right.screenX) * 0.5f;
  out.screenY = (left.screenY + right.screenY) * 0.5f;
  out.viewZ = (left.viewZ + right.viewZ) * 0.5f;
  out.rgb = averageRgb(left.rgb, right.rgb);
  out.u = averageByte(left.u, right.u);
  out.v = averageByte(left.v, right.v);
  return out;
}

bool adaptiveSubdivide(const Face &parent,
                       std::vector<Face> &out,
                       size_t faceLimit,
                       int clipRight) {
  if ((parent.vertexCount != 3 && parent.vertexCount != 4) || parent.paintGroup == UINT32_MAX ||
      out.size() > faceLimit || clipRight <= 0 || clipRight > INT16_MAX) {
    return false;
  }
  std::vector<Face> accepted;
  size_t visited = 0;
  if (!emitAdaptiveChildren(parent, accepted, faceLimit - out.size(), visited, clipRight)) {
    return false;
  }
  for (uint32_t i = 0; i < accepted.size(); ++i) {
    accepted[i].paintGroup = parent.paintGroup;
    accepted[i].paintSuborder = i;
  }
  out.insert(out.end(), accepted.begin(), accepted.end());
  return true;
}

uint32_t reservePaintGroup(Recipe &recipe) {
  return recipe.nextPaintGroup++;
}

bool appendLinked(Recipe &recipe, Face face, size_t faceLimit) {
  if (recipe.faces.size() >= faceLimit) {
    return false;
  }
  if (face.paintGroup == UINT32_MAX) {
    face.paintGroup = reservePaintGroup(recipe);
  }
  recipe.faces.push_back(face);
  return true;
}

bool paintOrder(std::span<const Face> faces, std::vector<size_t> &indices) {
  indices.clear();
  indices.reserve(faces.size());
  for (size_t i = 0; i < faces.size(); ++i) {
    if (faces[i].paintGroup == UINT32_MAX || faces[i].otBin >= 0x800u) {
      indices.clear();
      return false;
    }
    indices.push_back(i);
  }
  std::stable_sort(indices.begin(), indices.end(), [faces](size_t leftIndex, size_t rightIndex) {
    const Face &left = faces[leftIndex], &right = faces[rightIndex];
    if (left.otBin != right.otBin) {
      return left.otBin > right.otBin;
    }
    if (left.paintGroup != right.paintGroup) {
      return left.paintGroup > right.paintGroup;
    }
    return left.paintSuborder < right.paintSuborder;
  });
  for (size_t i = 1; i < indices.size(); ++i) {
    const Face &left = faces[indices[i - 1]], &right = faces[indices[i]];
    if (left.otBin == right.otBin && left.paintGroup == right.paintGroup &&
        left.paintSuborder == right.paintSuborder) {
      indices.clear();
      return false;
    }
  }
  return true;
}

Difference compare(std::span<const Face> expected, std::span<const Face> actual) {
  Difference out{};
  if (expected.size() != actual.size()) {
    out.equal = false;
    out.field = "face_count";
    return out;
  }
  for (size_t i = 0; i < expected.size(); ++i) {
    out.face = i;
    const Face &left = expected[i], &right = actual[i];
#define WORLD_FACE_FIELD(name)                                                                     \
  if (left.name != right.name) {                                                                   \
    out.equal = false;                                                                             \
    out.field = #name;                                                                             \
    return out;                                                                                    \
  }
    WORLD_FACE_FIELD(family)
    WORLD_FACE_FIELD(origin)
    WORLD_FACE_FIELD(vertexCount)
    WORLD_FACE_FIELD(otBin)
    WORLD_FACE_FIELD(sector)
    WORLD_FACE_FIELD(source)
    WORLD_FACE_FIELD(sourceOrdinal)
    WORLD_FACE_FIELD(paintGroup)
    WORLD_FACE_FIELD(paintSuborder)
#undef WORLD_FACE_FIELD
    if (left.material.textured != right.material.textured ||
        left.material.semiTransparent != right.material.semiTransparent ||
        left.material.clut != right.material.clut || left.material.tpage != right.material.tpage) {
      out.equal = false;
      out.field = "material";
      return out;
    }
    for (uint32_t vertex = 0; vertex < left.vertexCount; ++vertex) {
      if (!equalVertex(left.vertices[vertex], right.vertices[vertex], out.field)) {
        out.equal = false;
        return out;
      }
    }
  }
  return out;
}

} // namespace spyro::world_recipe
