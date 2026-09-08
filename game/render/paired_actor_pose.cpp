#include "paired_actor_pose.h"
#include "paired_actor_depth.h"

#include "actor_model_codec.h"
#include "core.h"
#include "proj_params.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace spyro::paired_actor {
namespace {

constexpr uint32_t kActorTable = 0x80076378u;
constexpr uint32_t kAnimState = 0x80078A70u;
constexpr uint32_t kDeltaTable = 0x8006D614u;

int64_t wrap44(int64_t v) {
  return (int64_t)((uint64_t)v << 20) >> 20;
}

int32_t clampi(int32_t v, int32_t lo, int32_t hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

struct Mat3i {
  int32_t v[3][3]{};
};

Mat3i unpack_matrix(const std::array<uint32_t, 5> &p) {
  return {{{(int16_t)p[0], (int16_t)(p[0] >> 16), (int16_t)p[1]},
           {(int16_t)(p[1] >> 16), (int16_t)p[2], (int16_t)(p[2] >> 16)},
           {(int16_t)p[3], (int16_t)(p[3] >> 16), (int16_t)p[4]}}};
}

std::array<uint32_t, 5> pack_matrix(const Mat3i &m) {
  auto hw = [](int32_t x) {
    return (uint32_t)(uint16_t)x;
  };
  return {hw(m.v[0][0]) | (hw(m.v[0][1]) << 16),
          hw(m.v[0][2]) | (hw(m.v[1][0]) << 16),
          hw(m.v[1][1]) | (hw(m.v[1][2]) << 16),
          hw(m.v[2][0]) | (hw(m.v[2][1]) << 16),
          (uint32_t)(int32_t)(int16_t)m.v[2][2]};
}

std::array<int32_t, 3> mvmva_r(const Mat3i &m, const std::array<int32_t, 3> &x) {
  std::array<int32_t, 3> r{};
  for (int row = 0; row < 3; ++row) {
    int64_t a = 0;
    for (int col = 0; col < 3; ++col) {
      a = wrap44(a + (int64_t)m.v[row][col] * x[col]);
    }
    r[row] = clampi((int32_t)(a >> 12), -32768, 32767);
  }
  return r;
}

std::array<int32_t, 3> mvmva_r_mac(const Mat3i &m, const std::array<int32_t, 3> &x) {
  std::array<int32_t, 3> r{};
  for (int row = 0; row < 3; ++row) {
    int64_t a = 0;
    for (int col = 0; col < 3; ++col) {
      a = wrap44(a + (int64_t)m.v[row][col] * x[col]);
    }
    r[row] = (int32_t)(a >> 12);
  }
  return r;
}

std::array<int32_t, 3>
mvmva_rt(const Mat3i &m, const std::array<int32_t, 3> &x, const std::array<int32_t, 3> &tr) {
  std::array<int32_t, 3> r{};
  for (int row = 0; row < 3; ++row) {
    int64_t a = (int64_t)tr[row] << 12;
    for (int col = 0; col < 3; ++col) {
      a = wrap44(a + (int64_t)m.v[row][col] * x[col]);
    }
    r[row] = clampi((int32_t)(a >> 12), -32768, 32767);
  }
  return r;
}

void rotate_y(Mat3i &m, int32_t si, int32_t co) {
  const auto a = mvmva_r(m, {co, 0, si}), b = mvmva_r(m, {-si, 0, co});
  for (int r = 0; r < 3; ++r) {
    m.v[r][0] = a[r];
    m.v[r][2] = b[r];
  }
}

void rotate_x(Mat3i &m, int32_t si, int32_t co) {
  const auto a = mvmva_r(m, {0, co, si}), b = mvmva_r(m, {0, -si, co});
  for (int r = 0; r < 3; ++r) {
    m.v[r][1] = a[r];
    m.v[r][2] = b[r];
  }
}

void rotate_z(Mat3i &m, int32_t si, int32_t co) {
  const auto a = mvmva_r(m, {co, si, 0}), b = mvmva_r(m, {-si, co, 0});
  for (int r = 0; r < 3; ++r) {
    m.v[r][0] = a[r];
    m.v[r][1] = b[r];
  }
}

int32_t sar32(uint32_t v, unsigned shift) {
  return (int32_t)v >> shift;
}

int32_t wrap_add32(int32_t a, int32_t b) {
  return (int32_t)((uint32_t)a + (uint32_t)b);
}

int32_t wrap_sub32(int32_t a, int32_t b) {
  return (int32_t)((uint32_t)a - (uint32_t)b);
}

Vec3i short_delta(uint16_t word) {
  return {sar32((uint32_t)word << 18, 25),
          -sar32((uint32_t)word << 23, 25),
          -sar32((uint32_t)word << 28, 25)};
}

Vec3i short_absolute(uint16_t lo, uint16_t hi) {
  return {sar32((uint32_t)lo << 18, 21),
          -sar32(((uint32_t)hi << 12) | ((uint32_t)lo << 28), 21),
          -sar32((uint32_t)hi << 22, 21)};
}

Vec3i add(Vec3i a, Vec3i b) {
  return {wrap_add32(a.x, b.x), wrap_add32(a.y, b.y), wrap_add32(a.z, b.z)};
}

} // namespace

Vec3i unpack_accum(uint32_t packed) {
  return {sar32(packed, 21), -sar32(packed << 11, 21), -sar32(packed << 22, 21)};
}

Vec3i blend16(Vec3i a, Vec3i b, uint8_t blend) {
  const spyro::actor_model_codec::BlendResult result = spyro::actor_model_codec::blendPose(
      {a.x, a.y, a.z}, {b.x, b.y, b.z}, (int16_t)((uint16_t)blend * 256u));
  return {result.mac[0], result.mac[1], result.mac[2]};
}

uint32_t keyframe_ptr(uint32_t frameWord) {
  return (frameWord & 0x001FFFFFu) << 1;
}

Vec3i rtps_input(Vec3i v) {
  const uint32_t yz = ((uint32_t)v.z << 16) + (uint32_t)v.y;
  return {(int16_t)v.x, (int16_t)yz, (int16_t)(yz >> 16)};
}

std::array<int32_t, 3> packed_root_input(const std::array<int32_t, 3> &root) {
  const uint32_t xy =
      (uint32_t)(0u - (uint32_t)root[1]) + ((uint32_t)(0u - (uint32_t)root[2]) << 16);
  return {(int16_t)xy, (int16_t)(xy >> 16), (int16_t)root[0]};
}

bool make_stream(Core *c, uint8_t anim, uint8_t frame, int layer, StreamDesc &out) {
  const uint32_t table = c->mem_r32(kActorTable);
  out.model = c->mem_r32(table + (uint32_t)anim * 4u + 0x38u);
  if (!out.model) {
    return false;
  }
  const uint32_t boundary = c->mem_r8(out.model + 8u + (uint32_t)layer);
  const uint32_t previous = layer ? c->mem_r8(out.model + 7u + (uint32_t)layer) : 0u;
  if (boundary < previous) {
    return false;
  }
  out.count = boundary - previous;
  out.base = c->mem_r32(out.model + 0x10u);
  out.base += previous * 4u;
  out.frameWord = c->mem_r32(out.model + 0x24u + (uint32_t)frame * 4u);
  out.keyframe = keyframe_ptr(out.frameWord);
  if (!out.base || !out.keyframe) {
    return false;
  }
  const uint32_t w8 = c->mem_r32(out.keyframe + 8u);
  const uint32_t wc = c->mem_r32(out.keyframe + 0xCu);
  const uint32_t payload = out.keyframe + 0x18u;
  if (layer == 0) {
    out.bytes = payload + (w8 >> 20);
    out.shorts = payload;
  } else if (layer == 1) {
    out.bytes = payload + ((w8 >> 10) & 0x3FFu);
    out.shorts = payload + (wc >> 16);
  } else {
    out.bytes = payload + (w8 & 0x3FFu);
    out.shorts = payload + (wc & 0xFFFFu);
  }
  out.shortFirst = (out.frameWord & (1u << (21 + layer))) != 0;
  return true;
}

bool decode_stream(Core *c, const StreamDesc &d, std::vector<Vec3i> &out) {
  out.clear();
  out.reserve(d.count);
  uint32_t base = d.base, bp = d.bytes, sp = d.shorts;
  bool useShort = d.shortFirst;
  Vec3i accum{0, 0, 0};
  for (uint32_t i = 0; i < d.count; ++i, base += 4u) {
    if (!useShort) {
      const uint8_t code = c->mem_r8(bp++);
      const uint32_t packed =
          c->mem_r32(kDeltaTable + (uint32_t)(code & 0xFEu) * 2u) + c->mem_r32(base);
      accum = add(accum, unpack_accum(packed));
      useShort = (code & 1u) != 0;
    } else {
      const uint16_t word = c->mem_r16(sp);
      sp += 2u;
      useShort = (word & 0x4000u) != 0;
      if ((int16_t)word < 0) {
        const uint16_t hi = c->mem_r16(sp);
        sp += 2u;
        accum = short_absolute(word, hi);
      } else {
        const uint32_t baseWord = c->mem_r32(base);
        const Vec3i sd = short_delta(word), bd = unpack_accum(baseWord);
        accum = add(accum, add(sd, bd));
      }
    }
    out.push_back(accum);
  }
  return out.size() == d.count;
}

bool build_descs(Core *c, std::array<LayerDesc, kLayers> &desc) {
  for (int layer = 0; layer < kLayers; ++layer) {
    const uint8_t animA = c->mem_r8(kAnimState + (uint32_t)layer * 2u);
    const uint8_t animB = c->mem_r8(kAnimState + (uint32_t)layer * 2u + 1u);
    const uint8_t frameA = c->mem_r8(kAnimState + 6u + (uint32_t)layer * 2u);
    const uint8_t frameB = c->mem_r8(kAnimState + 7u + (uint32_t)layer * 2u);
    desc[layer].blend = c->mem_r8(kAnimState + 12u + (uint32_t)layer);
    desc[layer].hasB = desc[layer].blend != 0;
    if (!make_stream(c, animA, frameA, layer, desc[layer].a)) {
      return false;
    }
    if (desc[layer].hasB) {
      if (!make_stream(c, animB, frameB, layer, desc[layer].b)) {
        return false;
      }
      if (desc[layer].a.count != desc[layer].b.count) {
        return false;
      }
    }
  }
  return true;
}

bool decode_pose(Core *c,
                 const std::array<LayerDesc, kLayers> &desc,
                 PairedPose &pose,
                 std::array<uint32_t, kLayers> &decoded) {
  for (int layer = 0; layer < kLayers; ++layer) {
    std::vector<Vec3i> a, b;
    if (!decode_stream(c, desc[layer].a, a)) {
      return false;
    }
    if (desc[layer].hasB && !decode_stream(c, desc[layer].b, b)) {
      return false;
    }
    pose.layers[layer].vertices.resize(a.size());
    for (size_t i = 0; i < a.size(); ++i) {
      const Vec3i resolved = desc[layer].hasB ? blend16(a[i], b[i], desc[layer].blend) : a[i];
      pose.layers[layer].vertices[i] = rtps_input(resolved);
    }
    decoded[layer] = (uint32_t)a.size();
  }
  return true;
}

bool build_transform(Core *c, SpyroPairedActorTransform &out) {
  constexpr uint32_t instance = 0x80078A58u, camera = 0x80076DD0u;
  constexpr uint32_t sinTable = 0x8006CBF8u, cosTable = 0x8006CC78u;
  if (!c->rsub.projParams.geomValid()) {
    return false;
  }
  std::array<uint32_t, 5> packed{};
  for (uint32_t i = 0; i < 5; ++i) {
    packed[i] = c->mem_r32(camera + i * 4u);
  }
  Mat3i m = unpack_matrix(packed);
  std::array<int32_t, 3> cameraPosition{};
  for (uint32_t i = 0; i < 3; ++i) {
    cameraPosition[i] = (int32_t)c->mem_r32(camera + 40u + i * 4u);
    for (uint32_t j = 0; j < 3; ++j) {
      out.sceneCamera.matrix[i][j] = (int16_t)m.v[i][j];
    }
  }
  out.sceneCamera.position = cameraPosition;
  out.sceneCamera.valid = true;
  const int32_t dx = (int32_t)(c->mem_r32(instance) - (uint32_t)cameraPosition[0]);
  const int32_t dy = (int32_t)((uint32_t)cameraPosition[1] - c->mem_r32(instance + 4u));
  const int32_t dz = (int32_t)((uint32_t)cameraPosition[2] - c->mem_r32(instance + 8u));
  const std::array<int32_t, 3> delta = {(int16_t)dy, (int16_t)dz, (int16_t)dx};
  const auto tr = mvmva_r_mac(m, delta);
  out.base_mac = tr;
  const uint32_t angles = c->mem_r32(instance + 12u);
  auto trig = [&](uint32_t byte, int32_t &si, int32_t &co) {
    const uint32_t off = (byte & 255u) * 2u;
    si = (int16_t)c->mem_r16(sinTable + off);
    co = (int16_t)c->mem_r16(cosTable + off);
  };
  int32_t si = 0, co = 0;
  if ((int32_t)angles > 0) {
    if ((angles >> 16) & 255u) {
      trig(angles >> 16, si, co);
      rotate_y(m, si, co);
    }
    if ((angles >> 8) & 255u) {
      trig(angles >> 8, si, co);
      rotate_x(m, si, co);
    }
    if (angles & 255u) {
      trig(angles, si, co);
      rotate_z(m, si, co);
    }
  }
  const auto final = pack_matrix(m);
  for (uint32_t layer = 0; layer < 3; ++layer) {
    for (uint32_t i = 0; i < 5; ++i) {
      out.layer_cr[layer][i] = final[i];
    }
  }
  for (uint32_t layer = 1; layer < 3; ++layer) {
    Mat3i child = m;
    const uint32_t childAngles = c->mem_r32(instance + 12u + layer * 4u);
    if ((int32_t)childAngles > 0) {
      if ((childAngles >> 16) & 255u) {
        trig(childAngles >> 16, si, co);
        rotate_y(child, si, co);
      }
      if ((childAngles >> 8) & 255u) {
        trig(childAngles >> 8, si, co);
        rotate_x(child, si, co);
      }
      if (childAngles & 255u) {
        trig(childAngles, si, co);
        rotate_z(child, si, co);
      }
    }
    const auto childPacked = pack_matrix(child);
    for (uint32_t i = 0; i < 5; ++i) {
      out.layer_cr[layer][i] = childPacked[i];
    }
  }
  std::array<int32_t, 3> layerTr = tr;
  for (uint32_t i = 0; i < 3; ++i) {
    out.layer_cr[0][5 + i] = (uint32_t)layerTr[i];
  }

  auto root_words = [&](uint8_t anim, uint8_t frame, uint32_t &a, uint32_t &b) -> bool {
    const uint32_t table = c->mem_r32(kActorTable);
    const uint32_t model = table ? c->mem_r32(table + (uint32_t)anim * 4u + 0x38u) : 0;
    if (!model) {
      return false;
    }
    const uint32_t fw = c->mem_r32(model + 0x24u + (uint32_t)frame * 4u);
    const uint32_t key = (fw & 0x001FFFFFu) << 1;
    if (!key) {
      return false;
    }
    a = c->mem_r32(key + 16u);
    b = c->mem_r32(key + 20u);
    return true;
  };
  uint32_t a0 = 0, a1 = 0, b0 = 0, b1 = 0;
  if (!root_words(c->mem_r8(instance + 24u), c->mem_r8(instance + 30u), a0, a1)) {
    return false;
  }
  const uint8_t blend = c->mem_r8(instance + 36u);
  if (blend && !root_words(c->mem_r8(instance + 25u), c->mem_r8(instance + 31u), b0, b1)) {
    return false;
  }
  out.root_words[0] = a0;
  out.root_words[1] = a1;
  out.root_words[2] = b0;
  out.root_words[3] = b1;
  auto unpack_root = [](uint32_t a) {
    std::array<int32_t, 3> av = {
        (int32_t)a >> 21, (int32_t)(a << 11) >> 21, (int32_t)(a << 22) >> 21};
    return av;
  };
  std::array<std::array<int32_t, 3>, 2> roots = {unpack_root(a0), unpack_root(a1)};
  if (blend) {
    const std::array<std::array<int32_t, 3>, 2> alternate = {unpack_root(b0), unpack_root(b1)};
    for (unsigned i = 0; i < roots.size(); ++i) {
      const spyro::actor_model_codec::BlendResult result =
          spyro::actor_model_codec::blendPose({roots[i][0], roots[i][1], roots[i][2]},
                                              {alternate[i][0], alternate[i][1], alternate[i][2]},
                                              (int16_t)((uint16_t)blend * 256u));
      roots[i] = result.mac;
    }
  }
  for (uint32_t layer = 1; layer < 3; ++layer) {
    const auto &rv = roots[layer - 1];
    const std::array<int32_t, 3> rootInput = packed_root_input(rv);
    out.root_input[layer - 1] = rootInput;
    layerTr = mvmva_rt(m, rootInput, tr);
    for (uint32_t i = 0; i < 3; ++i) {
      out.layer_cr[layer][5 + i] = (uint32_t)layerTr[i];
    }
  }
  out.ofx = (uint32_t)(int32_t)c->rsub.projParams.geomOfx() << 16;
  out.ofy = (uint32_t)(int32_t)c->rsub.projParams.geomOfy() << 16;
  out.h = (uint32_t)(int32_t)c->rsub.projParams.geomH();
  const uint32_t table = c->mem_r32(kActorTable);
  const uint32_t primary =
      table ? c->mem_r32(table + (uint32_t)c->mem_r8(instance + 24u) * 4u + 0x38u) : 0;
  const uint32_t secondary =
      blend && table ? c->mem_r32(table + (uint32_t)c->mem_r8(instance + 25u) * 4u + 0x38u) : 0;
  if (!primary || (blend && !secondary)) {
    return false;
  }
  const uint32_t primaryShift = c->mem_r8(primary + 11u);
  const uint32_t secondaryShift = secondary ? c->mem_r8(secondary + 11u) : 0u;
  uint32_t control = secondaryShift;
  if ((int32_t)(secondaryShift - primaryShift) < 0) {
    control = primaryShift;
  }
  out.ot_control = control;
  out.depth_bias = c->mem_r8(instance + 39u);
  const auto depth = paired_actor_depth::derive(tr[2], out.depth_bias, control);
  out.ot_shift = depth.shift;
  out.depth_origin = static_cast<uint32_t>(depth.origin);
  out.depth_near = depth.near;
  return true;
}

} // namespace spyro::paired_actor
