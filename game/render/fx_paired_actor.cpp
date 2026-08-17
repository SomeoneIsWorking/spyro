// fx_paired_actor.cpp — first native ownership slice of Spyro renderer 0x80023AC4.
//
// This file owns the normal opaque/textured arm of 0x80023AC4 end-to-end: animation inputs,
// three layer transforms, fixed-point projection, face/material acceptance and authored-order
// PainterObject submission.  The alternate/status-plane arm remains a loud refusal.
#include "fx_paired_actor.h"
#include "actor_model_codec.h"
#include "cfg.h"
#include "core.h"
#include "frame_env.h"
#include "game.h"
#include "gpu_vk.h"
#include "native_projection.h"
#include "paired_actor_decode.h"
#include "producer_scope.h"
#include "proj_params.h"
#include "proj_vtx.h"
#include "render_queue.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <lucent/log.h>
#include <vector>

void proj_native_xform(int vx, int vy, int vz, ProjVtx *out);

namespace {

constexpr uint32_t kActorTable = 0x80076378u;
constexpr uint32_t kAnimState = 0x80078A70u;
constexpr uint32_t kDeltaTable = 0x8006D614u;
// The guest address this producer transcribes (r_pete — Spyro's model) — the key its producer-DB
// row is charged to. A MEASURED constant, compared by code against the guest image by
// tools/verify_producers.py.
constexpr uint32_t kProducerKey = 0x80023AC4u;
constexpr int kLayers = 3;

struct Vec3i {
  int32_t x, y, z;
};

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
  std::array<LayerDesc, kLayers> desc;
  PairedPose pose;
  bool decoded = false;
  uint64_t allRtps = 0;
  std::vector<uint32_t> rtpsPcs;
  bool warmup = true;
  std::vector<spyro::paired_actor::ProjectedVertex> projected;
  spyro::paired_actor::ProjectedVertex pending{};
  ProjVtx pendingFramework{};
  bool hasPending = false;
  uint32_t projectedCompared = 0;
  uint32_t projectedMismatches = 0;
  uint32_t floatCompared = 0;
  uint32_t floatMismatches = 0;
  uint32_t floatSubpixelDiff = 0;
  uint32_t frameworkFloatCompared = 0;
  uint32_t frameworkFloatMismatches = 0;
  const char *firstFrameworkFloatField = nullptr;
  float floatMaxError = 0;
  spyro::paired_actor::ProjectedVertex firstGuestProjected{};
  spyro::paired_actor::ProjectedVertex firstNativeProjected{};
  uint32_t firstProjected = 0;
  spyro::paired_actor::ResolveResult faces;
  bool facesResolved = false;
  uint32_t guestCandidates = 0;
  uint32_t guestNclipOps = 0;
  uint32_t guestFront = 0;
  uint32_t guestBackOrZero = 0;
  uint32_t malformedNclip = 0;
  struct PacketObservation {
    uint32_t sourceOrdinal = 0;
    uint32_t fragment = 0;
    uint32_t address = 0;
    uint32_t otLink = 0;
    uint8_t command = 0;
  };
  std::vector<PacketObservation> guestPackets;
  bool packetCandidateActive = false;
  uint32_t packetCandidateOrdinal = 0;
  uint32_t packetCandidateStart = 0;
  uint32_t packetSourcesResolved = 0;
  uint32_t packetSourcesWithOutput = 0;
  uint32_t packetBytesUnparsed = 0;
  uint32_t packetGt3 = 0;
  uint32_t packetGt4 = 0;
  uint32_t malformedOtTags = 0;
  uint32_t faceKeyMatches = 0;
  uint32_t faceKeyMismatches = 0;
  uint32_t firstFaceKeyMismatch = UINT32_MAX;
  spyro::paired_actor::FaceCompareResult packetContentCompare;
  struct OtRecord {
    uint32_t address = 0, bin = 0;
  };
  std::vector<OtRecord> otRecords;
  uint32_t pcSeen = 0, pcMatched = 0, preSnapshots = 0, postSnapshots = 0;
  uint32_t binsScanned = 0, postWithoutPre = 0;
  uint32_t nonemptyBins = 0, traversedPackets = 0, unmappedPackets = 0;
  uint32_t duplicatePackets = 0, cycles = 0, badTails = 0, unclearedWords = 0;
  uint32_t otCompared = 0, otMismatches = 0, firstOtMismatch = UINT32_MAX;
  struct GlobalWord {
    uint32_t address = 0, expected = 0;
  };
  std::vector<GlobalWord> globalExpected;
  uint32_t globalSlots = 0, globalWordsCompared = 0, globalMismatches = 0;
  uint32_t firstGlobalAddress = 0, globalSimulationErrors = 0;
  bool corruptGlobalRejected = false;
  spyro::paired_actor::OverlapDepthStats overlap;
  uint32_t ditherBit9 = 0;
  SpyroPairedActorTransform nativeTransform{};
  bool transformBuilt = false;
  uint32_t transformSnapshots = 0, transformCompared = 0, transformMismatches = 0;
  uint32_t firstTransformPc = 0, firstTransformReg = UINT32_MAX;
  uint32_t firstTransformGuest = 0, firstTransformNative = 0;
  uint32_t rootInputsCompared = 0, rootInputMismatches = 0;
  uint32_t transformNegativeInputs = 0, transformNegativeCr0Diff = 0, transformNegativeHDiff = 0;
};

static GuestXyzCapture sGuest;

static int64_t wrap44(int64_t v) {
  return (int64_t)((uint64_t)v << 20) >> 20;
}

static int32_t clampi(int32_t v, int32_t lo, int32_t hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

struct Mat3i {
  int32_t v[3][3]{};
};

static Mat3i unpack_matrix(const std::array<uint32_t, 5> &p) {
  return {{{(int16_t)p[0], (int16_t)(p[0] >> 16), (int16_t)p[1]},
           {(int16_t)(p[1] >> 16), (int16_t)p[2], (int16_t)(p[2] >> 16)},
           {(int16_t)p[3], (int16_t)(p[3] >> 16), (int16_t)p[4]}}};
}

static std::array<uint32_t, 5> pack_matrix(const Mat3i &m) {
  auto hw = [](int32_t x) {
    return (uint32_t)(uint16_t)x;
  };
  return {hw(m.v[0][0]) | (hw(m.v[0][1]) << 16),
          hw(m.v[0][2]) | (hw(m.v[1][0]) << 16),
          hw(m.v[1][1]) | (hw(m.v[1][2]) << 16),
          hw(m.v[2][0]) | (hw(m.v[2][1]) << 16),
          (uint32_t)(int32_t)(int16_t)m.v[2][2]};
}

static std::array<int32_t, 3> mvmva_r(const Mat3i &m, const std::array<int32_t, 3> &x) {
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
static std::array<int32_t, 3> mvmva_r_mac(const Mat3i &m, const std::array<int32_t, 3> &x) {
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
static std::array<int32_t, 3>
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

static void rotate_y(Mat3i &m, int32_t si, int32_t co) {
  const auto a = mvmva_r(m, {co, 0, si}), b = mvmva_r(m, {-si, 0, co});
  for (int r = 0; r < 3; ++r) {
    m.v[r][0] = a[r];
    m.v[r][2] = b[r];
  }
}
static void rotate_x(Mat3i &m, int32_t si, int32_t co) {
  // 0x800242C4 packs DR0=(cos<<16), DR1=sin; 0x800242EC packs
  // DR0=(-sin<<16), DR1=cos. Vector order is therefore (0,cos,sin)/(0,-sin,cos).
  const auto a = mvmva_r(m, {0, co, si}), b = mvmva_r(m, {0, -si, co});
  for (int r = 0; r < 3; ++r) {
    m.v[r][1] = a[r];
    m.v[r][2] = b[r];
  }
}
static void rotate_z(Mat3i &m, int32_t si, int32_t co) {
  const auto a = mvmva_r(m, {co, si, 0}), b = mvmva_r(m, {-si, co, 0});
  for (int r = 0; r < 3; ++r) {
    m.v[r][0] = a[r];
    m.v[r][1] = b[r];
  }
}

static std::array<int32_t, 3> packed_root_input(const std::array<int32_t, 3> &root) {
  const uint32_t xy =
      (uint32_t)(0u - (uint32_t)root[1]) + ((uint32_t)(0u - (uint32_t)root[2]) << 16);
  return {(int16_t)xy, (int16_t)(xy >> 16), (int16_t)root[0]};
}

static bool build_transform(Core *c, SpyroPairedActorTransform &out) {
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
  // The subtractions are guest signed 32-bit operations. The following MTC2 writes target IR1..3,
  // whose architectural write rule is `(int16_t)value`; model that boundary explicitly here.
  const int32_t dx = (int32_t)((uint32_t)c->mem_r32(instance + 0u) - c->mem_r32(camera + 40u));
  const int32_t dy = (int32_t)((uint32_t)c->mem_r32(camera + 44u) - c->mem_r32(instance + 4u));
  const int32_t dz = (int32_t)((uint32_t)c->mem_r32(camera + 48u) - c->mem_r32(instance + 8u));
  // 0x80023F38 writes r1=dx to DR11(IR3), r2=dy to DR9(IR1), r3=dz to DR10(IR2).
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
  // 0x800240D8/24194 independently compose child packed rotations +16/+20 from the same parent
  // matrix. Translation roots were projected before these rotations are installed.
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

  // 0x80023B00..23ED0 resolves the two packed root vectors from layer-0's selected keyframes.
  // 0x80024088/240A8 transform both as sibling offsets from the same base translation.
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
    // 0x80024074..2409C packs X/Y with `(-y) + ((-z) << 16)`, not two independent
    // halfwords.  A negative low word therefore borrows from the high word (for example
    // -4 + (194 << 16) encodes high=193).  Preserve that defined 32-bit guest arithmetic.
    const std::array<int32_t, 3> rootInput = packed_root_input(rv);
    out.root_input[layer - 1] = rootInput;
    // 0x80024088 and 0x800240A8 both run before either derived TR is installed, so each root is
    // transformed from the same base translation; they are sibling offsets, not cumulative ones.
    layerTr = mvmva_rt(m, rootInput, tr);
    // Opcode 0x4A49E012 selects CR5..7 as its translation vector.
    for (uint32_t i = 0; i < 3; ++i) {
      out.layer_cr[layer][5 + i] = (uint32_t)layerTr[i];
    }
  }
  out.ofx = (uint32_t)(int32_t)c->rsub.projParams.geomOfx() << 16;
  out.ofy = (uint32_t)(int32_t)c->rsub.projParams.geomOfy() << 16;
  out.h = (uint32_t)(int32_t)c->rsub.projParams.geomH();
  // 0x80023FA4..23FC8: the branch delay slot writes distance=512 on BOTH arms; only the
  // control selection differs (primary when secondary-primary is negative, secondary otherwise).
  // CR15 subtracts 512<<control from the unclamped MVMVA MAC3 and signed-clamps at zero.
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
  const uint32_t distance = 512u;
  out.ot_control = control;
  out.ot_shift = (uint8_t)((control + 4u) & 31u);
  const uint32_t rawOrigin = (uint32_t)tr[2] - (distance << (control & 31u));
  out.depth_origin = (int32_t)rawOrigin < 0 ? 0u : rawOrigin;
  const uint32_t rawNear = (uint32_t)((int32_t)tr[2] >> 7) - c->mem_r8(instance + 39u);
  out.depth_near = (int32_t)rawNear < 0 ? 0u : rawNear;
  return true;
}

static spyro::paired_actor::ProjectedVertex
project_rtps(uint32_t d0, uint32_t d1, const std::array<uint32_t, 27> &cr) {
  using namespace psxport::native_projection;
  const uint32_t c0 = cr[0], c1 = cr[1], c2 = cr[2], c3 = cr[3], c4 = cr[4];
  FixedAffine affine{};
  affine.m = {{{(int16_t)c0, (int16_t)(c0 >> 16), (int16_t)c1},
               {(int16_t)(c1 >> 16), (int16_t)c2, (int16_t)(c2 >> 16)},
               {(int16_t)c3, (int16_t)(c3 >> 16), (int16_t)c4}}};
  affine.t = {{(int32_t)cr[5], (int32_t)cr[6], (int32_t)cr[7]}};
  const NativeProjectedVertex p = project(affine,
                                          {(int32_t)cr[24], (int32_t)cr[25], (uint16_t)cr[26]},
                                          {(int16_t)d0, (int16_t)(d0 >> 16), (int16_t)d1});
  return {p.sx,
          p.sy,
          p.sz,
          p.pz,
          p.raw_view[2],
          p.raw_view[0],
          p.raw_view[1],
          p.px,
          p.py,
          (int16_t)p.ir[0],
          (int16_t)p.ir[1],
          (int16_t)p.ir[2]};
}

static int round_screen(float v) {
  return (int)(v < 0.0f ? v - 0.5f : v + 0.5f);
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
  return {sar32(packed, 21), -sar32(packed << 11, 21), -sar32(packed << 22, 21)};
}

static Vec3i short_delta(uint16_t word) {
  // Compact (bit15 clear) delta form at 0x800245B8..24624.
  return {sar32((uint32_t)word << 18, 25),
          -sar32((uint32_t)word << 23, 25),
          -sar32((uint32_t)word << 28, 25)};
}

static Vec3i short_absolute(uint16_t lo, uint16_t hi) {
  // Reset/absolute (bit15 set) form at 0x80024628..246A8.
  return {sar32((uint32_t)lo << 18, 21),
          -sar32(((uint32_t)hi << 12) | ((uint32_t)lo << 28), 21),
          -sar32((uint32_t)hi << 22, 21)};
}

static Vec3i add(Vec3i a, Vec3i b) {
  // The RAM-resident guest decoder is R3000 code: addu/subu accumulator
  // operations wrap modulo 2^32.  Signed C++ overflow is undefined, so spell
  // out the guest arithmetic instead of relying on the host compiler.
  return {wrap_add32(a.x, b.x), wrap_add32(a.y, b.y), wrap_add32(a.z, b.z)};
}

static Vec3i blend16(Vec3i a, Vec3i b, uint8_t blend) {
  const spyro::actor_model_codec::BlendResult result = spyro::actor_model_codec::blendPose(
      {a.x, a.y, a.z}, {b.x, b.y, b.z}, (int16_t)((uint16_t)blend * 256u));
  return {result.mac[0], result.mac[1], result.mac[2]};
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

static bool make_stream(Core *c, uint8_t anim, uint8_t frame, int layer, StreamDesc &out) {
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

static bool decode_stream(Core *c, const StreamDesc &d, std::vector<Vec3i> &out) {
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

static bool build_descs(Core *c, std::array<LayerDesc, kLayers> &desc) {
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

static bool decode_pose(Core *c,
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

static bool vertex_rtps_pc(uint32_t pc) {
  switch (pc) {
  case 0x800244E0u:
  case 0x80024580u:
  case 0x800246BCu:
  case 0x800247D8u:
  case 0x80024A14u:
    return true;
  default:
    return false;
  }
}

static void finish_packet_candidate(Core *c, GuestXyzCapture &capture, uint32_t end) {
  if (!capture.packetCandidateActive) {
    return;
  }
  ++capture.packetSourcesResolved;
  uint32_t at = capture.packetCandidateStart;
  uint32_t fragment = 0;
  while (at < end) {
    const uint8_t command = (uint8_t)(c->mem_r32(at + 4u) >> 24);
    const uint8_t kind = command & (uint8_t)~2u;
    const uint32_t size = kind == 0x34u ? 40u : (kind == 0x3Cu ? 52u : 0u);
    if (!size || size > end - at) {
      capture.packetBytesUnparsed += end - at;
      break;
    }
    const uint32_t tag = c->mem_r32(at);
    if ((tag >> 24) != size / 4u - 1u) {
      ++capture.malformedOtTags;
    }
    kind == 0x34u ? ++capture.packetGt3 : ++capture.packetGt4;
    capture.guestPackets.push_back({capture.packetCandidateOrdinal, fragment++, at, tag, command});
    at += size;
  }
  if (fragment) {
    ++capture.packetSourcesWithOutput;
  }
  capture.packetCandidateActive = false;
}

static void compare_face_keys(GuestXyzCapture &capture) {
  if (!capture.faces || capture.packetCandidateActive) {
    return;
  }
  std::vector<std::pair<uint32_t, bool>> native;
  native.reserve(capture.faces.faces.size());
  for (const auto &face : capture.faces.faces) {
    native.emplace_back(face.source_ordinal, face.quad);
  }
  std::sort(native.begin(), native.end());
  std::vector<std::pair<uint32_t, bool>> guest;
  guest.reserve(capture.guestPackets.size());
  for (const auto &packet : capture.guestPackets) {
    guest.emplace_back(packet.sourceOrdinal, (packet.command & (uint8_t)~2u) == 0x3Cu);
  }
  std::sort(guest.begin(), guest.end());
  const size_t common = std::min(native.size(), guest.size());
  for (size_t i = 0; i < common; ++i) {
    if (native[i] == guest[i]) {
      ++capture.faceKeyMatches;
    } else {
      if (capture.firstFaceKeyMismatch == UINT32_MAX) {
        capture.firstFaceKeyMismatch = (uint32_t)i;
      }
      ++capture.faceKeyMismatches;
    }
  }
  capture.faceKeyMismatches += (uint32_t)(native.size() + guest.size() - 2 * common);
}

static void compare_packet_content(Core *c, GuestXyzCapture &capture) {
  std::vector<spyro::paired_actor::ResolvedFace> guest;
  guest.reserve(capture.guestPackets.size());
  for (const auto &packet : capture.guestPackets) {
    spyro::paired_actor::ResolvedFace face;
    face.source_ordinal = packet.sourceOrdinal;
    face.fragment_ordinal = packet.fragment;
    face.quad = (packet.command & (uint8_t)~2u) == 0x3Cu;
    face.material.command = packet.command;
    const int nv = face.quad ? 4 : 3;
    for (int v = 0; v < nv; ++v) {
      const uint32_t color = c->mem_r32(packet.address + 4u + (uint32_t)v * 12u);
      const uint32_t sxy = c->mem_r32(packet.address + 8u + (uint32_t)v * 12u);
      face.material.rgb[v] = color & 0x00FFFFFFu;
      face.vertex[v].x = (int16_t)sxy;
      face.vertex[v].y = (int16_t)(sxy >> 16);
      face.packet_attr[v] = c->mem_r32(packet.address + 12u + (uint32_t)v * 12u);
    }
    guest.push_back(face);
  }
  std::vector<spyro::paired_actor::ResolvedFace> native = capture.faces.faces;
  auto key = [](const auto &face) {
    return std::pair(face.source_ordinal, face.fragment_ordinal);
  };
  std::sort(native.begin(), native.end(), [&](const auto &a, const auto &b) {
    return key(a) < key(b);
  });
  std::sort(guest.begin(), guest.end(), [&](const auto &a, const auto &b) {
    return key(a) < key(b);
  });
  capture.packetContentCompare =
      spyro::paired_actor::compare_ordered_faces(native, guest, {.depth = false, .ot_bin = false});
}

constexpr uint32_t kLocalOt = 0x8006FCF4u;
constexpr uint32_t kLocalOtBins = 288u;

static void snapshot_local_ot(Core *c, GuestXyzCapture &capture) {
  finish_packet_candidate(c, capture, c->r[24]);
  std::vector<uint32_t> seen;
  for (int bin = (int)kLocalOtBins - 1; bin >= 0; --bin) {
    ++capture.binsScanned;
    const uint32_t tail = c->mem_r32(kLocalOt + (uint32_t)bin * 8u);
    const uint32_t head = c->mem_r32(kLocalOt + (uint32_t)bin * 8u + 4u);
    if (!head && !tail) {
      continue;
    }
    ++capture.nonemptyBins;
    if (!head || !tail) {
      ++capture.badTails;
      continue;
    }
    uint32_t at = head;
    for (uint32_t steps = 0; steps <= capture.guestPackets.size(); ++steps) {
      if (std::find(seen.begin(), seen.end(), at) != seen.end()) {
        ++capture.duplicatePackets;
        ++capture.cycles;
        break;
      }
      seen.push_back(at);
      capture.otRecords.push_back({at, (uint32_t)bin});
      ++capture.traversedPackets;
      if (at == tail) {
        break;
      }
      const uint32_t next = c->mem_r32(at) & 0x00FFFFFFu;
      if (!next) {
        ++capture.badTails;
        break;
      }
      at = 0x80000000u | next;
      if (steps == capture.guestPackets.size()) {
        ++capture.cycles;
      }
    }
  }
  ++capture.preSnapshots;

  auto remember = [&](uint32_t address, uint32_t value) {
    auto found = std::find_if(
        capture.globalExpected.begin(), capture.globalExpected.end(), [&](const auto &word) {
          return word.address == address;
        });
    if (found == capture.globalExpected.end()) {
      capture.globalExpected.push_back({address, value});
    } else {
      found->expected = value;
    }
  };
  auto expected = [&](uint32_t address) {
    auto found = std::find_if(
        capture.globalExpected.begin(), capture.globalExpected.end(), [&](const auto &word) {
          return word.address == address;
        });
    return found == capture.globalExpected.end() ? c->mem_r32(address) : found->expected;
  };
  const uint32_t shift = gte_read_ctrl(13) & 31u;
  const uint32_t globalBase = c->mem_r32(0x80075820u);
  uint32_t global = (gte_read_ctrl(14) << 3) + ((32u << shift) - 8u);
  int32_t bias = (int32_t)(kLocalOt + 2048u - c->r[21]);
  uint32_t adjust = bias >= 0 ? ((uint32_t)bias << shift) : 0u;
  adjust = (adjust >> 8) << 3;
  global -= adjust;
  if ((int32_t)global < 0) {
    global = 0;
  }
  global += globalBase;
  const uint32_t bytesPerSlot = 256u >> shift;
  uint32_t local = c->r[21], localEnd = c->r[20] - 8u;
  if (!globalBase || !bytesPerSlot || local < kLocalOt || local >= kLocalOt + kLocalOtBins * 8u) {
    ++capture.globalSimulationErrors;
    return;
  }
  while (local != localEnd) {
    ++capture.globalSlots;
    const uint32_t stop = std::max(local - bytesPerSlot, localEnd);
    uint32_t globalTail = expected(global);
    uint32_t globalHead = expected(global + 4u);
    remember(global, globalTail);
    remember(global + 4u, globalHead);
    while (local != stop) {
      const uint32_t localTail = c->mem_r32(local);
      const uint32_t localHead = c->mem_r32(local + 4u);
      local -= 8u;
      if (!localHead) {
        continue;
      }
      if (!globalTail) {
        globalHead = localHead;
      } else {
        const uint32_t tag = expected(globalTail);
        remember(globalTail, (tag & 0xFF000000u) | (localHead & 0x00FFFFFFu));
      }
      globalTail = localTail;
    }
    remember(global, globalTail);
    remember(global + 4u, globalHead);
    global = global == globalBase ? globalBase + 8u : global - 8u;
  }
}

static void compare_global_snapshot(Core *c, GuestXyzCapture &capture, bool corrupt) {
  for (size_t i = 0; i < capture.globalExpected.size(); ++i) {
    const auto &word = capture.globalExpected[i];
    ++capture.globalWordsCompared;
    uint32_t expected = word.expected;
    if (corrupt && i == 0) {
      expected ^= 1u;
    }
    if (c->mem_r32(word.address) != expected) {
      if (!capture.firstGlobalAddress) {
        capture.firstGlobalAddress = word.address;
      }
      ++capture.globalMismatches;
    }
  }
}

static void validate_local_ot_cleared(Core *c, GuestXyzCapture &capture) {
  if (capture.preSnapshots == 0) {
    ++capture.postWithoutPre;
  }
  for (uint32_t i = 0; i < kLocalOtBins * 2u; ++i) {
    if (c->mem_r32(kLocalOt + i * 4u) != 0) {
      ++capture.unclearedWords;
    }
  }
  compare_global_snapshot(c, capture, false);
  GuestXyzCapture corrupted = capture;
  corrupted.globalWordsCompared = corrupted.globalMismatches = corrupted.firstGlobalAddress = 0;
  compare_global_snapshot(c, corrupted, true);
  capture.corruptGlobalRejected =
      !capture.globalExpected.empty() &&
      corrupted.globalWordsCompared == capture.globalExpected.size() &&
      corrupted.globalMismatches == 1 &&
      corrupted.firstGlobalAddress == capture.globalExpected.front().address;
  ++capture.postSnapshots;
}

struct SyntheticLink {
  uint32_t address, next;
};
static bool validate_synthetic_ot(bool corruptTail, bool corruptLink) {
  // Bin 9 drains before bin 3; bin 3 contains two FIFO-linked packets. A pre-existing global
  // packet is intentionally absent from the local bins and must not affect this local traversal.
  const SyntheticLink links[] = {
      {0x80001000u, 0}, {0x80002000u, 0x00003000u}, {0x80003000u, 0}, {0x80004000u, 0}};
  const uint32_t heads[] = {0x80001000u, 0x80002000u};
  const uint32_t tails[] = {0x80001000u, corruptTail ? 0x80004000u : 0x80003000u};
  const uint32_t expected[] = {0x80001000u, 0x80002000u, 0x80003000u};
  std::vector<uint32_t> actual;
  for (int chain = 0; chain < 2; ++chain) {
    uint32_t at = heads[chain];
    for (uint32_t steps = 0; steps < 4; ++steps) {
      actual.push_back(at);
      if (at == tails[chain]) {
        break;
      }
      auto link = std::find_if(std::begin(links), std::end(links), [&](const auto &value) {
        return value.address == at;
      });
      if (link == std::end(links)) {
        return false;
      }
      uint32_t next = link->next;
      if (corruptLink && at == 0x80002000u) {
        next = 0;
      }
      if (!next) {
        return false;
      }
      at = 0x80000000u | next;
    }
    if (actual.back() != tails[chain]) {
      return false;
    }
  }
  return actual.size() == std::size(expected) &&
         std::equal(actual.begin(), actual.end(), std::begin(expected));
}

static bool validate_synthetic_global(bool corruptGlobalLink) {
  // One pre-existing global chain ends at 0x80005000. Appending the local FIFO chain must preserve
  // its high tag byte, link its low 24 bits to the local head, and advance only the global tail.
  constexpr uint32_t oldHead = 0x80004000u, oldTail = 0x80005000u;
  constexpr uint32_t localHead = 0x80002000u, localTail = 0x80003000u;
  constexpr uint32_t oldTag = 0x09ABCDEFu;
  const uint32_t expectedTag = (oldTag & 0xFF000000u) | (localHead & 0x00FFFFFFu);
  const uint32_t observedTag = corruptGlobalLink ? oldTag : expectedTag;
  const uint32_t globalWord0 = localTail;
  const uint32_t globalWord1 = oldHead;
  return observedTag == expectedTag && globalWord0 == localTail && globalWord1 == oldHead &&
         (observedTag >> 24) == (oldTag >> 24);
}

static void capture_ot_checkpoint(Core *c, uint64_t, uint32_t pc, void *user) {
  auto &capture = *static_cast<GuestXyzCapture *>(user);
  if (pc == 0x800257A0u) {
    snapshot_local_ot(c, capture);
  } else if (pc == 0x800258B0u) {
    validate_local_ot_cleared(c, capture);
  }
}

static bool compare_numeric_ot(GuestXyzCapture &capture, bool corrupt) {
  std::vector<spyro::paired_actor::ResolvedFace> native = capture.faces.faces;
  std::stable_sort(native.begin(), native.end(), [](const auto &a, const auto &b) {
    return a.ot_bin > b.ot_bin;
  });
  const size_t common = std::min(native.size(), capture.otRecords.size());
  for (size_t i = 0; i < common; ++i) {
    auto packet = std::find_if(
        capture.guestPackets.begin(), capture.guestPackets.end(), [&](const auto &value) {
          return value.address == capture.otRecords[i].address;
        });
    if (packet == capture.guestPackets.end()) {
      ++capture.unmappedPackets;
      continue;
    }
    ++capture.otCompared;
    uint32_t expectedBin = native[i].ot_bin;
    if (corrupt && i == 0) {
      ++expectedBin;
    }
    if (packet->sourceOrdinal != native[i].source_ordinal ||
        packet->fragment != native[i].fragment_ordinal || capture.otRecords[i].bin != expectedBin) {
      if (capture.firstOtMismatch == UINT32_MAX) {
        capture.firstOtMismatch = (uint32_t)i;
      }
      ++capture.otMismatches;
    }
  }
  capture.otMismatches += (uint32_t)(native.size() + capture.otRecords.size() - 2u * common);
  return capture.otCompared == native.size() && capture.otMismatches == 0 &&
         capture.unmappedPackets == 0;
}

static void capture_guest_xyz(Core *c, uint64_t, uint32_t pc, uint32_t insn, void *user) {
  auto &capture = *static_cast<GuestXyzCapture *>(user);
  if (insn == 0x4A180001u && (pc == 0x80024088u || pc == 0x800240A8u)) {
    if (!capture.transformBuilt) {
      capture.transformBuilt = build_transform(c, capture.nativeTransform);
    }
    const uint32_t root = pc == 0x80024088u ? 0u : 1u;
    const auto &n = capture.nativeTransform.root_input[root];
    const uint32_t d0 = gte_read_data(0), d1 = gte_read_data(1);
    const int32_t g[3] = {(int16_t)d0, (int16_t)(d0 >> 16), (int16_t)d1};
    for (int i = 0; i < 3; ++i) {
      ++capture.rootInputsCompared;
      capture.rootInputMismatches += g[i] != n[i];
    }
    if (root == 1 && capture.rootInputMismatches) {
      lucent::error("pairedpose",
                    "root2 input blend={} words={:08X}/{:08X}->{:08X}/{:08X} guest=({},{},{}) "
                    "native=({},{},{}) compared={} mismatches={}",
                    c->mem_r8(0x80078A7Cu),
                    capture.nativeTransform.root_words[0],
                    capture.nativeTransform.root_words[1],
                    capture.nativeTransform.root_words[2],
                    capture.nativeTransform.root_words[3],
                    g[0],
                    g[1],
                    g[2],
                    n[0],
                    n[1],
                    n[2],
                    capture.rootInputsCompared,
                    capture.rootInputMismatches);
    }
  }
  if (insn == 0x4B400006u && (pc == 0x80024CECu || pc == 0x80024EF0u)) {
    finish_packet_candidate(c, capture, c->r[24]);
    capture.packetCandidateActive = true;
    capture.packetCandidateOrdinal = capture.guestCandidates;
    capture.packetCandidateStart = c->r[24];
  }
  if (pc == 0x80024CECu && !capture.facesResolved) {
    capture.facesResolved = true;
    if (capture.desc[0].a.model && capture.projected.size() == 238) {
      const uint32_t stream = c->mem_r32(capture.desc[0].a.model + 0x14u);
      const uint32_t colors = c->mem_r32(capture.desc[0].a.model + 0x18u);
      if (stream && colors) {
        const uint32_t bytes = c->mem_r32(stream);
        std::vector<uint32_t> words(1u + bytes / 4u);
        for (uint32_t i = 0; i < words.size(); ++i) {
          words[i] = c->mem_r32(stream + i * 4u);
        }
        const auto decoded = spyro::paired_actor::decode_normal_stream(words);
        std::vector<uint32_t> base(512);
        for (uint32_t i = 0; i < 512; ++i) {
          base[i] = c->mem_r32(colors + i * 4u);
        }
        if (decoded) {
          capture.faces =
              spyro::paired_actor::resolve_normal_faces(decoded.primitives,
                                                        capture.projected,
                                                        {base, c->mem_r32(0x80078A80u)},
                                                        gte_read_ctrl(15),
                                                        (uint8_t)(gte_read_ctrl(13) + 4u));
        } else {
          capture.faces.error = decoded.error;
        }
      } else {
        capture.faces.error = "normal stream or material table pointer is null";
      }
    } else {
      capture.faces.error = "face resolution reached before 238 projected vertices";
    }
  }
  if (insn != 0x4A180001u) {
    return;
  }
  ++capture.allRtps;
  bool novel = true;
  for (uint32_t seen : capture.rtpsPcs) {
    novel &= seen != pc;
  }
  if (novel) {
    capture.rtpsPcs.push_back(pc);
  }
  if (!vertex_rtps_pc(pc)) {
    return;
  }
  if (!capture.transformBuilt) {
    capture.transformBuilt = build_transform(c, capture.nativeTransform);
  }
  if (capture.warmup) {
    const uint32_t transformLayer = capture.transformSnapshots;
    ++capture.transformSnapshots;
    const auto &layerCr = capture.nativeTransform.layer_cr[transformLayer < 3 ? transformLayer : 2];
    const uint32_t native[14] = {layerCr[0],
                                 layerCr[1],
                                 layerCr[2],
                                 layerCr[3],
                                 layerCr[4],
                                 layerCr[5],
                                 layerCr[6],
                                 layerCr[7],
                                 capture.nativeTransform.ofx,
                                 capture.nativeTransform.ofy,
                                 capture.nativeTransform.h,
                                 capture.nativeTransform.ot_control,
                                 capture.nativeTransform.depth_near,
                                 capture.nativeTransform.depth_origin};
    const uint32_t regs[14] = {0, 1, 2, 3, 4, 5, 6, 7, 24, 25, 26, 13, 14, 15};
    const uint32_t nregs = transformLayer == 0 ? 14u : 11u;
    for (uint32_t i = 0; i < nregs; ++i) {
      ++capture.transformCompared;
      const uint32_t actual = gte_read_ctrl(regs[i]);
      if (!capture.transformBuilt || actual != native[i]) {
        if (!capture.transformMismatches) {
          capture.firstTransformPc = pc;
          capture.firstTransformReg = regs[i];
          capture.firstTransformGuest = actual;
          capture.firstTransformNative = native[i];
        }
        ++capture.transformMismatches;
      }
    }
  }
  if (!capture.decoded) {
    std::array<uint32_t, kLayers> counts{};
    capture.decoded =
        build_descs(c, capture.desc) && decode_pose(c, capture.desc, capture.pose, counts);
  }
  ++capture.targeted;
  if (capture.warmup) {
    capture.warmup = false;
    return;
  }
  const uint32_t vxy = gte_read_data(0);
  const uint32_t vz = gte_read_data(1);
  capture.vertices.push_back({(int16_t)vz, (int16_t)vxy, (int16_t)(vxy >> 16)});
  std::array<uint32_t, 27> tcr{};
  const uint32_t transformLayer = capture.transformSnapshots ? capture.transformSnapshots - 1 : 0;
  for (uint32_t i = 0; i < 8; ++i) {
    tcr[i] = capture.nativeTransform.layer_cr[transformLayer < 3 ? transformLayer : 2][i];
  }
  tcr[24] = capture.nativeTransform.ofx;
  tcr[25] = capture.nativeTransform.ofy;
  tcr[26] = capture.nativeTransform.h;
  capture.pending = project_rtps(gte_read_data(0), gte_read_data(1), tcr);
  proj_native_xform((int16_t)gte_read_data(0),
                    (int16_t)(gte_read_data(0) >> 16),
                    (int16_t)gte_read_data(1),
                    &capture.pendingFramework);
  capture.hasPending = true;
  if (pc == 0x80024A14u) {
    capture.warmup = true;
  }
}

static void capture_guest_projection(Core *, uint64_t, uint32_t pc, uint32_t insn, void *user) {
  auto &capture = *static_cast<GuestXyzCapture *>(user);
  if (insn == 0x4B400006u) {
    ++capture.guestNclipOps;
    if (pc == 0x80024CECu || pc == 0x80024EF0u) {
      ++capture.guestCandidates;
    } else if (pc != 0x80024D1Cu && pc != 0x800251A8u) {
      ++capture.malformedNclip;
    }
    const int32_t area = (int32_t)gte_read_data(24);
    area > 0 ? ++capture.guestFront : ++capture.guestBackOrZero;
    return;
  }
  if (insn != 0x4A180001u || !vertex_rtps_pc(pc) || !capture.hasPending) {
    return;
  }
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
  ++capture.floatCompared;
  const int fdx = std::abs(round_screen(n.screen_x) - g.x);
  const int fdy = std::abs(round_screen(n.screen_y) - g.y);
  capture.floatMaxError = std::max(capture.floatMaxError, (float)std::max(fdx, fdy));
  capture.floatMismatches += (fdx > 1 || fdy > 1);
  // Negative discriminator: forcing the float endpoint back to the exact guest integer SXY must
  // lose information on the live corpus while leaving the guest SXY comparison itself unchanged.
  capture.floatSubpixelDiff += (std::fabs(n.screen_x - (float)g.x) > 1.0e-4f ||
                                std::fabs(n.screen_y - (float)g.y) > 1.0e-4f);
  ++capture.frameworkFloatCompared;
  const ProjVtx &f = capture.pendingFramework;
  auto mismatch = [&](bool bad, const char *field) {
    if (!bad) {
      return;
    }
    if (!capture.frameworkFloatMismatches) {
      capture.firstFrameworkFloatField = field;
    }
    ++capture.frameworkFloatMismatches;
  };
  mismatch(n.view_x != f.ir1, "ir1");
  mismatch(n.view_y != f.ir2, "ir2");
  mismatch(n.view_ir_z != f.ir3, "ir3");
  mismatch(n.depth != f.sz, "sz");
  mismatch(n.x != f.sx, "sx");
  mismatch(n.y != f.sy, "sy");
  mismatch(std::fabs(n.screen_x - f.px) > 1.0e-5f, "px");
  mismatch(std::fabs(n.screen_y - f.py) > 1.0e-5f, "py");
  mismatch(std::fabs(n.view_z - f.pz) > 1.0e-5f, "pz");
  capture.hasPending = false;
}

static bool compare_actual_guest(Core *c, GuestXyzCapture &guest) {
  finish_packet_candidate(c, guest, c->r[24]);
  compare_face_keys(guest);
  compare_packet_content(c, guest);
  guest.pcSeen = (uint32_t)c->pcObserver.seen();
  guest.pcMatched = (uint32_t)c->pcObserver.matched();
  c->pcObserver.disarm();
  const bool otGreen = compare_numeric_ot(guest, false);
  guest.overlap = spyro::paired_actor::analyze_overlap_depth(guest.faces.faces);
  if (guest.transformBuilt) {
    std::array<uint32_t, 27> base{}, badCr{}, badH{};
    uint32_t vertexIndex = 0;
    base[24] = guest.nativeTransform.ofx;
    base[25] = guest.nativeTransform.ofy;
    base[26] = guest.nativeTransform.h;
    badCr = base;
    badH = base;
    badCr[0] ^= 0x1000u;
    badH[26] ^= 1u;
    for (const Vec3i &v : guest.vertices) {
      uint32_t layer = vertexIndex >= guest.desc[0].a.count ? 1 : 0;
      if (vertexIndex >= guest.desc[0].a.count + guest.desc[1].a.count) {
        layer = 2;
      }
      for (uint32_t i = 0; i < 8; ++i) {
        base[i] = guest.nativeTransform.layer_cr[layer][i];
      }
      base[24] = guest.nativeTransform.ofx;
      base[25] = guest.nativeTransform.ofy;
      base[26] = guest.nativeTransform.h;
      // Flip one full 1.3.12 basis unit. A one-LSB perturbation can legitimately quantize to the
      // same SXY for all vertices and is not a discriminating negative.
      badCr = base;
      badH = base;
      badCr[0] ^= 0x1000u;
      badH[26] ^= 1u;
      const uint32_t d0 = (uint16_t)v.y | ((uint32_t)(uint16_t)v.z << 16), d1 = (uint16_t)v.x;
      const auto p = project_rtps(d0, d1, base), q = project_rtps(d0, d1, badCr),
                 h = project_rtps(d0, d1, badH);
      ++guest.transformNegativeInputs;
      guest.transformNegativeCr0Diff += (p.x != q.x || p.y != q.y || p.depth != q.depth);
      guest.transformNegativeHDiff += (p.x != h.x || p.y != h.y || p.depth != h.depth);
      ++vertexIndex;
    }
  }
  for (const auto &face : guest.faces.faces) {
    if (face.packet_attr[1] & 0x02000000u) {
      ++guest.ditherBit9;
    }
  }
  GuestXyzCapture corrupt = guest;
  corrupt.otCompared = corrupt.otMismatches = corrupt.unmappedPackets = 0;
  corrupt.firstOtMismatch = UINT32_MAX;
  const bool corruptRejected = !compare_numeric_ot(corrupt, true) && corrupt.otMismatches != 0 &&
                               corrupt.firstOtMismatch == 0;
  const bool topologyDiscriminator =
      validate_synthetic_ot(false, false) && !validate_synthetic_ot(true, false) &&
      !validate_synthetic_ot(false, true) && validate_synthetic_global(false) &&
      !validate_synthetic_global(true);
  const uint64_t armed = gte_preop_observer_disarm(c);
  if (guest.targeted == 0 && !guest.decoded) {
    lucent::debug("pairedpose",
                  "actual guest XYZ: armed_ops={} all_rtps={} target_rtps=0 "
                  "expected=UNKNOWN compared=0 (0x80023AC4 not reached; novel_rtps_pcs={})",
                  armed,
                  guest.allRtps,
                  guest.rtpsPcs.size());
    for (uint32_t pc : guest.rtpsPcs) {
      lucent::debug("pairedpose", "actual guest XYZ: observed RTPS pc=0x{:08X}", pc);
    }
    return true;
  }
  const uint32_t expectedVertices =
      guest.decoded ? guest.desc[0].a.count + guest.desc[1].a.count + guest.desc[2].a.count : 0;
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
            firstLayer = layer;
            firstVertex = vertex;
            firstGuest = g;
            firstNative = n;
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
                  armed,
                  guest.allRtps,
                  guest.targeted,
                  expectedRtps,
                  compared,
                  expectedVertices,
                  mismatches,
                  firstLayer,
                  firstVertex,
                  firstGuest.x,
                  firstGuest.y,
                  firstGuest.z,
                  firstNative.x,
                  firstNative.y,
                  firstNative.z);
    return false;
  }
  if (guest.hasPending || guest.projectedCompared != expectedVertices ||
      guest.projectedMismatches) {
    lucent::error("pairedpose",
                  "actual guest projection: compared={}/{} pending={} mismatches={} "
                  "first={} guest=({},{},{}) native=({},{},{}) transform built={} layers={}/3 "
                  "regs={}/33 mismatches={} first_pc=0x{:08X} first_cr={} guest_cr=0x{:08X} "
                  "native_cr=0x{:08X}",
                  guest.projectedCompared,
                  expectedVertices,
                  guest.hasPending,
                  guest.projectedMismatches,
                  guest.firstProjected,
                  guest.firstGuestProjected.x,
                  guest.firstGuestProjected.y,
                  guest.firstGuestProjected.depth,
                  guest.firstNativeProjected.x,
                  guest.firstNativeProjected.y,
                  guest.firstNativeProjected.depth,
                  guest.transformBuilt,
                  guest.transformSnapshots,
                  guest.transformCompared,
                  guest.transformMismatches,
                  guest.firstTransformPc,
                  guest.firstTransformReg,
                  guest.firstTransformGuest,
                  guest.firstTransformNative);
    const auto &l2 = guest.nativeTransform.layer_cr[2];
    lucent::error("pairedpose",
                  "projection fail transform detail blend={} guest_last_tr={:08X},{:08X},{:08X} "
                  "native_l2_tr={:08X},{:08X},{:08X}",
                  c->mem_r8(0x80078A7Cu),
                  gte_read_ctrl(5),
                  gte_read_ctrl(6),
                  gte_read_ctrl(7),
                  l2[5],
                  l2[6],
                  l2[7]);
    return false;
  }
  lucent::info(
      "pairedpose",
      "transform snapshot oracle: built={} layers={}/3 regs={}/36 mismatches={} first_pc=0x{:08X} "
      "first_cr={} guest=0x{:08X} native=0x{:08X} negative_inputs={} perturb_cr0_diff={} "
      "perturb_h_diff={}",
      guest.transformBuilt,
      guest.transformSnapshots,
      guest.transformCompared,
      guest.transformMismatches,
      guest.firstTransformPc,
      guest.firstTransformReg,
      guest.firstTransformGuest,
      guest.firstTransformNative,
      guest.transformNegativeInputs,
      guest.transformNegativeCr0Diff,
      guest.transformNegativeHDiff);
  lucent::info(
      "pairedpose",
      "float SXY oracle: framework_vertices={}/{} framework_field_mismatches={} first_field={} "
      "guest_rounded_le1={}/{} guest_mismatches={} max_error={:.3f} "
      "integer_forced_negative_diff={}/{}",
      guest.frameworkFloatCompared,
      expectedVertices,
      guest.frameworkFloatMismatches,
      guest.firstFrameworkFloatField ? guest.firstFrameworkFloatField : "none",
      guest.floatCompared - guest.floatMismatches,
      guest.floatCompared,
      guest.floatMismatches,
      guest.floatMaxError,
      guest.floatSubpixelDiff,
      guest.floatCompared);
  const bool transformGreen =
      guest.transformBuilt && guest.transformSnapshots == 3 && guest.transformCompared == 36 &&
      !guest.transformMismatches && guest.rootInputsCompared == 6 && !guest.rootInputMismatches &&
      guest.projectedCompared == expectedVertices && !guest.projectedMismatches &&
      guest.transformNegativeInputs == expectedVertices && guest.transformNegativeCr0Diff &&
      guest.transformNegativeHDiff;
  const bool floatGreen = guest.frameworkFloatCompared == expectedVertices &&
                          !guest.frameworkFloatMismatches &&
                          guest.floatCompared == expectedVertices && !guest.floatMismatches &&
                          guest.floatSubpixelDiff > 0;
  if (cfg_str("PSXPORT_PAIRED_FLOAT_XY_ORACLE")) {
    lucent::info("pairedpose",
                 "float-only gate: framework_vertices={}/{} field_mismatches={} first_field={} "
                 "guest_rounded_le1={}/{} max_error={:.3f} integer_forced_negative={}/{} => {}",
                 guest.frameworkFloatCompared,
                 expectedVertices,
                 guest.frameworkFloatMismatches,
                 guest.firstFrameworkFloatField ? guest.firstFrameworkFloatField : "none",
                 guest.floatCompared - guest.floatMismatches,
                 expectedVertices,
                 guest.floatMaxError,
                 guest.floatSubpixelDiff,
                 guest.floatCompared,
                 floatGreen ? "PASS" : "FAIL");
    return floatGreen;
  }
  if (cfg_str("PSXPORT_PAIRED_TRANSFORM_ORACLE")) {
    lucent::info("pairedpose",
                 "transform-only gate: layers={}/3 regs={}/36 roots={}/6 vertices={}/{} "
                 "mismatches={} perturb_cr0={}/{} perturb_h={}/{} => {}",
                 guest.transformSnapshots,
                 guest.transformCompared,
                 guest.rootInputsCompared,
                 guest.projectedCompared,
                 expectedVertices,
                 guest.transformMismatches + guest.projectedMismatches,
                 guest.transformNegativeCr0Diff,
                 guest.transformNegativeInputs,
                 guest.transformNegativeHDiff,
                 guest.transformNegativeInputs,
                 transformGreen ? "PASS" : "FAIL");
    if (!transformGreen) {
      const auto &l2 = guest.nativeTransform.layer_cr[2];
      lucent::error("pairedpose",
                    "transform fail detail blend={} layer2 guest_tr={:08X},{:08X},{:08X} "
                    "native_tr={:08X},{:08X},{:08X}",
                    c->mem_r8(0x80078A7Cu),
                    gte_read_ctrl(5),
                    gte_read_ctrl(6),
                    gte_read_ctrl(7),
                    l2[5],
                    l2[6],
                    l2[7]);
    }
    return transformGreen;
  }
  lucent::info("pairedpose",
               "normal-face discriminator: decoded={} naive_accepted={} gt3={} gt4={} "
               "guest_candidates={} nclip_ops={} front={} back_or_zero={} malformed={} "
               "state-specific packet contribution is read from the source-keyed guest oracle; "
               "error={}",
               guest.faces.candidates,
               guest.faces.faces.size(),
               guest.faces.triangles,
               guest.faces.quads,
               guest.guestCandidates,
               guest.guestNclipOps,
               guest.guestFront,
               guest.guestBackOrZero,
               guest.malformedNclip,
               guest.faces.error);
  lucent::info("pairedpose",
               "guest packet/source oracle: candidates={} sources_resolved={} unresolved={} "
               "sources_with_packets={} packets_parsed={} gt3={} gt4={} bytes_unparsed={} "
               "nclip_records={} malformed_nclip={} malformed_ot_tags={} "
               "(each packet keyed source_ordinal+fragment; word0 OT-link captured)",
               guest.guestCandidates,
               guest.packetSourcesResolved,
               guest.packetCandidateActive ? 1 : 0,
               guest.packetSourcesWithOutput,
               guest.guestPackets.size(),
               guest.packetGt3,
               guest.packetGt4,
               guest.packetBytesUnparsed,
               guest.guestNclipOps,
               guest.malformedNclip,
               guest.malformedOtTags);
  lucent::info("pairedpose",
               "normal face/source compare: native={} guest={} compared={} mismatches={} "
               "first_mismatch={} (key=source_ordinal+GT3/GT4; OT drain order compared separately)",
               guest.faces.faces.size(),
               guest.guestPackets.size(),
               guest.faceKeyMatches,
               guest.faceKeyMismatches,
               guest.firstFaceKeyMismatch);
  lucent::info("pairedpose",
               "normal packet content compare: compared={}/{} actual={} mismatch_index={} "
               "first_field={} depth=UNAVAILABLE ot_bin=UNAVAILABLE order=source+fragment",
               guest.packetContentCompare.compared,
               guest.packetContentCompare.expected,
               guest.packetContentCompare.actual,
               guest.packetContentCompare.mismatch_index,
               guest.packetContentCompare.first_field);
  lucent::info(
      "pairedpose",
      "numeric OT oracle: pc_seen={} pc_matched={} pre={}/1 post={}/1 bins_scanned={}/288 "
      "nonempty_bins={} post_without_pre={} "
      "packets={}/{} compared={}/{} mismatches={} first={} unmapped={} duplicates={} "
      "cycles={} bad_tails={} uncleared_words={} global_slots={} global_words={} "
      "global_mismatches={} first_global=0x{:08X} global_errors={} corrupt_discriminator={}",
      guest.pcSeen,
      guest.pcMatched,
      guest.preSnapshots,
      guest.postSnapshots,
      guest.binsScanned,
      guest.nonemptyBins,
      guest.postWithoutPre,
      guest.traversedPackets,
      guest.guestPackets.size(),
      guest.otCompared,
      guest.faces.faces.size(),
      guest.otMismatches,
      guest.firstOtMismatch,
      guest.unmappedPackets,
      guest.duplicatePackets,
      guest.cycles,
      guest.badTails,
      guest.unclearedWords,
      guest.globalSlots,
      guest.globalWordsCompared,
      guest.globalMismatches,
      guest.firstGlobalAddress,
      guest.globalSimulationErrors,
      corruptRejected && topologyDiscriminator && guest.corruptGlobalRejected ? "REJECTED"
                                                                              : "FAILED_TO_REJECT");
  lucent::info("pairedpose",
               "overlap depth: faces={} pairs={} bbox={} overlap={} opaque_comparable={} pixels={} "
               "stable={} inverted={} ties={} "
               "disjoint={} tri_tri={} tri_quad={} quad_quad={} opaque_opaque={} opaque_semi={} "
               "semi_semi={} tpage_bit9={}/{} inv_same={} inv_diff={} max_bin_delta={} "
               "max_required_bias={:.9f} first_inverted={}:{} pixel=({},{}), ord_near={:.9f} "
               "ord_far={:.9f} painter_required={}",
               guest.faces.faces.size(),
               guest.overlap.pairs,
               guest.overlap.bbox_overlap,
               guest.overlap.sampled_overlap,
               guest.overlap.opaque_comparable,
               guest.overlap.covered_pixels,
               guest.overlap.stable,
               guest.overlap.inverted,
               guest.overlap.ties,
               guest.overlap.disjoint,
               guest.overlap.tri_tri,
               guest.overlap.tri_quad,
               guest.overlap.quad_quad,
               guest.overlap.opaque_opaque,
               guest.overlap.opaque_semi,
               guest.overlap.semi_semi,
               guest.ditherBit9,
               guest.faces.faces.size(),
               guest.overlap.inverted_same_bin,
               guest.overlap.inverted_diff_bin,
               guest.overlap.max_inverted_bin_delta,
               guest.overlap.max_required_bias,
               guest.overlap.first_inverted_a,
               guest.overlap.first_inverted_b,
               guest.overlap.first_x,
               guest.overlap.first_y,
               guest.overlap.first_game_near_ord,
               guest.overlap.first_game_far_ord,
               guest.overlap.inverted != 0);
  if (guest.overlap.first_inverted_a < guest.faces.faces.size() &&
      guest.overlap.first_inverted_b < guest.faces.faces.size()) {
    const auto &a = guest.faces.faces[guest.overlap.first_inverted_a];
    const auto &b = guest.faces.faces[guest.overlap.first_inverted_b];
    lucent::info("pairedpose",
                 "overlap witness A src={} frag={} bin={} nv={} xy/z=[({},{},{:.3f}) "
                 "({},{},{:.3f}) ({},{},{:.3f}) ({},{},{:.3f})] B src={} frag={} bin={} nv={} "
                 "xy/z=[({},{},{:.3f}) ({},{},{:.3f}) ({},{},{:.3f}) ({},{},{:.3f})]",
                 a.source_ordinal,
                 a.fragment_ordinal,
                 a.ot_bin,
                 a.quad ? 4 : 3,
                 a.vertex[0].x,
                 a.vertex[0].y,
                 a.vertex[0].view_z,
                 a.vertex[1].x,
                 a.vertex[1].y,
                 a.vertex[1].view_z,
                 a.vertex[2].x,
                 a.vertex[2].y,
                 a.vertex[2].view_z,
                 a.vertex[3].x,
                 a.vertex[3].y,
                 a.vertex[3].view_z,
                 b.source_ordinal,
                 b.fragment_ordinal,
                 b.ot_bin,
                 b.quad ? 4 : 3,
                 b.vertex[0].x,
                 b.vertex[0].y,
                 b.vertex[0].view_z,
                 b.vertex[1].x,
                 b.vertex[1].y,
                 b.vertex[1].view_z,
                 b.vertex[2].x,
                 b.vertex[2].y,
                 b.vertex[2].view_z,
                 b.vertex[3].x,
                 b.vertex[3].y,
                 b.vertex[3].view_z);
  }
  if (!guest.transformBuilt || guest.transformSnapshots != 3 || guest.transformCompared != 36 ||
      guest.transformMismatches || guest.rootInputsCompared != 6 || guest.rootInputMismatches ||
      guest.transformNegativeInputs != expectedVertices || !guest.transformNegativeCr0Diff ||
      !guest.transformNegativeHDiff || !otGreen || !corruptRejected || !topologyDiscriminator ||
      !guest.corruptGlobalRejected || guest.pcSeen != 2 || guest.pcMatched != 2 ||
      guest.preSnapshots != 1 || guest.postSnapshots != 1 ||
      guest.traversedPackets != guest.guestPackets.size() || guest.binsScanned != kLocalOtBins ||
      guest.postWithoutPre || guest.duplicatePackets || guest.cycles || guest.badTails ||
      guest.unclearedWords || guest.globalSimulationErrors || !guest.globalSlots ||
      !guest.globalWordsCompared || guest.globalMismatches || !guest.overlap.opaque_comparable) {
    return false;
  }
  lucent::info("pairedpose",
               "actual guest XYZ+projection: armed_ops={} all_rtps={} target_rtps={}/{} "
               "xyz={}/{} projected={}/{} mismatches=0",
               armed,
               guest.allRtps,
               guest.targeted,
               expectedRtps,
               compared,
               expectedVertices,
               guest.projectedCompared,
               expectedVertices);
  return true;
}

static bool refuse_shipping(SpyroPairedActorFrameState &state, const char *why) {
  state.refusal = why;
  state.current = {};
  state.endpoints_compatible = false;
  lucent::error(
      "pairedactor",
      "0x80023AC4 native refusal: invocations={} groups={} candidates={} faces={} reason={}",
      state.invocations,
      state.groups,
      state.candidates,
      state.faces,
      why);
  return false;
}

static uint64_t topology_fingerprint(const std::array<uint32_t, 3> &counts,
                                     std::span<const spyro::paired_actor::Primitive> primitives) {
  uint64_t h = 1469598103934665603ull;
  auto add = [&](uint32_t v) {
    h ^= v;
    h *= 1099511628211ull;
  };
  for (uint32_t n : counts) {
    add(n);
  }
  add((uint32_t)primitives.size());
  for (const auto &p : primitives) {
    add(p.source_ordinal);
    add(p.quad);
    add(p.two_sided);
    add(p.semi_transparent);
    add((uint8_t)p.ot_adjust);
    const uint32_t nv = p.quad ? 4u : 3u;
    for (uint32_t i = 0; i < nv; ++i) {
      add(p.projected_offset[i]);
      add(p.material_offset[i]);
      add(p.packet_attr[i]);
    }
  }
  return h;
}

static bool frames_compatible(const SpyroPairedFrame &a, const SpyroPairedFrame &b) {
  return a.valid && b.valid && !a.culled && !b.culled && a.topology == b.topology &&
         a.epoch == b.epoch && a.layer_counts == b.layer_counts &&
         a.primitives.size() == b.primitives.size() && a.materials == b.materials &&
         a.override_control == b.override_control && a.transform.ofx == b.transform.ofx &&
         a.transform.ofy == b.transform.ofy && a.transform.h == b.transform.h &&
         a.transform.depth_origin == b.transform.depth_origin &&
         a.transform.ot_shift == b.transform.ot_shift &&
         a.gpu.da_x0 - a.gpu.off_x == b.gpu.da_x0 - b.gpu.off_x &&
         a.gpu.da_y0 - a.gpu.off_y == b.gpu.da_y0 - b.gpu.off_y &&
         a.gpu.da_x1 - a.gpu.off_x == b.gpu.da_x1 - b.gpu.off_x &&
         a.gpu.da_y1 - a.gpu.off_y == b.gpu.da_y1 - b.gpu.off_y && a.gpu.tw_mx == b.gpu.tw_mx &&
         a.gpu.tw_my == b.gpu.tw_my && a.gpu.tw_ox == b.gpu.tw_ox && a.gpu.tw_oy == b.gpu.tw_oy &&
         std::equal(a.primitives.begin(),
                    a.primitives.end(),
                    b.primitives.begin(),
                    [](const auto &x, const auto &y) {
                      if (x.source_ordinal != y.source_ordinal || x.quad != y.quad ||
                          x.two_sided != y.two_sided || x.semi_transparent != y.semi_transparent ||
                          x.ot_adjust != y.ot_adjust) {
                        return false;
                      }
                      const uint32_t nv = x.quad ? 4u : 3u;
                      for (uint32_t i = 0; i < nv; ++i) {
                        if (x.projected_offset[i] != y.projected_offset[i] ||
                            x.material_offset[i] != y.material_offset[i] ||
                            x.packet_attr[i] != y.packet_attr[i]) {
                          return false;
                        }
                      }
                      return true;
                    });
}

static void
log_frame_compatibility(const SpyroPairedFrame &a, const SpyroPairedFrame &b, bool compatible) {
  static uint64_t scanned = 0, matched = 0;
  ++scanned;
  matched += compatible;
  const bool identity = a.valid && b.valid && !a.culled && !b.culled && a.epoch == b.epoch;
  const bool topology = a.topology == b.topology && a.layer_counts == b.layer_counts &&
                        a.primitives.size() == b.primitives.size();
  const bool materials = a.materials == b.materials && a.override_control == b.override_control;
  const bool projection = a.transform.ofx == b.transform.ofx &&
                          a.transform.ofy == b.transform.ofy && a.transform.h == b.transform.h;
  const bool ordering = a.transform.depth_origin == b.transform.depth_origin &&
                        a.transform.ot_shift == b.transform.ot_shift;
  const bool gpu = a.gpu.da_x0 - a.gpu.off_x == b.gpu.da_x0 - b.gpu.off_x &&
                   a.gpu.da_y0 - a.gpu.off_y == b.gpu.da_y0 - b.gpu.off_y &&
                   a.gpu.da_x1 - a.gpu.off_x == b.gpu.da_x1 - b.gpu.off_x &&
                   a.gpu.da_y1 - a.gpu.off_y == b.gpu.da_y1 - b.gpu.off_y &&
                   a.gpu.tw_mx == b.gpu.tw_mx && a.gpu.tw_my == b.gpu.tw_my &&
                   a.gpu.tw_ox == b.gpu.tw_ox && a.gpu.tw_oy == b.gpu.tw_oy;
  lucent::debug("pairedactor",
                "temporal recipe census: scanned={} matched={} identity={} topology={} "
                "materials={} projection={} ordering={} gpu={} prev_faces={} cur_faces={}",
                scanned,
                matched,
                identity,
                topology,
                materials,
                projection,
                ordering,
                gpu,
                a.primitives.size(),
                b.primitives.size());
}

static bool rebuild_recipe_eligible(const SpyroPairedFrame &frame, bool duplicate) {
  return frame.valid && !frame.culled && !duplicate;
}

static SpyroPairedRebuildResult emit_captured_endpoint(Core *c,
                                                       RenderQueue &rq,
                                                       const SpyroPairedFrame &frame,
                                                       const SpyroPairedGpuSnapshot &destination) {
  bool duplicate = false;
  const int queued = rq.consumed ? 0 : rq.n;
  for (int i = 0; i < queued; ++i) {
    duplicate |= rq.items[i].painter_object == 0x80023AC4u;
  }
  if (!rebuild_recipe_eligible(frame, duplicate)) {
    return SpyroPairedRebuildResult::Refused;
  }
  std::vector<spyro::paired_actor::ProjectedVertex> projected;
  projected.reserve(frame.pose.size());
  size_t at = 0;
  for (uint32_t layer = 0; layer < 3; ++layer) {
    std::array<uint32_t, 27> cr{};
    for (uint32_t i = 0; i < 8; ++i) {
      cr[i] = frame.transform.layer_cr[layer][i];
    }
    cr[24] = frame.transform.ofx;
    cr[25] = frame.transform.ofy;
    cr[26] = frame.transform.h;
    for (uint32_t n = 0; n < frame.layer_counts[layer]; ++n, ++at) {
      if (at >= frame.pose.size()) {
        return SpyroPairedRebuildResult::Refused;
      }
      const auto &v = frame.pose[at];
      const uint32_t d0 = (uint16_t)v[1] | ((uint32_t)(uint16_t)v[2] << 16);
      projected.push_back(project_rtps(d0, (uint16_t)v[0], cr));
    }
  }
  if (at != frame.pose.size()) {
    return SpyroPairedRebuildResult::Refused;
  }
  auto resolved =
      spyro::paired_actor::resolve_normal_faces(frame.primitives,
                                                projected,
                                                {frame.materials, frame.override_control},
                                                frame.transform.depth_origin,
                                                frame.transform.ot_shift);
  if (!resolved) {
    return SpyroPairedRebuildResult::Refused;
  }
  const auto &faces = resolved.faces;
  if (faces.empty()) {
    return SpyroPairedRebuildResult::NoOutput;
  }
  if (faces.size() > (size_t)(RQ_MAX - queued) || rq.mPainterScopeDepth || rq.mPainterInvalidId) {
    return SpyroPairedRebuildResult::Refused;
  }
  ProducerScope producer(&c->rsub.producerScope, kProducerKey, "pairedactor:normal");
  RenderQueue::PainterObjectScope painter(rq, kProducerKey);
  for (const auto &face : faces) {
    int xs[4]{}, ys[4]{}, us[4]{}, vs[4]{};
    float xsf[4]{}, ysf[4]{};
    unsigned char rs[4]{}, gs[4]{}, bs[4]{};
    float depth[4]{};
    const uint16_t clut = (uint16_t)(face.packet_attr[0] >> 16),
                   tpage = (uint16_t)(face.packet_attr[1] >> 16);
    const uint32_t nv = face.quad ? 4u : 3u;
    for (uint32_t v = 0; v < nv; ++v) {
      xs[v] = face.vertex[v].x + destination.off_x;
      ys[v] = face.vertex[v].y + destination.off_y;
      xsf[v] = face.vertex[v].screen_x + (float)destination.off_x;
      ysf[v] = face.vertex[v].screen_y + (float)destination.off_y;
      us[v] = face.packet_attr[v] & 0xFFu;
      vs[v] = (face.packet_attr[v] >> 8) & 0xFFu;
      const uint32_t rgb = face.material.rgb[v];
      rs[v] = rgb;
      gs[v] = rgb >> 8;
      bs[v] = rgb >> 16;
      depth[v] = proj_pz_to_ord(face.vertex[v].view_z);
    }
    rq.emitOrQueue(c,
                   1,
                   RQ_WORLD,
                   RQ_OM_DEPTH,
                   (int)nv,
                   0,
                   0,
                   xs,
                   ys,
                   xsf,
                   ysf,
                   us,
                   vs,
                   rs,
                   gs,
                   bs,
                   depth,
                   (tpage >> 7) & 3u,
                   (tpage & 0x0Fu) * 64,
                   ((tpage >> 4) & 1u) * 256,
                   (clut & 0x3Fu) * 16,
                   (clut >> 6) & 0x1FFu,
                   destination.tw_mx,
                   destination.tw_my,
                   destination.tw_ox,
                   destination.tw_oy,
                   destination.da_x0,
                   destination.da_y0,
                   destination.da_x1,
                   destination.da_y1,
                   (tpage >> 5) & 3u,
                   nullptr,
                   -1,
                   0.0f);
  }
  return SpyroPairedRebuildResult::Emitted;
}

static bool project_captured(const SpyroPairedFrame &frame,
                             std::vector<spyro::paired_actor::ProjectedVertex> &out) {
  out.clear();
  out.reserve(frame.pose.size());
  size_t at = 0;
  for (uint32_t layer = 0; layer < 3; ++layer) {
    std::array<uint32_t, 27> cr{};
    for (uint32_t i = 0; i < 8; ++i) {
      cr[i] = frame.transform.layer_cr[layer][i];
    }
    cr[24] = frame.transform.ofx;
    cr[25] = frame.transform.ofy;
    cr[26] = frame.transform.h;
    for (uint32_t n = 0; n < frame.layer_counts[layer]; ++n, ++at) {
      if (at >= frame.pose.size()) {
        return false;
      }
      const auto &v = frame.pose[at];
      out.push_back(
          project_rtps((uint16_t)v[1] | ((uint32_t)(uint16_t)v[2] << 16), (uint16_t)v[0], cr));
    }
  }
  return at == frame.pose.size();
}

static const SpyroPairedGpuSnapshot &temporal_destination(const SpyroPairedFrame &,
                                                          const SpyroPairedFrame &current) {
  return current.gpu;
}

static bool interpolate_projected(std::span<const spyro::paired_actor::ProjectedVertex> a,
                                  std::span<const spyro::paired_actor::ProjectedVertex> b,
                                  const SpyroPairedActorTransform &tr,
                                  float t,
                                  std::vector<spyro::paired_actor::ProjectedVertex> &out) {
  if (a.size() != b.size()) {
    return false;
  }
  out.clear();
  out.reserve(a.size());
  const float ofx = (float)(int32_t)tr.ofx / 65536.0f, ofy = (float)(int32_t)tr.ofy / 65536.0f,
              h = (float)tr.h;
  for (size_t i = 0; i < a.size(); ++i) {
    spyro::paired_actor::ProjectedVertex p{};
    p.raw_view_x = a[i].raw_view_x + (b[i].raw_view_x - a[i].raw_view_x) * t;
    p.raw_view_y = a[i].raw_view_y + (b[i].raw_view_y - a[i].raw_view_y) * t;
    p.raw_view_z = a[i].raw_view_z + (b[i].raw_view_z - a[i].raw_view_z) * t;
    if (!std::isfinite(p.raw_view_x) || !std::isfinite(p.raw_view_y) ||
        !std::isfinite(p.raw_view_z)) {
      return false;
    }
    const float irx = std::clamp(p.raw_view_x, -32768.0f, 32767.0f);
    const float iry = std::clamp(p.raw_view_y, -32768.0f, 32767.0f);
    p.view_z = std::max(h * 0.5f, p.raw_view_z);
    const float scale = h / p.view_z;
    p.screen_x = std::clamp(ofx + irx * scale, -1024.0f, 1023.0f);
    p.screen_y = std::clamp(ofy + iry * scale, -1024.0f, 1023.0f);
    p.x = (int16_t)std::clamp(round_screen(p.screen_x), -1024, 1023);
    p.y = (int16_t)std::clamp(round_screen(p.screen_y), -1024, 1023);
    p.depth = (uint16_t)std::clamp(p.raw_view_z, 0.0f, 65535.0f);
    p.view_x = (int16_t)irx;
    p.view_y = (int16_t)iry;
    out.push_back(p);
  }
  return true;
}

static SpyroPairedRebuildResult emit_interpolated(
    Core *c, RenderQueue &rq, const SpyroPairedFrame &prev, const SpyroPairedFrame &cur, float t) {
  // `t` selects content only. Both reruns target the current draw buffer selected for FPS60.
  const auto &destination = temporal_destination(prev, cur);
  if (!std::isfinite(t)) {
    return SpyroPairedRebuildResult::Refused;
  }
  if (t == 0.0f) {
    return emit_captured_endpoint(c, rq, prev, destination);
  }
  if (t == 1.0f) {
    return emit_captured_endpoint(c, rq, cur, destination);
  }
  if (t < 0.0f || t > 1.0f) {
    return SpyroPairedRebuildResult::Refused;
  }
  if (!frames_compatible(prev, cur) || prev.transform.ot_shift != cur.transform.ot_shift) {
    return SpyroPairedRebuildResult::Refused;
  }
  std::vector<spyro::paired_actor::ProjectedVertex> pa, pb;
  if (!project_captured(prev, pa) || !project_captured(cur, pb) || pa.size() != pb.size()) {
    return SpyroPairedRebuildResult::Refused;
  }
  std::vector<spyro::paired_actor::ProjectedVertex> pm;
  if (!interpolate_projected(pa, pb, cur.transform, t, pm)) {
    return SpyroPairedRebuildResult::Refused;
  }
  auto resolved =
      spyro::paired_actor::resolve_normal_faces_continuous(cur.primitives,
                                                           pm,
                                                           {cur.materials, cur.override_control},
                                                           cur.transform.depth_origin,
                                                           cur.transform.ot_shift);
  if (!resolved) {
    return SpyroPairedRebuildResult::Refused;
  }
  const SpyroPairedFrame &mid = cur;
  auto &faces = resolved.faces;
  if (faces.empty()) {
    return SpyroPairedRebuildResult::NoOutput;
  }
  const int queued = rq.consumed ? 0 : rq.n;
  if (faces.size() > (size_t)(RQ_MAX - queued) || rq.mPainterScopeDepth || rq.mPainterInvalidId) {
    return SpyroPairedRebuildResult::Refused;
  }
  for (int i = 0; i < queued; ++i) {
    if (rq.items[i].painter_object == 0x80023AC4u) {
      return SpyroPairedRebuildResult::Refused;
    }
  }
  ProducerScope producer(&c->rsub.producerScope, kProducerKey, "pairedactor:normal");
  RenderQueue::PainterObjectScope painter(rq, kProducerKey);
  for (const auto &face : faces) {
    int xs[4]{}, ys[4]{}, us[4]{}, vs[4]{};
    float xsf[4]{}, ysf[4]{}, depth[4]{};
    unsigned char rs[4]{}, gs[4]{}, bs[4]{};
    const uint16_t clut = face.packet_attr[0] >> 16, tpage = face.packet_attr[1] >> 16;
    const uint32_t nv = face.quad ? 4u : 3u;
    for (uint32_t v = 0; v < nv; ++v) {
      xs[v] = face.vertex[v].x + mid.gpu.off_x;
      ys[v] = face.vertex[v].y + mid.gpu.off_y;
      xsf[v] = face.vertex[v].screen_x + mid.gpu.off_x;
      ysf[v] = face.vertex[v].screen_y + mid.gpu.off_y;
      us[v] = face.packet_attr[v] & 255;
      vs[v] = (face.packet_attr[v] >> 8) & 255;
      const uint32_t rgb = face.material.rgb[v];
      rs[v] = rgb;
      gs[v] = rgb >> 8;
      bs[v] = rgb >> 16;
      depth[v] = proj_pz_to_ord(face.vertex[v].view_z);
    }
    rq.emitOrQueue(c,
                   1,
                   RQ_WORLD,
                   RQ_OM_DEPTH,
                   nv,
                   0,
                   0,
                   xs,
                   ys,
                   xsf,
                   ysf,
                   us,
                   vs,
                   rs,
                   gs,
                   bs,
                   depth,
                   (tpage >> 7) & 3,
                   (tpage & 15) * 64,
                   ((tpage >> 4) & 1) * 256,
                   (clut & 63) * 16,
                   (clut >> 6) & 511,
                   mid.gpu.tw_mx,
                   mid.gpu.tw_my,
                   mid.gpu.tw_ox,
                   mid.gpu.tw_oy,
                   mid.gpu.da_x0,
                   mid.gpu.da_y0,
                   mid.gpu.da_x1,
                   mid.gpu.da_y1,
                   (tpage >> 5) & 3,
                   nullptr,
                   -1,
                   0.0f);
  }
  return SpyroPairedRebuildResult::Emitted;
}

static bool submit_native(Core *c, SpyroPairedActorFrameState &state) {
  if (++state.invocations != 1) {
    return refuse_shipping(state, "second invocation in one drawn frame");
  }
  const uint32_t parserControl = c->mem_r32(0x80078A80u);
  ++state.parser_scanned;
  ((parserControl >> 24) != 0 ? state.parser_alternate : state.parser_normal)++;
  lucent::debug("pairedactor",
                "parser reachability: scanned={} normal={} alternate={} control=0x{:08X}",
                state.parser_scanned,
                state.parser_normal,
                state.parser_alternate,
                parserControl);
  if ((parserControl >> 24) != 0) {
    return refuse_shipping(state, "alternate/status-plane parser is active");
  }

  std::array<LayerDesc, kLayers> desc{};
  PairedPose pose;
  std::array<uint32_t, kLayers> decoded{};
  if (!build_descs(c, desc) || !decode_pose(c, desc, pose, decoded)) {
    return refuse_shipping(state, "incomplete animation descriptors or pose");
  }
  const uint32_t vertexCount = decoded[0] + decoded[1] + decoded[2];
  for (uint32_t layer = 0; layer < kLayers; ++layer) {
    if (!decoded[layer] || decoded[layer] != desc[layer].a.count) {
      return refuse_shipping(state, "resolved layer count does not match its descriptor");
    }
  }

  SpyroPairedActorTransform transform{};
  if (!build_transform(c, transform)) {
    return refuse_shipping(state, "production transform/projection inputs missing");
  }
  // 0x80023F50..23F8C rejects the whole invocation before primitive decode. This is a valid
  // no-output result, distinct from a missing group or a producer refusal.
  const int32_t tx = transform.base_mac[0], ty = transform.base_mac[1], tz = transform.base_mac[2];
  const int32_t zp = (int32_t)((uint32_t)tz + 2048u);
  if ((int32_t)((uint32_t)tz - 16384u) >= 0 || (int32_t)((uint32_t)zp - (uint32_t)tx) <= 0 ||
      (int32_t)((uint32_t)zp + (uint32_t)tx) <= 0 || (int32_t)((uint32_t)zp - (uint32_t)ty) <= 0 ||
      (int32_t)((uint32_t)zp + (uint32_t)ty) <= 0) {
    state.culled = true;
    return true;
  }
  std::vector<spyro::paired_actor::ProjectedVertex> projected;
  projected.reserve(vertexCount);
  for (uint32_t layer = 0; layer < kLayers; ++layer) {
    std::array<uint32_t, 27> cr{};
    for (uint32_t i = 0; i < 8; ++i) {
      cr[i] = transform.layer_cr[layer][i];
    }
    cr[24] = transform.ofx;
    cr[25] = transform.ofy;
    cr[26] = transform.h;
    for (const Vec3i &v : pose.layers[layer].vertices) {
      const uint32_t d0 = (uint16_t)v.y | ((uint32_t)(uint16_t)v.z << 16);
      projected.push_back(project_rtps(d0, (uint16_t)v.x, cr));
    }
  }
  if (projected.size() != vertexCount) {
    return refuse_shipping(state, "projection table incomplete");
  }

  const uint32_t stream = c->mem_r32(desc[0].a.model + 0x14u);
  const uint32_t colors = c->mem_r32(desc[0].a.model + 0x18u);
  if (!stream || !colors) {
    return refuse_shipping(state, "normal stream or material table missing");
  }
  const uint32_t bytes = c->mem_r32(stream);
  const uint32_t streamPhys = stream & 0x1FFFFFFFu;
  if ((stream & 0xFFE00000u) != 0x80000000u || (bytes & 3u) || streamPhys > 0x1FFFFCu ||
      bytes > 0x200000u - streamPhys - 4u) {
    return refuse_shipping(state, "normal stream byte count is unaligned or outside guest RAM");
  }
  std::vector<uint32_t> words(1u + bytes / 4u);
  for (uint32_t i = 0; i < words.size(); ++i) {
    words[i] = c->mem_r32(stream + i * 4u);
  }
  const auto primitives = spyro::paired_actor::decode_normal_stream(words);
  if (!primitives) {
    return refuse_shipping(state, "normal primitive stream malformed");
  }
  uint32_t maxMaterial = 0;
  for (const auto &primitive : primitives.primitives) {
    const uint32_t nv = primitive.quad ? 4u : 3u;
    for (uint32_t v = 0; v < nv; ++v) {
      if ((primitive.material_offset[v] & 3u) || primitive.material_offset[v] > 0x7FCu) {
        return refuse_shipping(
            state, "normal material offset is unaligned or outside encoded table range");
      }
      maxMaterial = std::max(maxMaterial, (uint32_t)primitive.material_offset[v]);
    }
  }
  std::vector<uint32_t> base(maxMaterial / 4u + 1u);
  const uint32_t colorPhys = colors & 0x1FFFFFFFu;
  if ((colors & 0xFFE00000u) != 0x80000000u || colorPhys > 0x1FFFFCu ||
      maxMaterial > 0x200000u - colorPhys - 4u) {
    return refuse_shipping(state, "normal material span is outside guest RAM");
  }
  for (uint32_t i = 0; i < base.size(); ++i) {
    base[i] = c->mem_r32(colors + i * 4u);
  }
  auto faces = spyro::paired_actor::resolve_normal_faces(primitives.primitives,
                                                         projected,
                                                         {base, c->mem_r32(0x80078A80u)},
                                                         transform.depth_origin,
                                                         transform.ot_shift);
  state.candidates = faces.candidates;
  state.faces = (uint32_t)faces.faces.size();
  if (!faces || faces.candidates != primitives.primitives.size()) {
    return refuse_shipping(state, "normal face census incomplete");
  }
  const GpuState &current = c->game->gpu;
  const int offX = current.s_off_x, offY = current.s_off_y;
  const int daX0 = current.s_da_x0, daY0 = current.s_da_y0, daX1 = current.s_da_x1,
            daY1 = current.s_da_y1;
  const int twMx = current.s_tw_mx, twMy = current.s_tw_my, twOx = current.s_tw_ox,
            twOy = current.s_tw_oy;
  for (const auto &face : faces.faces) {
    if (face.material.command & 2u) {
      return refuse_shipping(state, "semi-transparent face in opaque group");
    }
    if ((face.material.command & ~2u) != (face.quad ? 0x3Cu : 0x34u)) {
      return refuse_shipping(state, "untextured or unsupported primitive command");
    }
    const uint16_t tpage = (uint16_t)(face.packet_attr[1] >> 16);
    if (tpage & 0x0200u) {
      return refuse_shipping(state, "TPAGE dither bit is active");
    }
    if (((tpage >> 7) & 3u) > 2u) {
      return refuse_shipping(state, "TPAGE texture mode is unsupported");
    }
  }
  if (daX0 > daX1 || daY0 > daY1) {
    return refuse_shipping(state, "active GPU draw area is empty");
  }
  SpyroPairedFrame captured{};
  captured.valid = true;
  captured.epoch = state.stage2_epoch;
  captured.layer_counts = decoded;
  captured.transform = transform;
  captured.primitives = primitives.primitives;
  captured.materials = base;
  captured.override_control = c->mem_r32(0x80078A80u);
  captured.gpu = {offX, offY, daX0, daY0, daX1, daY1, twMx, twMy, twOx, twOy};
  captured.pose.reserve(vertexCount);
  for (const auto &layer : pose.layers) {
    for (const Vec3i &v : layer.vertices) {
      captured.pose.push_back({v.x, v.y, v.z});
    }
  }
  captured.topology = topology_fingerprint(decoded, captured.primitives);
  if (faces.faces.empty()) {
    state.current = std::move(captured);
    state.endpoints_compatible = frames_compatible(state.previous, state.current);
    log_frame_compatibility(state.previous, state.current, state.endpoints_compatible);
    lucent::debug("pairedactor",
                  "native joined zero-output invocation: candidates={} faces=0 vertices={}",
                  state.candidates,
                  vertexCount);
    return true;
  }
  RenderQueue &rq = c->game->rq;
  const int queued = rq.consumed ? 0 : rq.n;
  if (faces.faces.size() > (size_t)(RQ_MAX - queued)) {
    return refuse_shipping(state, "render queue lacks capacity for atomic painter group");
  }
  if (faces.faces.size() > PainterObjectLimits{}.max_faces) {
    return refuse_shipping(state, "painter group exceeds framework face limit");
  }
  if (rq.mPainterScopeDepth || rq.mPainterInvalidId) {
    return refuse_shipping(state, "render queue painter lifecycle is already invalid");
  }
  std::array<uint32_t, 256> painterIds{};
  size_t painterIdCount = 0;
  for (int i = 0; i < queued; ++i) {
    if (rq.items[i].painter_object) {
      const uint32_t id = rq.items[i].painter_object;
      if (id == 0x80023AC4u) {
        return refuse_shipping(state, "duplicate paired painter object already exists");
      }
      bool found = false;
      for (size_t k = 0; k < painterIdCount; ++k) {
        found |= painterIds[k] == id;
      }
      if (!found) {
        if (painterIdCount == painterIds.size()) {
          return refuse_shipping(state, "painter object capacity exhausted");
        }
        painterIds[painterIdCount++] = id;
      }
    }
  }
  if (painterIdCount >= PainterObjectLimits{}.max_objects) {
    return refuse_shipping(state, "painter object capacity exhausted");
  }
  const uint32_t baseSeq = rq.consumed ? 0u : rq.seq;
  if (faces.faces.size() - 1u > UINT32_MAX - baseSeq) {
    return refuse_shipping(state, "painter sequence range overflows");
  }
  const uint32_t finalSeq = baseSeq + (uint32_t)faces.faces.size() - 1u;
  if (queued && (!gpu_vk_order_bias_distinguishes(finalSeq))) {
    return refuse_shipping(state, "painter/ordinary tie channel would saturate");
  }
  if (emit_captured_endpoint(c, rq, captured, captured.gpu) != SpyroPairedRebuildResult::Emitted) {
    return refuse_shipping(state, "captured endpoint rebuild rejected prevalidated frame");
  }
  state.current = std::move(captured);
  state.endpoints_compatible = frames_compatible(state.previous, state.current);
  log_frame_compatibility(state.previous, state.current, state.endpoints_compatible);
  uint32_t grouped = 0;
  for (int i = 0; i < rq.n; ++i) {
    if (rq.items[i].painter_object == 0x80023AC4u) {
      ++grouped;
      const RqItem &item = rq.items[i];
      if (item.semi || item.painter_flags || item.layer != RQ_WORLD ||
          item.order_mode != RQ_OM_DEPTH || item.mode == 3) {
        lucent::error("pairedactor",
                      "FATAL: emitted painter item violates prevalidated planner contract");
        abort();
      }
    }
  }
  if (grouped != faces.faces.size()) {
    lucent::error("pairedactor",
                  "FATAL: painter accounting grouped={}/{} after atomic emit",
                  grouped,
                  faces.faces.size());
    abort();
  }
  state.groups = 1;
  lucent::debug("pairedactor",
                "native joined group: invocations=1 groups=1 candidates={} faces={} vertices={} "
                "offset=({}, {}) draw=({},{})-({},{}) tw=({},{},{},{})",
                state.candidates,
                state.faces,
                vertexCount,
                offX,
                offY,
                daX0,
                daY0,
                daX1,
                daY1,
                twMx,
                twMy,
                twOx,
                twOy);
  return true;
}

} // namespace

bool spyro_paired_actor_build_transform(Core *c, SpyroPairedActorTransform &out) {
  return build_transform(c, out);
}

bool spyro_paired_actor_decode_pose(Core *c) {
  std::array<LayerDesc, kLayers> desc;
  PairedPose pose;
  std::array<uint32_t, kLayers> decoded{};
  const bool descriptors = build_descs(c, desc);
  const bool ok = descriptors && decode_pose(c, desc, pose, decoded);
  // Negative-first diagnostic: every invocation says how many layers were
  // scanned and gives each denominator, even when construction fails before a
  // vertex can be decoded.  Silence can therefore never mean "three empty layers".
  lucent::debug("pairedpose",
                "0x80023AC4 pose: scanned_layers=3 valid_descriptors={} "
                "layer0={}/{} layer1={}/{} layer2={}/{} emitted_faces=0",
                descriptors ? 3 : 0,
                decoded[0],
                descriptors ? desc[0].a.count : 0,
                decoded[1],
                descriptors ? desc[1].a.count : 0,
                decoded[2],
                descriptors ? desc[2].a.count : 0);
  return ok;
}

bool spyro_paired_actor_submit(Core *c, SpyroPairedActorFrameState &state) {
  return submit_native(c, state);
}

SpyroPairedRebuildResult
spyro_paired_actor_rebuild_endpoint(Core *c, RenderQueue &target, const SpyroPairedFrame &frame) {
  return emit_captured_endpoint(c, target, frame, frame.gpu);
}

void spyro_paired_actor_frame_begin(SpyroPairedActorFrameState &state,
                                    bool state2,
                                    bool reference_leg,
                                    bool fps60_active) {
  if (fps60_active != state.was_fps60_active) {
    state.previous = {};
    state.current = {};
    state.endpoints_compatible = false;
  }
  state.was_fps60_active = fps60_active;
  if (reference_leg || !state2) {
    state.previous = {};
    state.current = {};
    state.endpoints_compatible = false;
  } else if (!state.was_state2) {
    ++state.stage2_epoch;
    state.previous = {};
    state.current = {};
    state.endpoints_compatible = false;
  } else {
    state.current = {};
    state.endpoints_compatible = false;
  }
  state.was_state2 = state2 && !reference_leg;
  state.invocations = state.groups = state.candidates = state.faces = 0;
  state.culled = false;
  state.refusal = nullptr;
}

SpyroPairedActorFrameState &spyro_paired_actor_state(Core *c) {
  if (!c || !c->gameCtx) {
    lucent::error("pairedactor", "FATAL: per-Core Spyro game context missing");
    abort();
  }
  return *static_cast<SpyroPairedActorFrameState *>(c->gameCtx);
}
void spyro_paired_actor_fps60_rotate(Core *c) {
  auto &state = spyro_paired_actor_state(c);
  if (state.current.valid && !state.refusal) {
    state.previous = std::move(state.current);
  } else {
    state.previous = {};
  }
  state.current = {};
  state.endpoints_compatible = false;
}

void spyro_paired_actor_fps60_world_pass(Core *c, float t) {
  if (!c || !c->game || !c->game->rqRedirect) {
    lucent::error("pairedactor", "FATAL: fps60 paired pass has no redirected sink");
    abort();
  }
  auto &state = spyro_paired_actor_state(c);
  if (!state.endpoints_compatible) {
    lucent::error("pairedactor", "FATAL: fps60 paired pass called without compatible endpoints");
    abort();
  }
  RenderQueue &sink = *c->game->rqRedirect;
  const int before = sink.n;
  const auto result = emit_interpolated(c, sink, state.previous, state.current, t);
  ++state.temporal_calls;
  if (t == 0.0f || t == 1.0f) {
    ++state.temporal_endpoint_calls;
  } else {
    ++state.temporal_midpoint_calls;
  }
  if (result == SpyroPairedRebuildResult::Emitted) {
    ++state.temporal_emitted;
  }
  if (result == SpyroPairedRebuildResult::NoOutput) {
    ++state.temporal_no_output;
  }
  lucent::debug("pairedactor",
                "temporal pass census: calls={} midpoint={} endpoint={} emitted={} no_output={} "
                "t={:.3f} sink_added={} result={}",
                state.temporal_calls,
                state.temporal_midpoint_calls,
                state.temporal_endpoint_calls,
                state.temporal_emitted,
                state.temporal_no_output,
                t,
                sink.n - before,
                (int)result);
  if (result == SpyroPairedRebuildResult::Refused) {
    lucent::error(
        "pairedactor", "FATAL: fps60 paired pass refused t={:.3f} result={}", t, (int)result);
    abort();
  }
}

bool spyro_paired_actor_fps60_eligible(const SpyroPairedActorFrameState &state) {
  if (!frames_compatible(state.previous, state.current)) {
    return false;
  }
  std::vector<spyro::paired_actor::ProjectedVertex> a, b;
  static uint64_t scanned = 0, projected = 0, resolved = 0, matched = 0;
  ++scanned;
  if (!project_captured(state.previous, a) || !project_captured(state.current, b)) {
    lucent::debug(
        "pairedactor",
        "temporal face census: scanned={} projected={} resolved={} matched={} projection=FAIL",
        scanned,
        projected,
        resolved,
        matched);
    return false;
  }
  ++projected;
  std::vector<spyro::paired_actor::ProjectedVertex> mid;
  if (!interpolate_projected(a, b, state.current.transform, 0.5f, mid)) {
    return false;
  }
  auto rm = spyro::paired_actor::resolve_normal_faces_continuous(
      state.current.primitives,
      mid,
      {state.current.materials, state.current.override_control},
      state.current.transform.depth_origin,
      state.current.transform.ot_shift);
  if (rm) {
    ++resolved;
  }
  const bool accepted = (bool)rm;
  matched += accepted;
  lucent::debug("pairedactor",
                "temporal continuous census: scanned={} projected={} resolved={} accepted={} "
                "candidates={} midpoint_faces={} error={}",
                scanned,
                projected,
                resolved,
                matched,
                rm ? rm.candidates : 0,
                rm ? rm.faces.size() : 0,
                rm && rm.error.empty() ? "none" : rm.error.c_str());
  return accepted;
}

bool spyro_paired_actor_frame_finish(const SpyroPairedActorFrameState &state,
                                     bool reference_leg,
                                     bool expect_group) {
  const uint32_t expected = expect_group ? 1u : 0u;
  const bool validZero = expect_group && (state.culled || state.faces == 0) && state.groups == 0;
  const bool ok = !state.refusal && (state.groups == expected || validZero) &&
                  state.invocations == (expect_group ? 1u : 0u);
  lucent::debug("pairedactor",
                "ownership gate: leg={} armed_groups={}/{} invocations={} faces={} culled={} "
                "refusal={} => {}",
                reference_leg ? "reference" : "native",
                state.groups,
                expected,
                state.invocations,
                state.faces,
                state.culled,
                state.refusal ? state.refusal : "none",
                ok ? "PASS" : "FAIL");
  return ok;
}

void spyro_paired_actor_oracle_arm(Core *c) {
  sGuest = {};
  static constexpr uint32_t targets[] = {0x800257A0u, 0x800258B0u};
  if (!c->pcObserver.arm(targets, std::size(targets), capture_ot_checkpoint, &sGuest)) {
    abort();
  }
  gte_op_observer_arm(c, capture_guest_xyz, capture_guest_projection, &sGuest);
}

bool spyro_paired_actor_oracle_finish(Core *c) {
  return compare_actual_guest(c, sGuest);
}

int spyro_paired_actor_selftest() {
  int checks = 0;
  auto expect = [&](bool pass, const char *what) {
    ++checks;
    if (!pass) {
      lucent::error("selftest", "FAIL(pairedpose): {}", what);
    }
    return pass;
  };
  bool ok = true;
  ok &= expect(unpack_accum(0x00200801u).x == 1, "packed X extraction");
  std::array<uint32_t, 27> identity{};
  identity[0] = 4096;
  identity[2] = 4096;
  identity[4] = 4096;
  identity[7] = 1000;
  identity[24] = 256u << 16;
  identity[25] = 120u << 16;
  identity[26] = 341;
  const auto center = project_rtps(0, 0, identity);
  ok &= expect(center.x == 256 && center.y == 120 && center.depth == 1000,
               "identity projection center and view depth");
  identity[7] = 170;
  const auto near = project_rtps(1, 0, identity);
  ok &= expect(near.x == 257 && near.y == 120 && near.depth == 170,
               "near-plane saturated UNR projection");
  ok &= expect(std::fabs(center.screen_x - 256.0f) < 1.0e-6f &&
                   std::fabs(center.screen_y - 120.0f) < 1.0e-6f,
               "float projection preserves optical center");
  identity[7] = 1000;
  identity[5] = 1;
  const auto fractional = project_rtps(0, 0, identity);
  ok &= expect(fractional.screen_x > 256.0f && fractional.screen_x < 257.0f && fractional.x == 256,
               "float projection retains subpixel endpoint discarded by integer SXY");
  Vec3i half = blend16({0, 0, 0}, {16, -16, 32}, 8);
  ok &= expect(half.x == 8, "half-frame positive blend");
  ok &= expect(half.y == -8 && half.z == 16, "half-frame signed blend");
  ok &= expect(blend16({7, -9, 11}, {99, 99, 99}, 0).x == 7, "zero blend preserves A");
  ok &= expect(keyframe_ptr(0xFFEF354Au) == 0x001E6A94u, "live frame-word pointer expansion");
  const Vec3i borrowed = rtps_input({11, -135, 128});
  ok &= expect(borrowed.x == 11 && borrowed.y == -135 && borrowed.z == 127,
               "RTPS DR0 addition carries negative Y into Z");
  const auto rootBorrowed = packed_root_input({-206, 4, -194});
  ok &= expect(rootBorrowed[0] == -4 && rootBorrowed[1] == 193 && rootBorrowed[2] == -206,
               "root RTPS packed add borrows negative low half into high half");
  // Layer zero's descriptor direction is fixed by 0x80023B94: the byte
  // stream starts after the short stream's payload prefix.
  constexpr uint32_t payload = 0x001E6608u, packedW8 = 0x05400000u;
  ok &= expect(payload + (packedW8 >> 20) == 0x001E665Cu,
               "layer-zero byte stream follows short stream payload");
  ok &=
      expect(validate_synthetic_ot(false, false),
             "local OT drains high bin then same-bin FIFO and ignores pre-existing global packet");
  ok &= expect(!validate_synthetic_ot(true, false), "local OT rejects corrupt tail");
  ok &= expect(!validate_synthetic_ot(false, true), "local OT rejects corrupt link");
  ok &= expect(validate_synthetic_global(false), "global OT appends after pre-existing chain");
  ok &=
      expect(!validate_synthetic_global(true), "global OT rejects corrupt pre-existing tail link");
  SpyroPairedFrame fa{}, fb{};
  fa.valid = fb.valid = true;
  fa.epoch = fb.epoch = 7;
  fa.layer_counts = fb.layer_counts = {1, 1, 1};
  fa.topology = fb.topology = 0x1234;
  ok &= expect(frames_compatible(fa, fb), "identical immutable endpoint recipes are compatible");
  fb.epoch = 8;
  ok &= expect(!frames_compatible(fa, fb),
               "state2 exit and re-entry epoch rejects identical topology");
  fb = fa;
  fb.culled = true;
  ok &= expect(!frames_compatible(fa, fb), "culled endpoint resets compatibility");
  ok &= expect(!rebuild_recipe_eligible(fb, false), "culled endpoint rebuild refuses");
  fb.culled = false;
  fb.valid = false;
  ok &= expect(!rebuild_recipe_eligible(fb, false), "invalid endpoint rebuild refuses");
  fb.valid = true;
  ok &= expect(!rebuild_recipe_eligible(fb, true), "duplicate painter endpoint rebuild refuses");
  fa.materials = {0x11223344};
  fb = fa;
  fa.materials[0] = 0;
  ok &=
      expect(fb.materials[0] == 0x11223344, "captured material copy is guest-mutation independent");
  SpyroPairedActorTransform temporalTr{};
  temporalTr.ofx = 256u << 16;
  temporalTr.ofy = 120u << 16;
  temporalTr.h = 340;
  std::vector<spyro::paired_actor::ProjectedVertex> va(1), vb(1), vm;
  va[0].raw_view_x = 40000.0f;
  vb[0].raw_view_x = 20000.0f;
  va[0].raw_view_y = vb[0].raw_view_y = 0.0f;
  va[0].raw_view_z = vb[0].raw_view_z = 1000.0f;
  ok &= expect(interpolate_projected(va, vb, temporalTr, 0.5f, vm) && vm[0].view_x == 30000,
               "temporal raw X interpolates before IR saturation");
  vb[0].raw_view_x = 50000.0f;
  ok &= expect(interpolate_projected(va, vb, temporalTr, 0.5f, vm) && vm[0].view_x == 32767,
               "temporal interpolated raw X saturates once at the GTE IR limit");
  constexpr uint32_t envA = 0x80076EE0u, envB = 0x80076F64u;
  ok &= expect(nativeFrameDisplayEnv(envA, false) == envA &&
                   nativeFrameDisplayEnv(envB, false) == envB,
               "normal display policy keeps each draw env's guest previous-buffer DISPENV");
  ok &=
      expect(nativeFrameDisplayEnv(envA, true) == envB && nativeFrameDisplayEnv(envB, true) == envA,
             "FPS60 display policy selects reciprocal DISPENV for current A/B draw buffer");
  ok &= expect(nativeFrameDisplayEnv(0x80000000u, true) == 0,
               "display policy loudly refuses an unknown draw environment");
  SpyroPairedFrame destinationPrev{}, destinationCur{};
  destinationPrev.gpu.off_y = 0;
  destinationCur.gpu.off_y = 240;
  ok &= expect(temporal_destination(destinationPrev, destinationCur).off_y == 240,
               "forced t=0 content still targets current frame GPU destination");
  SpyroPairedActorFrameState life{};
  spyro_paired_actor_frame_begin(life, true, false, true);
  ok &= expect(!life.previous.valid && !life.endpoints_compatible,
               "first FPS60 frame has no temporal predecessor");
  life.previous.valid = true;
  life.current.valid = true;
  life.endpoints_compatible = true;
  spyro_paired_actor_frame_begin(life, false, false, true);
  ok &= expect(!life.previous.valid && !life.current.valid && !life.endpoints_compatible,
               "state2 exit clears both temporal endpoints");
  if (ok) {
    lucent::info("selftest", "PASS(pairedpose): {} checks", checks);
  }
  return ok ? 0 : 1;
}
