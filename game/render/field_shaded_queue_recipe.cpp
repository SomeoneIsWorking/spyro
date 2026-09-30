#include "field_shaded_queue_recipe.h"

#include "projection_stream.h"

#include "scene_painter_order.h"
#include "shaded_moby_light.h"
#include "wide_clip_plan.h"

#include <lucent/log.h>

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

// Variant 1's arm (`.L80023534`'s fall-through, 0x80023574-0x800236D4): the primitive's word after
// its indices is a signed NORMAL, lit exactly like a vertex normal -- rotated by the record's
// matrix (MVMVA at 0x800235A0), scaled by the entry's factor (GPF 0), then CC against the light
// colour matrix with the entry as background -- and then given the entry's highlight. An earlier
// revision of this arm read that word as a COLOUR and multiplied it by the scale, skipping the
// MVMVA at 0x800235A0 that sits between the `mtc2` loads and the GPF. It is one owner with the
// per-vertex pass, `shaded_moby_light.h`, because both are the same GTE program.
uint32_t shadeVariantOne(const Input &input, const Record &record, uint32_t normal) {
  return shaded_light::faceColour(
      {.rotation = record.affine.m, .colourMatrix = input.colourMatrix, .entry = record.lightEntry},
      normal);
}

Vertex asVertex(const psxport::native_projection::NativeProjectedVertex &result) {
  return {.sx = result.sx,
          .sy = result.sy,
          .sz = result.sz,
          .screenX = result.px,
          .screenY = result.py,
          .viewZ = result.pz};
}

// The predecessor of `input.records[index]`, or nullptr when there is none to sample against. A
// paired record must present the same model to the sampler: each endpoint's own model vertex goes
// through its own transform, so a differing vertex count has no correspondence to sample.
const Record *pairedEndpoint(const Interval *interval, size_t index, const Record &record) {
  if (interval == nullptr || index >= interval->previous.size()) {
    return nullptr;
  }
  const Record *const endpoint = interval->previous[index];
  if (endpoint == nullptr || endpoint->vertices.size() != record.vertices.size()) {
    return nullptr;
  }
  return endpoint;
}

// What a record's geometry was actually projected through. `Declined` is not `Own`: one means the
// record had no predecessor to sample against, the other that it had one and the framework refused
// the interval. A single "not sampled" count could not tell a rule that ran from one that did not.
enum class Projected : uint8_t { Own, Sampled, Declined, Unavailable };

// Project one record's model vertices, through the interval when it has a predecessor. The
// framework refuses an interval whose endpoints carry wrapped accumulator history, which has no
// defined interpolation; that record falls back to its own transform rather than losing its
// geometry. `Unavailable` cannot occur in practice — endpoint mode never refuses — and is reported
// rather than assumed away.
Projected projectRecord(const Record &record,
                        const Record *endpoint,
                        const psxport::native_projection::ProjectionParams &projection,
                        double t,
                        std::vector<Vertex> &projected) {
  projected.clear();
  projected.reserve(record.vertices.size());
  bool declined = false;
  if (endpoint != nullptr) {
    const ProjectionStream interval(endpoint->affine, record.affine, projection, t);
    bool complete = true;
    for (size_t i = 0; i < record.vertices.size() && complete; ++i) {
      const auto sampled = interval.project(endpoint->vertices[i], record.vertices[i]);
      complete = sampled.has_value();
      if (complete) {
        projected.push_back(asVertex(*sampled));
      }
    }
    if (complete) {
      return Projected::Sampled;
    }
    declined = true;
    projected.clear();
  }
  const ProjectionStream own(record.affine, projection);
  for (const auto &source : record.vertices) {
    const auto result = own.project(source, source);
    if (!result) {
      return Projected::Unavailable;
    }
    projected.push_back(asVertex(*result));
  }
  return declined ? Projected::Declined : Projected::Own;
}

Recipe refuse(Recipe recipe, Status status, const Record &record, uint32_t primitive) {
  recipe.status = status;
  recipe.firstUnsupportedActor = record.actor;
  recipe.firstUnsupportedPrimitive = primitive;
  recipe.faces.clear();
  return recipe;
}
} // namespace

Recipe derive(const Input &input, const Interval *interval) {
  Recipe recipe{};
  recipe.sourceRecords = (uint32_t)input.records.size();
  uint32_t paintGroup = 0;
  std::vector<Vertex> projected;
  for (size_t recordIndex = 0; recordIndex < input.records.size(); ++recordIndex) {
    const Record &record = input.records[recordIndex];
    if (record.vertices.empty() || record.vertices.size() > 127u || input.clipRight <= 0) {
      return refuse(std::move(recipe), Status::InvalidInput, record, 0);
    }
    const Record *const endpoint = pairedEndpoint(interval, recordIndex, record);
    switch (projectRecord(record,
                          endpoint,
                          record.projection.value_or(input.projection),
                          interval ? interval->t : 1.0,
                          projected)) {
    case Projected::Sampled:
      ++recipe.sampled;
      break;
    case Projected::Declined:
      ++recipe.sampleDeclined;
      break;
    case Projected::Unavailable:
      return refuse(std::move(recipe), Status::InvalidInput, record, 0);
    case Projected::Own:
      break;
    }
    // The moby's WORLD ordering-table bin, one per record. Retail computes it at r_moby.s
    // 0x80022CD8-0x80022DA8 and parks it in the GTE's DQB register until 0x800239C4 reads it back
    // to splice this producer's whole chain into `g_WorldOT` at that single bin:
    //
    //     sra $t9, $v1, 6        ; $v1 is TRZ, doubled at 0x80022CD4
    //     sra $a0, $s4, 24       ; Moby::m_DepthOffset, signed
    //     sub $t9, $t9, $a0
    //     bgez/addi $t9, 0       ; clamped at 0
    //
    // The 288-entry table this recipe's `ot` indexes orders faces WITHIN one moby and is chained
    // into that one world bin, so `ot` was never a world position. Submitting it as one is what
    // let a distant gem outrank near terrain (issue 0120).
    const int64_t worldBin =
        std::max((int64_t)(record.affine.t[2] >> 6) - record.depthOffset, (int64_t)0);
    if (worldBin >= scene_painter_order::kWorldOtBins) {
      return refuse(std::move(recipe), Status::InvalidOtBin, record, 0);
    }
    uint32_t commonClip = 0x0fu;
    for (const Vertex &vertex : projected) {
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
      const bool variantOne = variant == 1u;
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
      // Only the lit flat arm (variant 3, r_moby.s 0x80023720) has the near-camera exemption: its
      // `addi $t3, $t3, -0x800 ; bgez` (0x80023734-0x80023738) skips the facing test below TRZ
      // 0x800. Variants 0, 1 and 2 (0x80023330-0x80023388, 0x80023534-0x80023570) cull a face whose
      // first NCLIP is not positive unless a quad's second winding (v3, v1, v2) is negative, at any
      // depth, and never add the reverse bias. Applying the exemption to every variant drew the
      // HUD's back faces (TRZ 1440) as black patches.
      const bool nearExempt = variant == 3u && nearCamera;
      const int32_t firstFacing =
          nclip(projected[index[0]], projected[index[1]], projected[index[2]]);
      bool reverseFacing = false;
      if (firstFacing <= 0) {
        if (!nearExempt) {
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
      if (reverseFacing && variant == 3u) {
        depth += 512;
      }
      if (depth <= 0) {
        ++recipe.rejected;
        continue;
      }
      const int64_t ot = depth >> 5;
      if (ot < 0 || ot >= scene_painter_order::kQueuedWorldSubBins) {
        return refuse(std::move(recipe), Status::InvalidOtBin, record, primitiveOrdinal);
      }
      Face face{.actor = record.actor,
                .actorOrdinal = record.actorOrdinal,
                .primitiveOrdinal = primitiveOrdinal,
                .paintGroup = paintGroup++,
                .otBin = (uint16_t)ot,
                .worldBin = (uint16_t)worldBin,
                .vertexCount = count,
                .semiTransparent = variantOne ? record.lightEntryIndex == 0
                                              : (lit && nearCamera && firstFacing >= 0),
                .gouraud = !lit};
      if (!lit) {
        for (uint32_t i = 0; i < count; ++i) {
          face.rgb[i] = primitive.vertexColours[i] & 0x00ffffffu;
        }
      } else if (variantOne) {
        face.rgb.fill(shadeVariantOne(input, record, primitive.normal));
      } else {
        const uint32_t rgb = shade(input, record, primitive.normal, reverseFacing);
        face.rgb.fill(rgb);
      }
      // DIAGNOSTIC. `PSXPORT_DEBUG=shadedface` prints one line per assembled face and NAMES THE
      // BRANCH it came from — so a colour disagreement with retail cannot be mis-attributed to
      // whichever arm happens to be in mind: `lit=0` means the vertex colours were copied with no
      // shading at all, and the arms' own inputs are printed beside the result.
      lucent::debug("shadedface",
                    "actor=0x{:08X} ordinal={} prim={} count={} lit={} variant1={} reverse={} "
                    "firstFacing={} rgb=0x{:08X} base=0x{:08X} scale=0x{:08X} entry=0x{:08X} "
                    "entryIndex={} colour=0x{:08X} "
                    // The OT-bin arithmetic, term by term. The line printed the colour branch and
                    // nothing about depth, so a bin disagreeing with retail by 154 could not be
                    // attributed to the sz sum, the origin bias or the shift. Retail computes the
                    // same three at r_moby.s 0x800233E0-0x800233FC; printing each separately is
                    // what lets ONE of them be blamed instead of the recipe as a whole. `t2` is the
                    // actor's view-Z origin, which this recipe uses as an APPROXIMATION of retail's
                    // TRZ (see the nearCamera comment above) -- if the bias is the fault, that
                    // approximation is where to look.
                    "szsum={} t2={} bias={} depth={} ot={} depth_offset={} world_bin={}",
                    record.actor,
                    record.actorOrdinal,
                    primitiveOrdinal,
                    count,
                    lit,
                    variantOne,
                    reverseFacing,
                    firstFacing,
                    face.rgb[0],
                    record.lightBase,
                    record.lightScale,
                    record.lightEntry,
                    record.lightEntryIndex,
                    primitive.normal,
                    (int64_t)projected[index[0]].sz + projected[index[1]].sz +
                        projected[index[2]].sz + projected[index[3]].sz,
                    record.affine.t[2],
                    (int64_t)std::max(record.affine.t[2] - 256, 0) * 4,
                    depth,
                    ot,
                    record.depthOffset,
                    worldBin);
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
  case Status::InvalidOtBin:
    return "InvalidOtBin";
  }
  return "<unknown>";
}
