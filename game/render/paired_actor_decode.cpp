#include "paired_actor_decode.h"

#include <algorithm>
#include <limits>

namespace spyro::paired_actor {
namespace {

uint16_t narrow_offset(uint32_t value) { return (uint16_t)value; }

}  // namespace

DecodeResult decode_normal_stream(std::span<const uint32_t> words) {
  DecodeResult result;
  if (words.empty()) {
    result.error = "normal primitive stream is missing its end offset";
    return result;
  }
  const uint32_t end_bytes = words[0];
  if ((end_bytes & 3u) != 0u || end_bytes > words.size_bytes() - sizeof(uint32_t)) {
    result.error = "normal primitive stream has an invalid end offset";
    return result;
  }

  const size_t end = 1u + end_bytes / 4u;
  size_t at = 1;
  uint32_t ordinal = 0;
  while (at < end) {
    const uint32_t w0 = words[at];
    const bool quad = (int32_t)w0 < 0;
    const size_t record_words = quad ? 6u : 5u;
    if (record_words > end - at) {
      result.error = "normal primitive stream ends inside a record";
      result.primitives.clear();
      return result;
    }

    const uint32_t w1 = words[at + 1u];
    Primitive p;
    p.source_ordinal = ordinal++;
    p.quad = quad;
    p.semi_transparent = (w1 & 1u) != 0;
    p.ot_adjust = (int8_t)((int32_t)w1 >> 28);
    p.projected_offset[0] = narrow_offset((w0 >> 18) & 0xFF0u);
    p.projected_offset[1] = narrow_offset((w0 >> 9) & 0xFF0u);
    p.projected_offset[2] = narrow_offset(w0 & 0xFF0u);
    p.material_offset[0] = narrow_offset((w1 >> 17) & 0x7FCu);
    p.material_offset[1] = narrow_offset((w1 >> 8) & 0x7FCu);
    p.material_offset[2] = narrow_offset((w1 & 0x3FEu) << 1);
    if (quad) {
      const uint32_t w2 = words[at + 2u];
      p.projected_offset[3] = narrow_offset(w2 & 0x7FCu);
      p.material_offset[3] = narrow_offset((w2 >> 9) & 0x7FCu);
      p.packet_attr[0] = words[at + 3u];
      p.packet_attr[1] = words[at + 4u];
      p.packet_attr[2] = words[at + 5u];
      p.packet_attr[3] = words[at + 5u] >> 16;
    } else {
      p.packet_attr[0] = words[at + 2u];
      p.packet_attr[1] = words[at + 3u];
      p.packet_attr[2] = words[at + 4u];
    }
    result.primitives.push_back(p);
    at += record_words;
  }
  if (at != end) {
    result.error = "normal primitive stream did not terminate on its declared boundary";
    result.primitives.clear();
  }
  return result;
}

bool resolve_material(const Primitive& primitive, const MaterialTables& tables,
                      ResolvedMaterial& out, std::string& error) {
  const bool use_override = (tables.override_control >> 24) != 0;
  const std::span<const uint32_t> selected = use_override ? tables.override_colors : tables.base;
  if (selected.empty()) {
    error = use_override ? "material override is active but its color table is missing"
                         : "base material color table is missing";
    return false;
  }
  const int count = primitive.quad ? 4 : 3;
  for (int i = 0; i < count; ++i) {
    const uint32_t offset = primitive.material_offset[i];
    if ((offset & 3u) != 0u || offset / 4u >= selected.size()) {
      error = "primitive material offset is outside the selected color table";
      return false;
    }
    out.rgb[i] = selected[offset / 4u] & 0x00FFFFFFu;
  }
  out.command = (uint8_t)((primitive.quad ? 0x3Cu : 0x34u) +
                          (primitive.semi_transparent ? 0x02u : 0u));
  error.clear();
  return true;
}

bool compute_ot_bin(const Primitive& primitive, const uint32_t vertex_depth[4],
                    uint32_t depth_origin, uint8_t shift, uint32_t& raw, uint32_t& bin) {
  // R3000 variable shifts use only the low five bits. `shift` is CR29+4 in the guest and retaining
  // this mask matters even for malformed/control-state inputs: rejecting >=32 is not what hardware
  // does.
  shift &= 31u;
  // Unsigned operations deliberately retain the R3000's 32-bit wrapping. The final gate is signed.
  const uint32_t weighted = primitive.quad
      ? vertex_depth[0] + vertex_depth[1] + vertex_depth[2] + vertex_depth[3]
      : vertex_depth[0] + (vertex_depth[0] >> 1) +
        vertex_depth[1] + (vertex_depth[1] >> 1) + vertex_depth[2];
  const uint32_t adjust = ((uint32_t)(int32_t)primitive.ot_adjust * 4u) << shift;
  raw = weighted - depth_origin * 4u + adjust;
  if ((int32_t)raw <= 0) {
    bin = 0;
    return false;
  }
  bin = raw >> shift;
  return true;
}

std::vector<OrderedPrimitive> stable_descending_bins(std::span<const OrderedPrimitive> input) {
  std::vector<OrderedPrimitive> result(input.begin(), input.end());
  std::stable_sort(result.begin(), result.end(), [](const OrderedPrimitive& a,
                                                    const OrderedPrimitive& b) {
    return a.ot_bin > b.ot_bin;
  });
  return result;
}

}  // namespace spyro::paired_actor
