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
  int32_t factor = (int32_t)(colourMac >> (reverseFacing ? 10 : 8));
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
          .viewZ = result.pz};
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
      // r_moby.s 0x80023320/0x80023324: bit 0 of the primitive word selects the LIT path
      // (`bgtz $t6, .L80023534`) and bit 1 is read ONLY inside that path (`bgtz $t7, .L80023720`).
      // So with bit 0 clear both words take the identical code path and both are per-vertex
      // Gouraud: refusing bit 1 there was refusing a combination retail cannot distinguish.
      const uint32_t variant = primitive.indices & 3u;
      const bool lit = (primitive.indices & 1u) != 0u;
      if (variant == 1u) {
        // Bit 0 set with bit 1 clear — .L80023534's fall-through. r_moby.s establishes its layout:
        // flat TEXTURED quads (0x28) whose UVs come from the light table's +0x200 half, or 0x20
        // untextured triangles when the last two indices match, with the depth built the same way.
        // The recipe does not implement that family yet, so it is refused BY NAME, and the variant
        // travels with the refusal (it is what a fix has to key on).
        recipe.firstUnsupportedVariant = variant;
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
      // The authenticated flat arm keeps 0x22/0x2A only while the projected depth TRZ is below
      // 0x800 AND the face is front-facing; every other reached path subtracts 0x02000000 from the
      // command and the face is opaque. TRZ is approximated by the actor's view-Z origin.
      const bool nearCamera = record.affine.t[2] < 2048;
      const int32_t firstFacing =
          nclip(projected[index[0]], projected[index[1]], projected[index[2]]);
      bool reverseFacing = false;
      if (firstFacing <= 0) {
        if (!nearCamera) {
          if (count == 3u ||
              nclip(projected[index[3]], projected[index[1]], projected[index[2]]) >= 0) {
            ++recipe.rejected;
            continue;
          }
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
      Face face{.actor = record.actor,
                .actorOrdinal = record.actorOrdinal,
                .primitiveOrdinal = primitiveOrdinal,
                .paintGroup = paintGroup++,
                .otBin = (uint16_t)ot,
                .vertexCount = count,
                .semiTransparent = lit && nearCamera && firstFacing >= 0,
                .gouraud = !lit};
      if (!lit) {
        for (uint32_t i = 0; i < count; ++i) {
          face.rgb[i] = primitive.vertexColours[i] & 0x00ffffffu;
        }
      } else {
        const uint32_t rgb = shade(input, record, primitive.normal, reverseFacing);
        face.rgb.fill(rgb);
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

const char *spyro::field_shaded_queue_recipe::statusName(Status status) {
  switch (status) {
  case Status::Ready:
    return "Ready";
  case Status::ValidEmpty:
    return "ValidEmpty";
  case Status::InvalidInput:
    return "InvalidInput";
  case Status::UnsupportedVariant:
    return "UnsupportedVariant";
  case Status::InvalidOtBin:
    return "InvalidOtBin";
  }
  return "<unknown>";
}
