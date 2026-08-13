// fx_paired_actor.cpp — first native ownership slice of Spyro renderer 0x80023AC4.
//
// This file owns the producer's INPUT side only: the six animation/frame selectors
// at 0x80078A70..7B are resolved through the actor table, all three compressed
// vertex layers are decoded, and the guest's per-layer A->B /16 interpolation is
// applied in model space.  It does not project, clip, decode faces, or submit a
// RenderQueue item yet.  Keeping that boundary explicit prevents a plausible but
// incomplete actor from being presented as a finished producer.
#include "fx_paired_actor.h"
#include "cfg.h"
#include "core.h"
#include "paired_actor_decode.h"
#include <lucent/log.h>
#include <array>
#include <bit>
#include <cstdint>
#include <vector>

namespace {

constexpr uint32_t kActorTable = 0x80076378u;
constexpr uint32_t kAnimState  = 0x80078A70u;
constexpr uint32_t kDeltaTable = 0x8006D614u;
constexpr int kLayers = 3;

struct Vec3i { int32_t x, y, z; };

struct StreamDesc {
  uint32_t model = 0;
  uint32_t base = 0;
  uint32_t bytes = 0;
  uint32_t shorts = 0;
  uint32_t frameWord = 0;
  uint32_t keyframe = 0;
  uint32_t count = 0;
  bool shortFirst = false;
};

struct LayerDesc {
  StreamDesc a, b;
  uint8_t blend = 0;
  bool hasB = false;
};

struct LayerPose {
  std::vector<Vec3i> vertices;
};

struct PairedPose {
  std::array<LayerPose, kLayers> layers;
};

struct GuestXyzCapture {
  std::vector<Vec3i> vertices;
  uint64_t targeted = 0;
  std::array<LayerDesc,kLayers> desc;
  PairedPose pose;
  bool decoded = false;
  uint64_t allRtps = 0;
  std::vector<uint32_t> rtpsPcs;
  bool warmup = true;
  std::vector<spyro::paired_actor::ProjectedVertex> projected;
  spyro::paired_actor::ProjectedVertex pending{};
  bool hasPending = false;
  uint32_t projectedCompared = 0;
  uint32_t projectedMismatches = 0;
  spyro::paired_actor::ProjectedVertex firstGuestProjected{};
  spyro::paired_actor::ProjectedVertex firstNativeProjected{};
  uint32_t firstProjected = 0;
};

static GuestXyzCapture sGuest;

struct DivTable { uint8_t value[0x101]; };
static constexpr DivTable make_div_table() {
  DivTable d{};
  for (uint32_t v = 0x8000; v < 0x10000; v += 0x80) {
    uint32_t x = 512;
    for (int i = 1; i < 5; ++i)
      x = (x * (1024 * 512 - ((v >> 7) * x))) >> 18;
    d.value[(v >> 7) & 0xff] = (uint8_t)(((x + 1) >> 1) - 0x101);
  }
  d.value[0x100] = d.value[0xff];
  return d;
}
static constexpr DivTable kDivTable = make_div_table();

static int32_t reciprocal(uint16_t divisor) {
  int32_t x = 0x101 + kDivTable.value[((divisor & 0x7fff) + 0x40) >> 7];
  int32_t t = (((int32_t)divisor * -x) + 0x80) >> 8;
  return ((x * (131072 + t)) + 0x80) >> 8;
}

static uint32_t divide_unr(uint16_t h, uint16_t z) {
  if ((uint32_t)z * 2 <= h) return 0x1ffff;
  unsigned shift = std::countl_zero(z);
  uint32_t dividend = (uint32_t)h << shift;
  uint32_t divisor = (uint32_t)z << shift;
  uint32_t r = (uint32_t)(((uint64_t)dividend *
      reciprocal((uint16_t)(divisor | 0x8000)) + 32768) >> 16);
  return r > 0x1ffff ? 0x1ffff : r;
}

static int64_t wrap44(int64_t v) {
  return (int64_t)((uint64_t)v << 20) >> 20;
}

static int32_t clampi(int32_t v, int32_t lo, int32_t hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

static spyro::paired_actor::ProjectedVertex project_rtps(
    uint32_t d0, uint32_t d1, const std::array<uint32_t,27>& cr) {
  const int32_t v[3] = {(int16_t)d0, (int16_t)(d0 >> 16), (int16_t)d1};
  const uint32_t c0 = cr[0], c1 = cr[1], c2 = cr[2], c3 = cr[3], c4 = cr[4];
  const int32_t m[3][3] = {
    {(int16_t)c0, (int16_t)(c0 >> 16), (int16_t)c1},
    {(int16_t)(c1 >> 16), (int16_t)c2, (int16_t)(c2 >> 16)},
    {(int16_t)c3, (int16_t)(c3 >> 16), (int16_t)c4}};
  int32_t ir[3]{};
  int64_t zUnshifted = 0;
  for (int row = 0; row < 3; ++row) {
    int64_t a = (int64_t)(int32_t)cr[5 + row] << 12;
    for (int col = 0; col < 3; ++col)
      a = wrap44(a + (int64_t)m[row][col] * v[col]);
    if (row == 2) zUnshifted = a;
    ir[row] = clampi((int32_t)(a >> 12), -32768, 32767);
  }
  const uint16_t sz = (uint16_t)clampi((int32_t)(zUnshifted >> 12), 0, 65535);
  const uint32_t ratio = divide_unr((uint16_t)cr[26], sz);
  const int32_t sx = clampi((int32_t)(((int64_t)(int32_t)cr[24] +
      (int64_t)ir[0] * ratio) >> 16), -1024, 1023);
  const int32_t sy = clampi((int32_t)(((int64_t)(int32_t)cr[25] +
      (int64_t)ir[1] * ratio) >> 16), -1024, 1023);
  return {(int16_t)sx, (int16_t)sy, sz};
}

static spyro::paired_actor::ProjectedVertex project_live_rtps() {
  std::array<uint32_t,27> cr{};
  for (uint32_t i = 0; i < cr.size(); ++i) cr[i] = gte_read_ctrl(i);
  return project_rtps(gte_read_data(0), gte_read_data(1), cr);
}

static int32_t sar32(uint32_t v, unsigned shift) {
  return (int32_t)v >> shift;
}

static int32_t wrap_add32(int32_t a, int32_t b) {
  return (int32_t)((uint32_t)a + (uint32_t)b);
}

static int32_t wrap_sub32(int32_t a, int32_t b) {
  return (int32_t)((uint32_t)a - (uint32_t)b);
}

static Vec3i unpack_accum(uint32_t packed) {
  // 0x800244DC..24520 / 0x80024700..24718.  The packed base term is
  // added with 32-bit wrap before these three signed extractions.
  return { sar32(packed, 21),
           -sar32(packed << 11, 21),
           -sar32(packed << 22, 21) };
}

static Vec3i short_delta(uint16_t word) {
  // Compact (bit15 clear) delta form at 0x800245B8..24624.
  return { sar32((uint32_t)word << 18, 25),
           -sar32((uint32_t)word << 23, 25),
           -sar32((uint32_t)word << 28, 25) };
}

static Vec3i short_absolute(uint16_t lo, uint16_t hi) {
  // Reset/absolute (bit15 set) form at 0x80024628..246A8.
  return { sar32((uint32_t)lo << 18, 21),
           -sar32(((uint32_t)hi << 12) | ((uint32_t)lo << 28), 21),
           -sar32((uint32_t)hi << 22, 21) };
}

static Vec3i add(Vec3i a, Vec3i b) {
  // The RAM-resident guest decoder is R3000 code: addu/subu accumulator
  // operations wrap modulo 2^32.  Signed C++ overflow is undefined, so spell
  // out the guest arithmetic instead of relying on the host compiler.
  return { wrap_add32(a.x, b.x), wrap_add32(a.y, b.y),
           wrap_add32(a.z, b.z) };
}

static Vec3i blend16(Vec3i a, Vec3i b, uint8_t blend) {
  // Guest loads IR0=blend*0x100 then executes INTPL (0x4A980011):
  // A + ((B-A)*IR0 >> 12), i.e. A + ((B-A)*blend >> 4).
  auto one = [blend](int32_t av, int32_t bv) {
    const int64_t delta = (int64_t)bv - (int64_t)av;
    return wrap_add32(av, (int32_t)((delta * blend) >> 4));
  };
  return { one(a.x,b.x), one(a.y,b.y), one(a.z,b.z) };
}

static Vec3i rtps_input(Vec3i v) {
  // 0x80024548/0x800247C0 do not pack Y/Z with an OR. They execute
  // `z * 0x10000 + y`, so a negative or overflowing Y carries into Z
  // before DR0 is split into its signed 16-bit halves by RTPS.
  const uint32_t yz = ((uint32_t)v.z << 16) + (uint32_t)v.y;
  return {(int16_t)v.x, (int16_t)yz, (int16_t)(yz >> 16)};
}

static uint32_t keyframe_ptr(uint32_t frameWord) {
  // Guest `(word << 11) >> 10`: a 21-bit half-address expanded to a
  // byte address in the PSX physical window.
  return (frameWord & 0x001FFFFFu) << 1;
}

static bool make_stream(Core* c, uint8_t anim, uint8_t frame, int layer,
                        StreamDesc& out) {
  const uint32_t table = c->mem_r32(kActorTable);
  out.model = c->mem_r32(table + (uint32_t)anim * 4u + 0x38u);
  if (!out.model) return false;
  const uint32_t boundary = c->mem_r8(out.model + 8u + (uint32_t)layer);
  const uint32_t previous = layer ? c->mem_r8(out.model + 7u + (uint32_t)layer) : 0u;
  if (boundary < previous) return false;
  out.count = boundary - previous;
  out.base = c->mem_r32(out.model + 0x10u);
  out.base += previous * 4u;
  out.frameWord = c->mem_r32(out.model + 0x24u + (uint32_t)frame * 4u);
  out.keyframe = keyframe_ptr(out.frameWord);
  if (!out.base || !out.keyframe) return false;
  const uint32_t w8 = c->mem_r32(out.keyframe + 8u);
  const uint32_t wc = c->mem_r32(out.keyframe + 0xCu);
  const uint32_t payload = out.keyframe + 0x18u;
  if (layer == 0) {
    // 0x80023B94 stores payload+(w8>>20) into descriptor word 10
    // (the byte stream) and payload into word 11 (the short stream).
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

static bool decode_stream(Core* c, const StreamDesc& d, std::vector<Vec3i>& out) {
  out.clear();
  out.reserve(d.count);
  uint32_t base = d.base, bp = d.bytes, sp = d.shorts;
  bool useShort = d.shortFirst;
  Vec3i accum{0,0,0};
  for (uint32_t i = 0; i < d.count; ++i, base += 4u) {
    if (!useShort) {
      const uint8_t code = c->mem_r8(bp++);
      const uint32_t packed = c->mem_r32(kDeltaTable + (uint32_t)(code & 0xFEu) * 2u)
                            + c->mem_r32(base);
      accum = add(accum, unpack_accum(packed));
      useShort = (code & 1u) != 0;
    } else {
      const uint16_t word = c->mem_r16(sp); sp += 2u;
      useShort = (word & 0x4000u) != 0;
      if ((int16_t)word < 0) {
        const uint16_t hi = c->mem_r16(sp); sp += 2u;
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

static bool build_descs(Core* c, std::array<LayerDesc,kLayers>& desc) {
  for (int layer = 0; layer < kLayers; ++layer) {
    const uint8_t animA = c->mem_r8(kAnimState + (uint32_t)layer * 2u);
    const uint8_t animB = c->mem_r8(kAnimState + (uint32_t)layer * 2u + 1u);
    const uint8_t frameA = c->mem_r8(kAnimState + 6u + (uint32_t)layer * 2u);
    const uint8_t frameB = c->mem_r8(kAnimState + 7u + (uint32_t)layer * 2u);
    desc[layer].blend = c->mem_r8(kAnimState + 12u + (uint32_t)layer);
    desc[layer].hasB = desc[layer].blend != 0;
    if (!make_stream(c, animA, frameA, layer, desc[layer].a)) return false;
    if (desc[layer].hasB) {
      if (!make_stream(c, animB, frameB, layer, desc[layer].b)) return false;
      if (desc[layer].a.count != desc[layer].b.count) return false;
    }
  }
  return true;
}

static bool decode_pose(Core* c, const std::array<LayerDesc,kLayers>& desc,
                        PairedPose& pose, std::array<uint32_t,kLayers>& decoded) {
  for (int layer = 0; layer < kLayers; ++layer) {
    std::vector<Vec3i> a, b;
    if (!decode_stream(c, desc[layer].a, a)) return false;
    if (desc[layer].hasB && !decode_stream(c, desc[layer].b, b)) return false;
    pose.layers[layer].vertices.resize(a.size());
    for (size_t i = 0; i < a.size(); ++i)
      pose.layers[layer].vertices[i] = rtps_input(desc[layer].hasB
        ? blend16(a[i], b[i], desc[layer].blend) : a[i]);
    decoded[layer] = (uint32_t)a.size();
  }
  return true;
}

static bool vertex_rtps_pc(uint32_t pc) {
  switch (pc) {
    case 0x800244E0u: case 0x80024580u:
    case 0x800246BCu: case 0x800247D8u:
    case 0x80024A14u: return true;
    default: return false;
  }
}

static void capture_guest_xyz(Core* c, uint64_t, uint32_t pc,
                              uint32_t insn, void* user) {
  auto& capture = *static_cast<GuestXyzCapture*>(user);
  if (insn != 0x4A180001u) return;
  ++capture.allRtps;
  bool novel = true;
  for (uint32_t seen : capture.rtpsPcs) novel &= seen != pc;
  if (novel) capture.rtpsPcs.push_back(pc);
  if (!vertex_rtps_pc(pc)) return;
  if (!capture.decoded) {
    std::array<uint32_t,kLayers> counts{};
    capture.decoded = build_descs(c, capture.desc) &&
                      decode_pose(c, capture.desc, capture.pose, counts);
  }
  ++capture.targeted;
  if (capture.warmup) {
    capture.warmup = false;
    return;
  }
  const uint32_t vxy = gte_read_data(0);
  const uint32_t vz = gte_read_data(1);
  capture.vertices.push_back({(int16_t)vz, (int16_t)vxy,
                              (int16_t)(vxy >> 16)});
  capture.pending = project_live_rtps();
  capture.hasPending = true;
  if (pc == 0x80024A14u) {
    capture.warmup = true;
  }
}

static void capture_guest_projection(Core*, uint64_t, uint32_t pc,
                                     uint32_t insn, void* user) {
  auto& capture = *static_cast<GuestXyzCapture*>(user);
  if (insn != 0x4A180001u || !vertex_rtps_pc(pc) || !capture.hasPending)
    return;
  const uint32_t sxy = gte_read_data(14);
  const spyro::paired_actor::ProjectedVertex g{
    (int16_t)sxy, (int16_t)(sxy >> 16), (uint16_t)gte_read_data(19)};
  const spyro::paired_actor::ProjectedVertex n = capture.pending;
  capture.projected.push_back(n);
  ++capture.projectedCompared;
  if (g.x != n.x || g.y != n.y || g.depth != n.depth) {
    if (capture.projectedMismatches == 0) {
      capture.firstProjected = capture.projectedCompared - 1;
      capture.firstGuestProjected = g;
      capture.firstNativeProjected = n;
    }
    ++capture.projectedMismatches;
  }
  capture.hasPending = false;
}

static bool compare_actual_guest(Core* c, GuestXyzCapture& guest) {
  const uint64_t armed = gte_preop_observer_disarm(c);
  if (guest.targeted == 0 && !guest.decoded) {
    lucent::debug("pairedpose",
                  "actual guest XYZ: armed_ops={} all_rtps={} target_rtps=0 "
                  "expected=UNKNOWN compared=0 (0x80023AC4 not reached; novel_rtps_pcs={})",
                  armed, guest.allRtps, guest.rtpsPcs.size());
    for (uint32_t pc : guest.rtpsPcs)
      lucent::debug("pairedpose", "actual guest XYZ: observed RTPS pc=0x{:08X}", pc);
    return true;
  }
  const uint32_t expectedVertices = guest.decoded
    ? guest.desc[0].a.count + guest.desc[1].a.count + guest.desc[2].a.count : 0;
  const uint32_t expectedRtps = expectedVertices + kLayers;
  uint32_t compared = 0, mismatches = 0;
  int firstLayer = -1;
  uint32_t firstVertex = 0;
  Vec3i firstGuest{}, firstNative{};
  if (guest.targeted == expectedRtps && guest.vertices.size() == expectedVertices) {
    size_t at = 0;
    for (int layer = 0; layer < kLayers; ++layer) {
      for (uint32_t vertex = 0; vertex < guest.desc[layer].a.count; ++vertex, ++at) {
        const Vec3i g = guest.vertices[at];
        const Vec3i n = guest.pose.layers[layer].vertices[vertex];
        ++compared;
        if (g.x != n.x || g.y != n.y || g.z != n.z) {
          if (firstLayer < 0) {
            firstLayer = layer; firstVertex = vertex;
            firstGuest = g; firstNative = n;
          }
          ++mismatches;
        }
      }
    }
  }
  if (!guest.decoded || guest.targeted != expectedRtps ||
      guest.vertices.size() != expectedVertices || mismatches) {
    lucent::error("pairedpose",
                  "actual guest XYZ: armed_ops={} all_rtps={} target_rtps={}/{} compared={}/{} "
                  "mismatches={} first=layer{} vertex{} guest=({},{},{}) native=({},{},{})",
                  armed, guest.allRtps, guest.targeted, expectedRtps, compared, expectedVertices, mismatches,
                  firstLayer, firstVertex, firstGuest.x, firstGuest.y, firstGuest.z,
                  firstNative.x, firstNative.y, firstNative.z);
    return false;
  }
  if (guest.hasPending || guest.projectedCompared != expectedVertices ||
      guest.projectedMismatches) {
    lucent::error("pairedpose",
                  "actual guest projection: compared={}/{} pending={} mismatches={} "
                  "first={} guest=({},{},{}) native=({},{},{})",
                  guest.projectedCompared, expectedVertices, guest.hasPending,
                  guest.projectedMismatches, guest.firstProjected,
                  guest.firstGuestProjected.x, guest.firstGuestProjected.y,
                  guest.firstGuestProjected.depth, guest.firstNativeProjected.x,
                  guest.firstNativeProjected.y, guest.firstNativeProjected.depth);
    return false;
  }
  lucent::info("pairedpose",
               "actual guest XYZ+projection: armed_ops={} all_rtps={} target_rtps={}/{} "
               "xyz={}/{} projected={}/{} mismatches=0",
               armed, guest.allRtps, guest.targeted, expectedRtps, compared,
               expectedVertices, guest.projectedCompared, expectedVertices);
  return true;
}

} // namespace

bool spyro_paired_actor_decode_pose(Core* c) {
  std::array<LayerDesc,kLayers> desc;
  PairedPose pose;
  std::array<uint32_t,kLayers> decoded{};
  const bool descriptors = build_descs(c, desc);
  const bool ok = descriptors && decode_pose(c, desc, pose, decoded);
  // Negative-first diagnostic: every invocation says how many layers were
  // scanned and gives each denominator, even when construction fails before a
  // vertex can be decoded.  Silence can therefore never mean "three empty layers".
  lucent::debug("pairedpose",
                "0x80023AC4 pose: scanned_layers=3 valid_descriptors={} "
                "layer0={}/{} layer1={}/{} layer2={}/{} emitted_faces=0",
                descriptors ? 3 : 0,
                decoded[0], descriptors ? desc[0].a.count : 0,
                decoded[1], descriptors ? desc[1].a.count : 0,
                decoded[2], descriptors ? desc[2].a.count : 0);
  return ok;
}

void spyro_paired_actor_oracle_arm(Core* c) {
  sGuest = {};
  gte_op_observer_arm(c, capture_guest_xyz, capture_guest_projection, &sGuest);
}

bool spyro_paired_actor_oracle_finish(Core* c) {
  return compare_actual_guest(c, sGuest);
}

int spyro_paired_actor_selftest() {
  int checks = 0;
  auto expect = [&](bool pass, const char* what) {
    ++checks;
    if (!pass) lucent::error("selftest", "FAIL(pairedpose): {}", what);
    return pass;
  };
  bool ok = true;
  ok &= expect(unpack_accum(0x00200801u).x == 1, "packed X extraction");
  std::array<uint32_t,27> identity{};
  identity[0] = 4096; identity[2] = 4096; identity[4] = 4096;
  identity[7] = 1000;
  identity[24] = 256u << 16; identity[25] = 120u << 16; identity[26] = 341;
  const auto center = project_rtps(0, 0, identity);
  ok &= expect(center.x == 256 && center.y == 120 && center.depth == 1000,
               "identity projection center and view depth");
  identity[7] = 170;
  const auto near = project_rtps(1, 0, identity);
  ok &= expect(near.x == 257 && near.y == 120 && near.depth == 170,
               "near-plane saturated UNR projection");
  ok &= expect(blend16({0,0,0}, {16,-16,32}, 8).x == 8,
               "half-frame positive blend");
  const Vec3i half = blend16({0,0,0}, {16,-16,32}, 8);
  ok &= expect(half.y == -8 && half.z == 16, "half-frame signed blend");
  ok &= expect(blend16({7,-9,11}, {99,99,99}, 0).x == 7,
               "zero blend preserves A");
  ok &= expect(keyframe_ptr(0xFFEF354Au) == 0x001E6A94u,
               "live frame-word pointer expansion");
  const Vec3i borrowed = rtps_input({11, -135, 128});
  ok &= expect(borrowed.x == 11 && borrowed.y == -135 && borrowed.z == 127,
               "RTPS DR0 addition carries negative Y into Z");
  // Layer zero's descriptor direction is fixed by 0x80023B94: the byte
  // stream starts after the short stream's payload prefix.
  constexpr uint32_t payload = 0x001E6608u, packedW8 = 0x05400000u;
  ok &= expect(payload + (packedW8 >> 20) == 0x001E665Cu,
               "layer-zero byte stream follows short stream payload");
  if (ok) lucent::info("selftest", "PASS(pairedpose): {} checks", checks);
  return ok ? 0 : 1;
}
