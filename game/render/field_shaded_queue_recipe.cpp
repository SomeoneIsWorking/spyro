#include "field_shaded_queue_recipe.h"

#include "wide_clip_plan.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>

namespace spyro::field_shaded_queue_recipe {
namespace {

int32_t nclip(const Vertex &a, const Vertex &b, const Vertex &c) {
  const int64_t value = (int64_t)a.sx * b.sy + (int64_t)b.sx * c.sy + (int64_t)c.sx * a.sy -
                        (int64_t)a.sx * c.sy - (int64_t)b.sx * a.sy - (int64_t)c.sx * b.sy;
  return (int32_t)(uint32_t)value;
}

int32_t clampIr(int64_t value) {
  return (int32_t)std::clamp<int64_t>(value, -32768, 32767);
}

std::array<int32_t, 3> transformNormal(const psxport::native_projection::FixedAffine &affine,
                                       std::array<int32_t, 3> source) {
  std::array<int32_t, 3> out{};
  for (uint32_t row = 0; row < 3; ++row) {
    int64_t sum = 0;
    for (uint32_t column = 0; column < 3; ++column) {
      sum += (int64_t)affine.m[row][column] * source[column];
    }
    out[row] = clampIr(sum >> 12);
  }
  return out;
}

uint32_t shade(const Input &input, const Record &record, uint32_t normal, bool reverseFacing) {
  const auto transformed = transformNormal(
      record.affine, {(int8_t)(normal >> 16), (int8_t)(normal >> 8), (int8_t)(normal >> 24)});
  int64_t colourMac = 0;
  for (uint32_t i = 0; i < 3; ++i) {
    colourMac += (int64_t)input.colourMatrix[0][i] * transformed[i];
  }
  int32_t factor = (int32_t)(colourMac >> (reverseFacing ? 2 : 8));
  uint32_t base = record.lightBase;
  if (reverseFacing) {
    base >>= 1;
  }

  std::array<int32_t, 3> linear = {(int32_t)((base << 4) & 0xff0u),
                                   (int32_t)((base >> 4) & 0xff0u),
                                   (int32_t)((base >> 12) & 0xff0u)};
  const std::array<int32_t, 3> scale = {(int32_t)((record.lightScale << 4) & 0xff0u),
                                        (int32_t)((record.lightScale >> 4) & 0xff0u),
                                        (int32_t)((record.lightScale >> 12) & 0xff0u)};
  for (uint32_t i = 0; i < 3; ++i) {
    linear[i] += (int32_t)(((int64_t)factor * scale[i]) >> 12);
  }
  const int32_t boost = factor - 1472;
  if (boost > 0) {
    for (int32_t &channel : linear) {
      channel += boost << 3;
    }
  }
  const auto channel = [](int32_t value) {
    return (uint32_t)(std::clamp(value, 0, 4095) >> 4);
  };
  return channel(linear[0]) | (channel(linear[1]) << 8) | (channel(linear[2]) << 16);
}

Vertex projectVertex(const psxport::native_projection::FixedAffine &affine,
                     const psxport::native_projection::ProjectionParams &projection,
                     psxport::native_projection::ModelVertex source) {
  const auto result = psxport::native_projection::project(affine, projection, source);
  return {.sx = result.sx,
          .sy = result.sy,
          .sz = result.sz,
          .screenX = result.px,
          .screenY = result.py,
          .viewZ = result.raw_view[2]};
}

Recipe refuse(Recipe recipe, Status status, const Record &record, uint32_t primitive) {
  recipe.status = status;
  recipe.firstUnsupportedActor = record.actor;
  recipe.firstUnsupportedPrimitive = primitive;
  recipe.faces.clear();
  return recipe;
}

} // namespace

Recipe derive(const Input &input) {
  Recipe recipe{};
  recipe.sourceRecords = (uint32_t)input.records.size();
  uint32_t paintGroup = 0;
  for (const Record &record : input.records) {
    if (record.vertices.empty() || record.vertices.size() > 127u || input.clipRight <= 0) {
      return refuse(std::move(recipe), Status::InvalidInput, record, 0);
    }
    std::vector<Vertex> projected;
    projected.reserve(record.vertices.size());
    uint32_t commonClip = 0x0fu;
    for (const auto &source : record.vertices) {
      projected.push_back(projectVertex(record.affine, input.projection, source));
      const Vertex &vertex = projected.back();
      commonClip &= spyro::wide::clipCode(vertex.sx, vertex.sy, input.clipRight);
    }
    if (record.clipMode && commonClip != 0u) {
      recipe.rejected += (uint32_t)record.primitives.size();
      continue;
    }

    for (uint32_t primitiveOrdinal = 0; primitiveOrdinal < record.primitives.size();
         ++primitiveOrdinal) {
      ++recipe.candidates;
      const Primitive &primitive = record.primitives[primitiveOrdinal];
      const uint32_t variant = primitive.normal & 3u;
      if (variant != 0u && variant != 3u) {
        return refuse(std::move(recipe), Status::UnsupportedVariant, record, primitiveOrdinal);
      }
      const std::array<uint32_t, 4> index = {(primitive.indices >> 23) & 0x7fu,
                                             (primitive.indices >> 16) & 0x7fu,
                                             (primitive.indices >> 9) & 0x7fu,
                                             (primitive.indices >> 2) & 0x7fu};
      if (std::ranges::any_of(index, [&](uint32_t value) {
            return value >= projected.size();
          })) {
        return refuse(std::move(recipe), Status::InvalidInput, record, primitiveOrdinal);
      }
      const uint8_t count = index[2] == index[3] ? 3u : 4u;
      const int32_t firstFacing =
          nclip(projected[index[0]], projected[index[1]], projected[index[2]]);
      bool reverseFacing = false;
      if (firstFacing <= 0) {
        if (count == 3u ||
            nclip(projected[index[3]], projected[index[1]], projected[index[2]]) >= 0) {
          ++recipe.rejected;
          continue;
        }
        reverseFacing = true;
      }
      int64_t depth = (int64_t)projected[index[0]].sz + projected[index[1]].sz +
                      projected[index[2]].sz + projected[index[3]].sz;
      depth -= (int64_t)std::max(record.affine.t[2] - 256, 0) * 4;
      if (reverseFacing) {
        depth += 512;
      }
      if (depth <= 0) {
        ++recipe.rejected;
        continue;
      }
      const int64_t ot = depth >> 5;
      if (ot < 0 || ot >= 288) {
        return refuse(std::move(recipe), Status::InvalidOtBin, record, primitiveOrdinal);
      }
      Face face{.actorOrdinal = record.actorOrdinal,
                .primitiveOrdinal = primitiveOrdinal,
                .paintGroup = paintGroup++,
                .otBin = (uint16_t)ot,
                .vertexCount = count,
                .semiTransparent = variant == 3u,
                .gouraud = variant == 0u};
      if (variant == 0u) {
        for (uint32_t i = 0; i < count; ++i) {
          face.rgb[i] = primitive.vertexColours[i] & 0x00ffffffu;
        }
      } else {
        const uint32_t rgb = shade(input, record, primitive.normal, reverseFacing);
        const uint32_t command =
            (count == 3u ? 0x22000000u : 0x2a000000u) - (uint32_t)record.lightingOffset + rgb;
        const uint32_t expectedOpcode = count == 3u ? 0x22u : 0x2au;
        if ((command >> 24) != expectedOpcode) {
          return refuse(std::move(recipe), Status::UnsupportedLighting, record, primitiveOrdinal);
        }
        face.rgb.fill(command & 0x00ffffffu);
      }
      for (uint32_t i = 0; i < count; ++i) {
        face.vertices[i] = projected[index[i]];
      }
      recipe.faces.push_back(face);
    }
  }
  recipe.status = recipe.faces.empty() ? Status::ValidEmpty : Status::Ready;
  return recipe;
}

} // namespace spyro::field_shaded_queue_recipe
