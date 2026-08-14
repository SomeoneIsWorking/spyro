#include "paired_actor_decode.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace spyro::paired_actor {

static float vertex_ord(float z) {
  constexpr float nearz = 170.5f, farz = 65535.f;
  if (z < nearz) {
    z = nearz;
  }
  const float ord = (1.f / z - 1.f / farz) / (1.f / nearz - 1.f / farz);
  return std::clamp(ord, 0.f, 1.f);
}
static bool face_z_at(const ResolvedFace &f, float x, float y, float &z) {
  const int nv = f.quad ? 4 : 3;
  for (int t = 0; t < (nv == 4 ? 2 : 1); ++t) {
    const int a = t, b = t + 1, c = t + 2;
    const float x0 = f.vertex[a].x, y0 = f.vertex[a].y, x1 = f.vertex[b].x, y1 = f.vertex[b].y;
    const float x2 = f.vertex[c].x, y2 = f.vertex[c].y;
    const float den = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2);
    if (den == 0) {
      continue;
    }
    const float l0 = ((y1 - y2) * (x - x2) + (x2 - x1) * (y - y2)) / den;
    const float l1 = ((y2 - y0) * (x - x2) + (x0 - x2) * (y - y2)) / den, l2 = 1 - l0 - l1;
    auto edge = [](float ax, float ay, float bx, float by, float px, float py) {
      return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
    };
    const float area = edge(x0, y0, x1, y1, x2, y2), sign = area < 0 ? -1.f : 1.f;
    auto inside = [&](float ax, float ay, float bx, float by) {
      float e = sign * edge(ax, ay, bx, by, x, y);
      float dx = sign * (bx - ax), dy = sign * (by - ay);
      return e > 0 || (e == 0 && (dy < 0 || (dy == 0 && dx > 0)));
    };
    if (!inside(x0, y0, x1, y1) || !inside(x1, y1, x2, y2) || !inside(x2, y2, x0, y0)) {
      continue;
    }
    z = l0 * vertex_ord(f.vertex[a].view_z) + l1 * vertex_ord(f.vertex[b].view_z) +
        l2 * vertex_ord(f.vertex[c].view_z);
    return true;
  }
  return false;
}

OverlapDepthStats analyze_overlap_depth(std::span<const ResolvedFace> faces) {
  OverlapDepthStats s;
  for (size_t i = 0; i < faces.size(); ++i) {
    for (size_t j = i + 1; j < faces.size(); ++j) {
      ++s.pairs;
      const auto &A = faces[i];
      const auto &B = faces[j];
      A.quad ? (B.quad ? ++s.quad_quad : ++s.tri_quad) : (B.quad ? ++s.tri_quad : ++s.tri_tri);
      const bool as = A.material.command & 2, bs = B.material.command & 2;
      as &&bs ? ++s.semi_semi : (as || bs ? ++s.opaque_semi : ++s.opaque_opaque);
      int ax0 = A.vertex[0].x, ax1 = ax0, ay0 = A.vertex[0].y, ay1 = ay0;
      int bx0 = B.vertex[0].x, bx1 = bx0, by0 = B.vertex[0].y, by1 = by0;
      for (int k = 1; k < (A.quad ? 4 : 3); ++k) {
        ax0 = std::min(ax0, (int)A.vertex[k].x);
        ax1 = std::max(ax1, (int)A.vertex[k].x);
        ay0 = std::min(ay0, (int)A.vertex[k].y);
        ay1 = std::max(ay1, (int)A.vertex[k].y);
      }
      for (int k = 1; k < (B.quad ? 4 : 3); ++k) {
        bx0 = std::min(bx0, (int)B.vertex[k].x);
        bx1 = std::max(bx1, (int)B.vertex[k].x);
        by0 = std::min(by0, (int)B.vertex[k].y);
        by1 = std::max(by1, (int)B.vertex[k].y);
      }
      const float x0 = std::max(ax0, bx0), x1 = std::min(ax1, bx1), y0 = std::max(ay0, by0),
                  y1 = std::min(ay1, by1);
      if (x0 >= x1 || y0 >= y1) {
        ++s.disjoint;
        continue;
      }
      ++s.bbox_overlap;
      bool sampled = false, inv = false, tie = false;
      int invx = 0, invy = 0;
      float invNear = 0, invFar = 0, maxGap = 0;
      for (int yy = (int)std::floor(y0); yy < (int)std::ceil(y1); ++yy) {
        for (int xx = (int)std::floor(x0); xx < (int)std::ceil(x1); ++xx) {
          float za, zb;
          if (!face_z_at(A, xx + .5f, yy + .5f, za) || !face_z_at(B, xx + .5f, yy + .5f, zb)) {
            continue;
          }
          sampled = true;
          ++s.covered_pixels;
          if (za == zb) {
            tie = true;
          } else {
            const bool aNear = A.ot_bin < B.ot_bin;
            const float nearOrd = aNear ? za : zb, farOrd = aNear ? zb : za;
            if (farOrd > nearOrd) {
              inv = true;
              const float gap = farOrd - nearOrd;
              if (gap > maxGap) {
                maxGap = gap;
                invx = xx;
                invy = yy;
                invNear = nearOrd;
                invFar = farOrd;
              }
            }
          }
        }
      }
      if (!sampled) {
        ++s.disjoint;
        continue;
      }
      ++s.sampled_overlap;
      if (as || bs) {
        continue;
      }
      ++s.opaque_comparable;
      if (inv) {
        ++s.inverted;
        const uint32_t delta = A.ot_bin > B.ot_bin ? A.ot_bin - B.ot_bin : B.ot_bin - A.ot_bin;
        if (delta) {
          s.inverted_diff_bin++;
        } else {
          s.inverted_same_bin++;
        }
        s.max_inverted_bin_delta = std::max(s.max_inverted_bin_delta, delta);
        s.max_required_bias = std::max(s.max_required_bias, maxGap);
        if (s.first_inverted_a == UINT32_MAX) {
          s.first_inverted_a = i;
          s.first_inverted_b = j;
          s.first_x = invx;
          s.first_y = invy;
          s.first_game_near_ord = invNear;
          s.first_game_far_ord = invFar;
        }
      } else if (tie) {
        ++s.ties;
      } else {
        ++s.stable;
      }
    }
  }
  return s;
}
namespace {

uint16_t narrow_offset(uint32_t value) {
  return (uint16_t)value;
}

// GTE NCLIP's MAC0 result, including the machine's 32-bit wrapping.  Writing the expression with
// unsigned intermediates avoids making host signed overflow part of the oracle.
int32_t nclip(const ProjectedVertex &a, const ProjectedVertex &b, const ProjectedVertex &c) {
  const uint32_t ax = (uint32_t)(int32_t)a.x, ay = (uint32_t)(int32_t)a.y;
  const uint32_t bx = (uint32_t)(int32_t)b.x, by = (uint32_t)(int32_t)b.y;
  const uint32_t cx = (uint32_t)(int32_t)c.x, cy = (uint32_t)(int32_t)c.y;
  return (int32_t)(ax * by + bx * cy + cx * ay - ax * cy - bx * ay - cx * by);
}

double
nclip_continuous(const ProjectedVertex &a, const ProjectedVertex &b, const ProjectedVertex &c) {
  return (double)a.screen_x * b.screen_y + (double)b.screen_x * c.screen_y +
         (double)c.screen_x * a.screen_y - (double)a.screen_x * c.screen_y -
         (double)b.screen_x * a.screen_y - (double)c.screen_x * b.screen_y;
}

} // namespace

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
    p.two_sided = (w0 & 1u) != 0;
    p.semi_transparent = (w1 & 1u) != 0;
    p.ot_adjust = (int8_t)((int32_t)w1 >> 28);
    p.projected_offset[0] = narrow_offset((w0 >> 20) & 0x7FCu);
    p.projected_offset[1] = narrow_offset((w0 >> 11) & 0x7FCu);
    p.projected_offset[2] = narrow_offset((w0 >> 2) & 0x7FCu);
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

bool resolve_material(const Primitive &primitive,
                      const MaterialTables &tables,
                      ResolvedMaterial &out,
                      std::string &error) {
  if ((tables.override_control >> 24) != 0) {
    error = "normal material resolver called while alternate override parser is active";
    return false;
  }
  const std::span<const uint32_t> selected = tables.base;
  if (selected.empty()) {
    error = "base material color table is missing";
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
  out.command =
      (uint8_t)((primitive.quad ? 0x3Cu : 0x34u) + (primitive.semi_transparent ? 0x02u : 0u));
  error.clear();
  return true;
}

bool compute_ot_bin(const Primitive &primitive,
                    const uint32_t vertex_depth[4],
                    uint32_t depth_origin,
                    uint8_t shift,
                    uint32_t &raw,
                    uint32_t &bin) {
  // R3000 variable shifts use only the low five bits. `shift` is CR29+4 in the guest and retaining
  // this mask matters even for malformed/control-state inputs: rejecting >=32 is not what hardware
  // does.
  shift &= 31u;
  // Unsigned operations deliberately retain the R3000's 32-bit wrapping. The final gate is signed.
  const uint32_t weighted =
      primitive.quad ? vertex_depth[0] + vertex_depth[1] + vertex_depth[2] + vertex_depth[3]
                     : vertex_depth[0] + (vertex_depth[0] >> 1) + vertex_depth[1] +
                           (vertex_depth[1] >> 1) + vertex_depth[2];
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
  std::stable_sort(
      result.begin(), result.end(), [](const OrderedPrimitive &a, const OrderedPrimitive &b) {
        return a.ot_bin > b.ot_bin;
      });
  return result;
}

FaceCompareResult compare_ordered_faces(std::span<const ResolvedFace> expected,
                                        std::span<const ResolvedFace> actual,
                                        FaceCompareOptions options) {
  FaceCompareResult out;
  out.expected = (uint32_t)expected.size();
  out.actual = (uint32_t)actual.size();
  const size_t common = std::min(expected.size(), actual.size());
  auto mismatch = [&](size_t index, const char *field) {
    out.compared = (uint32_t)index;
    out.mismatch_index = (uint32_t)index;
    out.first_field = field;
    return out;
  };
  for (size_t i = 0; i < common; ++i) {
    const ResolvedFace &e = expected[i];
    const ResolvedFace &a = actual[i];
    if (e.source_ordinal != a.source_ordinal) {
      return mismatch(i, "source_ordinal");
    }
    if (e.fragment_ordinal != a.fragment_ordinal) {
      return mismatch(i, "fragment_ordinal");
    }
    if (e.quad != a.quad) {
      return mismatch(i, "nv");
    }
    const int nv = e.quad ? 4 : 3;
    for (int v = 0; v < nv; ++v) {
      if (e.vertex[v].x != a.vertex[v].x) {
        return mismatch(i,
                        v == 0   ? "vertex[0].x"
                        : v == 1 ? "vertex[1].x"
                        : v == 2 ? "vertex[2].x"
                                 : "vertex[3].x");
      }
      if (e.vertex[v].y != a.vertex[v].y) {
        return mismatch(i,
                        v == 0   ? "vertex[0].y"
                        : v == 1 ? "vertex[1].y"
                        : v == 2 ? "vertex[2].y"
                                 : "vertex[3].y");
      }
      if (options.depth && e.vertex[v].depth != a.vertex[v].depth) {
        return mismatch(i,
                        v == 0   ? "vertex[0].depth"
                        : v == 1 ? "vertex[1].depth"
                        : v == 2 ? "vertex[2].depth"
                                 : "vertex[3].depth");
      }
      if (e.material.rgb[v] != a.material.rgb[v]) {
        return mismatch(i, v == 0 ? "rgb[0]" : v == 1 ? "rgb[1]" : v == 2 ? "rgb[2]" : "rgb[3]");
      }
      if (e.packet_attr[v] != a.packet_attr[v]) {
        return mismatch(i,
                        v == 0   ? "attr[0]"
                        : v == 1 ? "attr[1]"
                        : v == 2 ? "attr[2]"
                                 : "attr[3]");
      }
    }
    if ((e.material.command & ~0x02u) != (a.material.command & ~0x02u)) {
      return mismatch(i, "opcode");
    }
    if ((e.material.command & 0x02u) != (a.material.command & 0x02u)) {
      return mismatch(i, "semi");
    }
    if (options.ot_bin && e.ot_bin != a.ot_bin) {
      return mismatch(i, "ot_bin");
    }
  }
  out.compared = (uint32_t)common;
  if (expected.size() != actual.size()) {
    out.mismatch_index = (uint32_t)common;
    out.first_field = "count";
  }
  return out;
}

static ResolveResult resolve_normal_faces_impl(std::span<const Primitive> primitives,
                                               std::span<const ProjectedVertex> projected,
                                               const MaterialTables &materials,
                                               uint32_t depth_origin,
                                               uint8_t shift,
                                               bool continuous) {
  ResolveResult result;
  result.candidates = (uint32_t)primitives.size();
  for (const Primitive &primitive : primitives) {
    ResolvedFace face;
    face.source_ordinal = primitive.source_ordinal;
    face.quad = primitive.quad;
    const int count = primitive.quad ? 4 : 3;
    uint32_t depth[4]{};
    for (int i = 0; i < count; ++i) {
      const uint32_t offset = primitive.projected_offset[i];
      if ((offset & 3u) != 0u || offset / 4u >= projected.size()) {
        result.error = "primitive projected offset is outside the resolved vertex table";
        result.faces.clear();
        return result;
      }
      face.vertex[i] = projected[offset / 4u];
      depth[i] = face.vertex[i].depth;
      face.packet_attr[i] = primitive.packet_attr[i];
    }
    if (!resolve_material(primitive, materials, face.material, result.error)) {
      result.faces.clear();
      return result;
    }
    // Normal stream, 0x80024CEC..0x80024F3C. Bit zero of word 0 is the guest's two-sided bypass.
    // For quads the second NCLIP is (d,b,c), because SXY0 is replaced with d before the operation.
    // Their signs select full GT4, either diagonal's GT3, or rejection.
    const bool two_sided = primitive.two_sided;
    Primitive emitted = primitive;
    if (!two_sided) {
      const double first = continuous
                               ? nclip_continuous(face.vertex[0], face.vertex[1], face.vertex[2])
                               : (double)nclip(face.vertex[0], face.vertex[1], face.vertex[2]);
      if (!primitive.quad) {
        if (first <= 0) {
          continue;
        }
      } else {
        const double second = continuous
                                  ? nclip_continuous(face.vertex[3], face.vertex[1], face.vertex[2])
                                  : (double)nclip(face.vertex[3], face.vertex[1], face.vertex[2]);
        if (first >= 0 && second <= 0) {
          continue;
        }
        if (second <= 0 || first >= 0) {
          emitted.quad = false;
          face.quad = false;
          face.material.command = (uint8_t)(0x34u + (primitive.semi_transparent ? 0x02u : 0u));
          if (first >= 0) {
            face.vertex[0] = face.vertex[3];
            depth[0] = depth[3];
            face.material.rgb[0] = face.material.rgb[3];
            face.packet_attr[0] = (face.packet_attr[0] & 0xFFFF0000u) | (face.packet_attr[2] >> 16);
          }
        }
      }
    }
    if (continuous) {
      double z[4]{};
      for (int i = 0; i < count; ++i) {
        z[i] = std::clamp((double)face.vertex[i].raw_view_z, 0.0, 65535.0);
      }
      const double weighted =
          emitted.quad ? z[0] + z[1] + z[2] + z[3] : z[0] * 1.5 + z[1] * 1.5 + z[2];
      const double scale = std::ldexp(1.0, shift & 31u);
      const double raw =
          weighted - (double)depth_origin * 4.0 + (double)emitted.ot_adjust * 4.0 * scale;
      if (!std::isfinite(raw) || raw <= 0.0) {
        continue;
      }
      face.continuous_ot_key = raw / scale;
      face.ot_raw = (uint32_t)std::clamp(raw, 0.0, (double)UINT32_MAX);
      face.ot_bin = (uint32_t)std::clamp(face.continuous_ot_key, 0.0, (double)UINT32_MAX);
    } else if (!compute_ot_bin(emitted, depth, depth_origin, shift, face.ot_raw, face.ot_bin)) {
      continue;
    }
    face.quad ? ++result.quads : ++result.triangles;
    result.faces.push_back(face);
  }
  std::stable_sort(result.faces.begin(),
                   result.faces.end(),
                   [continuous](const ResolvedFace &a, const ResolvedFace &b) {
                     return continuous ? a.continuous_ot_key > b.continuous_ot_key
                                       : a.ot_bin > b.ot_bin;
                   });
  return result;
}

ResolveResult resolve_normal_faces(std::span<const Primitive> primitives,
                                   std::span<const ProjectedVertex> projected,
                                   const MaterialTables &materials,
                                   uint32_t depth_origin,
                                   uint8_t shift) {
  return resolve_normal_faces_impl(primitives, projected, materials, depth_origin, shift, false);
}

ResolveResult resolve_normal_faces_continuous(std::span<const Primitive> primitives,
                                              std::span<const ProjectedVertex> projected,
                                              const MaterialTables &materials,
                                              uint32_t depth_origin,
                                              uint8_t shift) {
  return resolve_normal_faces_impl(primitives, projected, materials, depth_origin, shift, true);
}

} // namespace spyro::paired_actor
