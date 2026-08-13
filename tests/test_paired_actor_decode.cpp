#include "paired_actor_decode.h"

#include <array>
#include <cstdio>
#include <cstdlib>

using namespace spyro::paired_actor;

namespace {

void require(bool condition, const char* what) {
  if (!condition) { std::fprintf(stderr, "paired_actor_decode: %s\n", what); std::abort(); }
}

uint32_t tri_w0(uint16_t a, uint16_t b, uint16_t c) {
  return ((uint32_t)a << 18) | ((uint32_t)b << 9) | c;
}

uint32_t mat_w1(uint16_t a, uint16_t b, uint16_t c, int8_t adjust, bool semi) {
  return ((uint32_t)(uint8_t)adjust << 28) | ((uint32_t)a << 17) |
         ((uint32_t)b << 8) | ((uint32_t)c >> 1) | (semi ? 1u : 0u);
}

}  // namespace

int main() {
  // Synthetic values exercise the exact masks seen in the stage-13 dump without embedding assets.
  const uint32_t t0 = tri_w0(0x120, 0x240, 0x360);
  const uint32_t t1 = mat_w1(0x40, 0x80, 0xC0, -2, true);
  const uint32_t q0 = 0x80000000u | tri_w0(0x150, 0x250, 0x350);
  const uint32_t q1 = mat_w1(0x44, 0x84, 0xC4, 3, false);
  const uint32_t q2 = ((uint32_t)0x104 << 9) | 0x1A0u;
  const std::array<uint32_t, 12> stream = {
    44u, t0, t1, 0x11112222u, 0x33334444u, 0x55556666u,
    q0, q1, q2, 0x77778888u, 0x9999AAAAu, 0xBBBBCCCCu
  };
  const DecodeResult decoded = decode_normal_stream(stream);
  require(decoded && decoded.primitives.size() == 2, "positive stream did not decode two records");
  const Primitive& tri = decoded.primitives[0];
  const Primitive& quad = decoded.primitives[1];
  require(!tri.quad && tri.semi_transparent && tri.ot_adjust == -2,
          "triangle flags/sign extension differ from the executable");
  require(tri.projected_offset[0] == 0x120 && tri.projected_offset[1] == 0x240 &&
          tri.projected_offset[2] == 0x360, "triangle projected offsets decoded incorrectly");
  require(tri.material_offset[0] == 0x40 && tri.material_offset[1] == 0x80 &&
          tri.material_offset[2] == 0xC0, "triangle material offsets decoded incorrectly");
  require(quad.quad && quad.projected_offset[3] == 0x1A0 &&
          quad.material_offset[3] == 0x104, "quad-only fields decoded incorrectly");
  require(quad.packet_attr[3] == 0xBBBBu, "quad's compact fourth packet attribute was lost");

  // Negative discriminator: an old fixed-five-word parser would accept this truncated quad.
  const std::array<uint32_t, 6> truncated_quad = {20u, q0, q1, q2, 1u, 2u};
  const DecodeResult bad = decode_normal_stream(truncated_quad);
  require(!bad && bad.primitives.empty(), "truncated sign-bit quad was silently accepted");

  std::array<uint32_t, 128> base{}, override_colors{};
  base[0x40 / 4] = 0xAA010203u; base[0x80 / 4] = 0xBB040506u; base[0xC0 / 4] = 0xCC070809u;
  override_colors[0x40 / 4] = 0xDD111213u;
  override_colors[0x80 / 4] = 0xEE141516u;
  override_colors[0xC0 / 4] = 0xFF171819u;
  ResolvedMaterial material{}; std::string error;
  require(resolve_material(tri, {base, override_colors, 0}, material, error),
          "base material resolution failed");
  require(material.command == 0x36 && material.rgb[0] == 0x010203,
          "base material/opcode resolution is wrong");
  require(resolve_material(tri, {base, override_colors, 0x01000000u}, material, error),
          "override material resolution failed");
  require(material.rgb[0] == 0x111213 && material.rgb[2] == 0x171819,
          "active override did not replace every vertex color");
  require(!resolve_material(tri, {base, {}, 0x01000000u}, material, error),
          "active-but-missing override table did not fail loudly");

  Primitive depth_prim = tri; depth_prim.ot_adjust = -1;
  const uint32_t depth[4] = {100u, 80u, 60u, 0u};
  uint32_t raw = 0, bin = 0;
  require(compute_ot_bin(depth_prim, depth, 20u, 2u, raw, bin), "positive OT case rejected");
  // 100 + 50 + 40 + 60 - 80 + (-4 << 2) = 154; 154 >> 2 = 38.
  require(raw == 154u && bin == 38u, "OT-bin formula differs from 0x80023AC4");
  require(!compute_ot_bin(depth_prim, depth, 100u, 2u, raw, bin),
          "raw<=0 OT candidate was not rejected");
  Primitive quad_depth = quad; quad_depth.ot_adjust = 0;
  const uint32_t quad_z[4] = {10u, 20u, 30u, 40u};
  require(compute_ot_bin(quad_depth, quad_z, 4u, 1u, raw, bin) && raw == 84u && bin == 42u,
          "quad OT bin did not use the executable's four-depth sum");

  std::array<OrderedPrimitive, 4> unordered{};
  unordered[0].primitive.source_ordinal = 0; unordered[0].ot_bin = 2;
  unordered[1].primitive.source_ordinal = 1; unordered[1].ot_bin = 5;
  unordered[2].primitive.source_ordinal = 2; unordered[2].ot_bin = 5;
  unordered[3].primitive.source_ordinal = 3; unordered[3].ot_bin = 1;
  const auto ordered = stable_descending_bins(unordered);
  require(ordered[0].primitive.source_ordinal == 1 && ordered[1].primitive.source_ordinal == 2 &&
          ordered[2].primitive.source_ordinal == 0 && ordered[3].primitive.source_ordinal == 3,
          "bins are not descending and stable within equal bins");
  return 0;
}
