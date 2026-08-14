#include "paired_actor_decode.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace spyro::paired_actor;

namespace {

void require(bool condition, const char *what) {
  if (!condition) {
    std::fprintf(stderr, "paired_actor_decode: %s\n", what);
    std::abort();
  }
}

uint32_t tri_w0(uint16_t a, uint16_t b, uint16_t c) {
  return ((uint32_t)a << 20) | ((uint32_t)b << 11) | ((uint32_t)c << 2);
}

uint32_t mat_w1(uint16_t a, uint16_t b, uint16_t c, int8_t adjust, bool semi) {
  return ((uint32_t)(uint8_t)adjust << 28) | ((uint32_t)a << 17) | ((uint32_t)b << 8) |
         ((uint32_t)c >> 1) | (semi ? 1u : 0u);
}

} // namespace

int main() {
  // Synthetic values exercise the exact masks seen in the stage-13 dump without embedding assets.
  const uint32_t t0 = tri_w0(0x120, 0x240, 0x360);
  const uint32_t t1 = mat_w1(0x40, 0x80, 0xC0, -2, true);
  const uint32_t q0 = 0x80000000u | tri_w0(0x150, 0x250, 0x350);
  const uint32_t q1 = mat_w1(0x44, 0x84, 0xC4, 3, false);
  const uint32_t q2 = ((uint32_t)0x104 << 9) | 0x1A0u;
  const std::array<uint32_t, 12> stream = {44u,
                                           t0,
                                           t1,
                                           0x11112222u,
                                           0x33334444u,
                                           0x55556666u,
                                           q0,
                                           q1,
                                           q2,
                                           0x77778888u,
                                           0x9999AAAAu,
                                           0xBBBBCCCCu};
  const DecodeResult decoded = decode_normal_stream(stream);
  require(decoded && decoded.primitives.size() == 2, "positive stream did not decode two records");
  const Primitive &tri = decoded.primitives[0];
  const Primitive &quad = decoded.primitives[1];
  require(!tri.quad && tri.semi_transparent && tri.ot_adjust == -2,
          "triangle flags/sign extension differ from the executable");
  require(tri.projected_offset[0] == 0x120 && tri.projected_offset[1] == 0x240 &&
              tri.projected_offset[2] == 0x360,
          "triangle projected offsets decoded incorrectly");
  require(tri.material_offset[0] == 0x40 && tri.material_offset[1] == 0x80 &&
              tri.material_offset[2] == 0xC0,
          "triangle material offsets decoded incorrectly");
  require(quad.quad && quad.projected_offset[3] == 0x1A0 && quad.material_offset[3] == 0x104,
          "quad-only fields decoded incorrectly");
  require(quad.packet_attr[3] == 0xBBBBu, "quad's compact fourth packet attribute was lost");

  // Negative discriminator: an old fixed-five-word parser would accept this truncated quad.
  const std::array<uint32_t, 6> truncated_quad = {20u, q0, q1, q2, 1u, 2u};
  const DecodeResult bad = decode_normal_stream(truncated_quad);
  require(!bad && bad.primitives.empty(), "truncated sign-bit quad was silently accepted");

  std::array<uint32_t, 128> base{};
  base[0x40 / 4] = 0xAA010203u;
  base[0x80 / 4] = 0xBB040506u;
  base[0xC0 / 4] = 0xCC070809u;
  ResolvedMaterial material{};
  std::string error;
  require(resolve_material(tri, {base, 0}, material, error), "base material resolution failed");
  require(material.command == 0x36 && material.rgb[0] == 0x010203,
          "base material/opcode resolution is wrong");
  require(!resolve_material(tri, {base, 0x01000000u}, material, error),
          "normal resolver silently treated the alternate override parser as a color-table swap");

  Primitive depth_prim = tri;
  depth_prim.ot_adjust = -1;
  const uint32_t depth[4] = {100u, 80u, 60u, 0u};
  uint32_t raw = 0, bin = 0;
  require(compute_ot_bin(depth_prim, depth, 20u, 2u, raw, bin), "positive OT case rejected");
  // 100 + 50 + 80 + 40 + 60 - 80 + (-4 << 2) = 234; 234 >> 2 = 58.
  // The second vertex appears both whole and halved; omitting the whole term was a plausible but
  // wrong reading that this asymmetric vector discriminates.
  require(raw == 234u && bin == 58u, "triangle OT-bin formula differs from 0x80023AC4");
  require(!compute_ot_bin(depth_prim, depth, 100u, 2u, raw, bin),
          "raw<=0 OT candidate was not rejected");
  Primitive quad_depth = quad;
  quad_depth.ot_adjust = 0;
  const uint32_t quad_z[4] = {10u, 20u, 30u, 40u};
  require(compute_ot_bin(quad_depth, quad_z, 4u, 1u, raw, bin) && raw == 84u && bin == 42u,
          "quad OT bin did not use the executable's four-depth sum");
  require(compute_ot_bin(quad_depth, quad_z, 4u, 33u, raw, bin) && bin == 42u,
          "OT shift did not retain the R3000 low-five-bit mask");

  std::array<OrderedPrimitive, 4> unordered{};
  unordered[0].primitive.source_ordinal = 0;
  unordered[0].ot_bin = 2;
  unordered[1].primitive.source_ordinal = 1;
  unordered[1].ot_bin = 5;
  unordered[2].primitive.source_ordinal = 2;
  unordered[2].ot_bin = 5;
  unordered[3].primitive.source_ordinal = 3;
  unordered[3].ot_bin = 1;
  const auto ordered = stable_descending_bins(unordered);
  require(ordered[0].primitive.source_ordinal == 1 && ordered[1].primitive.source_ordinal == 2 &&
              ordered[2].primitive.source_ordinal == 0 && ordered[3].primitive.source_ordinal == 3,
          "bins are not descending and stable within equal bins");

  std::array<ProjectedVertex, 256> projected{};
  projected[0x120 / 4] = {10, 10, 100};
  projected[0x240 / 4] = {20, 10, 80};
  projected[0x360 / 4] = {10, 20, 60};
  projected[0x150 / 4] = {40, 40, 10};
  projected[0x250 / 4] = {40, 50, 20};
  projected[0x350 / 4] = {50, 40, 30};
  projected[0x1A0 / 4] = {50, 50, 40};
  for (auto &p : projected) {
    p.raw_view_z = (float)p.depth;
    p.view_z = (float)p.depth;
  }
  const ResolveResult faces =
      resolve_normal_faces(decoded.primitives, projected, {base, 0}, 4u, 1u);
  require(faces && faces.candidates == 2 && faces.triangles == 1 && faces.quads == 1,
          "resolved face census lost a primitive variant");
  require(faces.faces.size() == 2 && faces.faces[0].source_ordinal == 0 &&
              faces.faces[1].source_ordinal == 1,
          "resolved faces lost descending-bin/source-ordinal ordering");
  require(faces.faces[0].vertex[1].x == 20 && faces.faces[0].material.rgb[2] == 0x070809 &&
              faces.faces[0].packet_attr[1] == 0x33334444u,
          "resolved face did not join projection, material and packet attributes");

  // Negative discriminator: the previous accept-all resolver emitted a clockwise triangle.
  auto culled_projected = projected;
  culled_projected[0x240 / 4] = {10, 20, 80};
  culled_projected[0x360 / 4] = {20, 10, 60};
  const std::array<Primitive, 1> tri_only{tri};
  const ResolveResult culled = resolve_normal_faces(tri_only, culled_projected, {base, 0}, 4u, 1u);
  require(culled && culled.candidates == 1 && culled.faces.empty(),
          "normal NCLIP<=0 triangle retained by the old accept-all behavior");
  Primitive two_sided_tri = tri;
  two_sided_tri.two_sided = true;
  const std::array<Primitive, 1> two_sided_only{two_sided_tri};
  const ResolveResult two_sided_faces =
      resolve_normal_faces(two_sided_only, culled_projected, {base, 0}, 4u, 1u);
  require(two_sided_faces && two_sided_faces.faces.size() == 1,
          "word-0 two-sided bit did not bypass the NCLIP gate");

  // Temporal midpoint visibility is evaluated before integer SXY quantization. This case has a
  // genuine positive subpixel area while all three integer endpoints collapse to (0,0): the exact
  // endpoint resolver must reject it and the continuous extension must retain it.
  auto subpixel_projected = projected;
  subpixel_projected[0x120 / 4] = {0, 0, 100};
  subpixel_projected[0x240 / 4] = {0, 0, 80};
  subpixel_projected[0x360 / 4] = {0, 0, 60};
  for (auto &p : subpixel_projected) {
    if (p.depth) {
      p.raw_view_z = (float)p.depth;
      p.view_z = (float)p.depth;
    }
  }
  subpixel_projected[0x120 / 4].screen_x = 0.10f;
  subpixel_projected[0x120 / 4].screen_y = 0.10f;
  subpixel_projected[0x240 / 4].screen_x = 0.90f;
  subpixel_projected[0x240 / 4].screen_y = 0.10f;
  subpixel_projected[0x360 / 4].screen_x = 0.10f;
  subpixel_projected[0x360 / 4].screen_y = 0.90f;
  const auto quantized_subpixel =
      resolve_normal_faces(tri_only, subpixel_projected, {base, 0}, 4u, 1u);
  const auto continuous_subpixel =
      resolve_normal_faces_continuous(tri_only, subpixel_projected, {base, 0}, 4u, 1u);
  require(quantized_subpixel && quantized_subpixel.faces.empty() && continuous_subpixel &&
              continuous_subpixel.faces.size() == 1,
          "continuous normal resolver did not discriminate subpixel area from quantized NCLIP");
  require(
      std::fabs(continuous_subpixel.faces[0].continuous_ot_key - 149.0) < 1.0e-9,
      "continuous OT key did not use raw 0..65535 depth independently of projection near clamp");

  // Positive first and second NCLIPs take the guest's d-for-a diagonal substitution.
  auto split_projected = projected;
  split_projected[0x150 / 4] = {0, 0, 10};
  split_projected[0x250 / 4] = {10, 0, 20};
  split_projected[0x350 / 4] = {0, 10, 30};
  split_projected[0x1A0 / 4] = {-10, -10, 40};
  for (auto &p : split_projected) {
    if (p.depth) {
      p.raw_view_z = (float)p.depth;
      p.view_z = (float)p.depth;
    }
  }
  const std::array<Primitive, 1> quad_only{quad};
  const ResolveResult split = resolve_normal_faces(quad_only, split_projected, {base, 0}, 0u, 1u);
  for (auto &p : split_projected) {
    p.screen_x = (float)p.x;
    p.screen_y = (float)p.y;
  }
  const ResolveResult continuous_split =
      resolve_normal_faces_continuous(quad_only, split_projected, {base, 0}, 0u, 1u);
  require(split && split.faces.size() == 1 && split.faces[0].vertex[0].x == -10 &&
              !split.faces[0].quad && split.triangles == 1 && split.quads == 0 &&
              split.faces[0].material.command == 0x34 &&
              split.faces[0].material.rgb[0] == split.faces[0].material.rgb[3] &&
              (split.faces[0].packet_attr[0] & 0xFFFFu) == (quad.packet_attr[2] >> 16),
          "quad positive/positive NCLIP diagonal substitution differs from the guest");
  require(continuous_split && continuous_split.faces.size() == 1 &&
              !continuous_split.faces[0].quad && continuous_split.faces[0].vertex[0].x == -10,
          "continuous quad resolver changed the guest diagonal sign/split table");
  // A<0,B<=0 emits the other GT3 diagonal without substituting vertex zero.
  auto first_split_projected = projected;
  first_split_projected[0x150 / 4] = {0, 0, 10};
  first_split_projected[0x250 / 4] = {0, 10, 20};
  first_split_projected[0x350 / 4] = {10, 0, 30};
  first_split_projected[0x1A0 / 4] = {-10, -10, 40};
  for (auto &p : first_split_projected) {
    if (p.depth) {
      p.raw_view_z = (float)p.depth;
      p.view_z = (float)p.depth;
    }
  }
  const ResolveResult first_split =
      resolve_normal_faces(quad_only, first_split_projected, {base, 0}, 0u, 1u);
  for (auto &p : first_split_projected) {
    p.screen_x = (float)p.x;
    p.screen_y = (float)p.y;
  }
  const auto continuous_first_split =
      resolve_normal_faces_continuous(quad_only, first_split_projected, {base, 0}, 0u, 1u);
  require(first_split && first_split.faces.size() == 1 && !first_split.faces[0].quad &&
              first_split.faces[0].vertex[0].x == 0 &&
              first_split.faces[0].material.command == 0x34,
          "quad negative/nonpositive NCLIP did not emit its first GT3 diagonal");
  require(continuous_first_split && continuous_first_split.faces.size() == 1 &&
              !continuous_first_split.faces[0].quad,
          "continuous quad resolver changed the negative/nonpositive split cell");
  // A>=0,B<=0 is the sole rejected quad sign pair; accept-all and one-NCLIP implementations fail.
  auto rejected_quad_projected = split_projected;
  rejected_quad_projected[0x1A0 / 4] = {20, 20, 40};
  const ResolveResult rejected_quad =
      resolve_normal_faces(quad_only, rejected_quad_projected, {base, 0}, 0u, 1u);
  for (auto &p : rejected_quad_projected) {
    p.screen_x = (float)p.x;
    p.screen_y = (float)p.y;
  }
  const auto continuous_rejected =
      resolve_normal_faces_continuous(quad_only, rejected_quad_projected, {base, 0}, 0u, 1u);
  require(rejected_quad && rejected_quad.faces.empty(),
          "quad nonnegative/nonpositive NCLIP sign pair was not rejected");
  require(continuous_rejected && continuous_rejected.faces.empty(),
          "continuous quad resolver changed the sole rejected sign cell");
  Primitive two_sided_quad = quad;
  two_sided_quad.two_sided = true;
  const std::array<Primitive, 1> two_sided_quad_only{two_sided_quad};
  const auto continuous_two_sided = resolve_normal_faces_continuous(
      two_sided_quad_only, rejected_quad_projected, {base, 0}, 0u, 1u);
  require(continuous_two_sided && continuous_two_sided.faces.size() == 1 &&
              continuous_two_sided.faces[0].quad,
          "continuous two-sided quad did not bypass both sign gates");

  // Runtime-oracle comparator is ordered, content-complete, and negative-first.
  const FaceCompareResult identical = compare_ordered_faces(faces.faces, faces.faces);
  require(identical && identical.compared == 2 && identical.expected == 2 && identical.actual == 2,
          "identical face census did not report both denominators");
  auto changed_faces = faces.faces;
  changed_faces[1].fragment_ordinal = 1;
  FaceCompareResult difference = compare_ordered_faces(faces.faces, changed_faces);
  require(!difference && difference.compared == 1 && difference.mismatch_index == 1 &&
              difference.first_field == "fragment_ordinal" && difference.expected == 2 &&
              difference.actual == 2,
          "first keyed face mismatch was not named exactly");
  changed_faces = faces.faces;
  changed_faces[0].packet_attr[1] ^= 1u;
  difference = compare_ordered_faces(faces.faces, changed_faces);
  require(!difference && difference.first_field == "attr[1]",
          "face comparator did not inspect compact packet attributes");
  changed_faces = faces.faces;
  changed_faces[0].material.command ^= 0x02u;
  difference = compare_ordered_faces(faces.faces, changed_faces);
  require(!difference && difference.first_field == "semi",
          "face comparator did not distinguish opcode from semi-transparency");
  changed_faces = faces.faces;
  changed_faces[0].vertex[0].depth ^= 1u;
  changed_faces[0].ot_bin ^= 1u;
  const FaceCompareResult packet_only =
      compare_ordered_faces(faces.faces, changed_faces, {.depth = false, .ot_bin = false});
  require(packet_only && packet_only.compared == 2,
          "packet-only oracle required depth or numeric OT bins absent from guest packets");
  const std::array<ResolvedFace, 0> no_faces{};
  difference = compare_ordered_faces(faces.faces, no_faces);
  require(!difference && difference.first_field == "count" && difference.compared == 0 &&
              difference.expected == 2 && difference.actual == 0,
          "empty runtime census produced a silent or denominator-free result");
  Primitive outside = tri;
  outside.projected_offset[0] = 0x7FC;
  const std::array<Primitive, 1> outside_list{outside};
  const ResolveResult rejected = resolve_normal_faces(outside_list, projected, {base, 0}, 0, 1);
  require(!rejected && rejected.faces.empty(),
          "out-of-range projected offset returned a plausible partial face list");

  auto overlap_face = [](int x, uint32_t bin, float z, bool quad, bool semi) {
    ResolvedFace f{};
    f.quad = quad;
    f.ot_bin = bin;
    f.material.command = (quad ? 0x3c : 0x34) | (semi ? 2 : 0);
    f.vertex[0] = {int16_t(x), 0, 100, z};
    f.vertex[1] = {int16_t(x + 20), 0, 100, z};
    f.vertex[2] = {int16_t(x), 20, 100, z};
    f.vertex[3] = {int16_t(x + 20), 20, 100, z};
    return f;
  };
  std::array<ResolvedFace, 2> depth_pair{overlap_face(0, 10, 300, false, false),
                                         overlap_face(0, 20, 500, false, false)};
  auto os = analyze_overlap_depth(depth_pair);
  require(os.sampled_overlap == 1 && os.stable == 1 && os.inverted == 0,
          "stable overlapping depth pair misclassified");
  depth_pair[1] = overlap_face(0, 20, 200, false, false);
  os = analyze_overlap_depth(depth_pair);
  require(os.inverted == 1 && os.opaque_comparable == 1,
          "opaque inverted overlapping pair not detected");
  depth_pair[1] = overlap_face(0, 20, 200, false, true);
  os = analyze_overlap_depth(depth_pair);
  require(os.inverted == 0 && os.sampled_overlap == 1 && os.opaque_semi == 1,
          "semi pair incorrectly entered opaque inversion gate");
  depth_pair[1] = overlap_face(40, 20, 50, false, false);
  os = analyze_overlap_depth(depth_pair);
  require(os.sampled_overlap == 0 && os.disjoint == 1, "disjoint pair reported overlap");
  depth_pair = {overlap_face(0, 10, 100, true, false), overlap_face(0, 10, 100, true, false)};
  os = analyze_overlap_depth(depth_pair);
  require(os.sampled_overlap == 1 && os.ties == 1 && os.quad_quad == 1 && os.opaque_opaque == 1,
          "quad equal-depth tie/matrix classification failed");
  depth_pair = {overlap_face(0, 10, 300.125f, false, false),
                overlap_face(0, 20, 300.875f, false, false)};
  os = analyze_overlap_depth(depth_pair);
  require(depth_pair[0].vertex[0].depth == depth_pair[1].vertex[0].depth && os.stable == 1,
          "fractional view-Z discriminator collapsed equal saturated SZ values");
  return 0;
}
