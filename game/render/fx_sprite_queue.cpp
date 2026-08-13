// fx_sprite_queue.cpp — the screen-space actor class of RasterizeSpritePrimQueue 0x80022A2C.
//
// This is a NATIVE PRODUCER: it reads the game's actor queue and mesh streams and submits resolved
// untextured polygons directly to RenderQueue. It never reads the GTE screen FIFO, ordering table,
// packet pool, or GP0 output. The platform GTE is used only for the game's lighting calculation —
// the same legitimate hardware-math boundary used by game/core/native_gte.cpp.
//
// Scope proven by C176 and the follow-up stage census: byte +0x50's sign bit selects this semantic
// class. A 7,000-present reference run exercised 12,092 such records and all of stage-13/mode-3's
// 4,294 records; that stage used only the flat bit01 primitive variant (134,528 faces). World-space
// records and the other three variants remain a loud gap, not a fallback through guest packets.
#include "render.h"
#include "core.h"
#include "game.h"
#include "render_queue.h"
#include "producer_scope.h"
#include "proj_params.h"
#include "cfg.h"
#include "producer_run.h"
#include "fx_paired_actor.h"
#include <lucent/log.h>
#include <algorithm>
#include <cstdint>
#include <limits>

uint32_t gte_read_data(uint32_t reg);
void gte_write_data(uint32_t reg, uint32_t v);
void gte_write_ctrl(uint32_t reg, uint32_t v);

namespace {

constexpr uint32_t kQueue = 0x800720F4u;
constexpr uint32_t kMeshTable = 0x80076378u;
constexpr uint32_t kPoolCursor = 0x80075710u;
constexpr uint32_t kPoolEnd = 0x800756FCu;
constexpr uint32_t kStage13State = 0x80078D7Cu;
constexpr uint32_t kStage13Timer = 0x80078D80u;
constexpr uint32_t kStage13Text = 0x80078D94u;
constexpr uint32_t kContinueFlag = 0x80078E78u;
constexpr uint32_t kGuestProducer = 0x80022A2Cu;
constexpr uint32_t kActorSize = 0x58u;
constexpr uint32_t kQueueCapacity = 256u;

int32_t sx8(uint8_t v) { return (int32_t)(int8_t)v; }

bool setup_screen_gte(Core* c, uint32_t actor) {
  const uint32_t flags = c->mem_r32(actor + 0x44u);
  // Stage-13 text animates only byte +0x46. The other two rotation arms are live elsewhere and are
  // deliberately left for the generic screen-class producer rather than guessed here.
  if ((flags & 0x0000FFFFu) != 0u) return false;

  // Screen-class arm 0x80022C84..22DA0: actor X/Y replace OFX/OFY, actor Z/2 is TRZ, and the base
  // rotation scales local Y by 0xA00/0x1000. DQA=1 is the guest's screen-class marker.
  gte_write_ctrl(24, c->mem_r32(actor + 0x0Cu) << 16);
  gte_write_ctrl(25, c->mem_r32(actor + 0x10u) << 16);
  gte_write_ctrl(5, 0); gte_write_ctrl(6, 0);
  gte_write_ctrl(7, (uint32_t)((int32_t)c->mem_r32(actor + 0x14u) >> 1));
  uint32_t m0 = 0x1000u, m1 = 0u, m2 = 0x0A00u, m3 = 0u, m4 = 0x1000u;
  gte_write_ctrl(0, m0); gte_write_ctrl(1, m1); gte_write_ctrl(2, m2);
  gte_write_ctrl(3, m3); gte_write_ctrl(4, m4); gte_write_ctrl(27, 1u);

  // Byte +0x46 is a Y rotation. This is the guest's two MVMVA basis-vector operations and its exact
  // packed-matrix reconstruction, not a host sin/cos approximation.
  const uint32_t angle = (flags >> 16) & 0xFFu;
  if (angle) {
    const uint32_t co = c->mem_r16(0x8006CC78u + angle * 2u);
    const uint32_t si = c->mem_r16(0x8006CBF8u + angle * 2u);
    gte_write_data(0, co); gte_write_data(1, si);
    gte_op(c, 0x4A486012u);
    const uint32_t a1 = gte_read_data(9), a2 = gte_read_data(10), a3 = gte_read_data(11);
    gte_write_data(0, (uint32_t)(uint16_t)(-(int16_t)si)); gte_write_data(1, co);
    gte_op(c, 0x4A486012u);
    const uint32_t b1 = gte_read_data(9), b2 = gte_read_data(10), b3 = gte_read_data(11);
    m0 = (m0 & 0xFFFF0000u) | (a1 & 0xFFFFu);
    m1 = (a2 << 16) | (b1 & 0xFFFFu);
    m2 = (b2 << 16) | (m2 & 0xFFFFu);
    m3 = (m3 & 0xFFFF0000u) | (a3 & 0xFFFFu);
    m4 = b3 & 0xFFFFu;
    gte_write_ctrl(0, m0); gte_write_ctrl(1, m1); gte_write_ctrl(2, m2);
    gte_write_ctrl(3, m3); gte_write_ctrl(4, m4);
  }
  return true;
}

void project_screen_vertex(Core* c, uint32_t p, int& x, int& y, uint32_t& z) {
  // 0x80023010..40 decodes the unaligned packed word with signed shifts, not three independent
  // signed bytes. The low bit of X includes z.bit7 and the low bit of Y includes x.bit7; dropping
  // those cross-byte bits changes half the RTPS inputs, producing 1px shifts and different NCLIP.
  const uint32_t raw = c->mem_r32(p);
  const int32_t vz = (int32_t)(raw << 24) >> 23;
  const int32_t vx = (int32_t)(raw << 16) >> 23;
  const int32_t vy = (int32_t)(raw << 8) >> 23;
  gte_write_data(0, (uint32_t)vx + ((uint32_t)vy << 16));
  gte_write_data(1, (uint32_t)vz);
  gte_op(c, 0x4A180001u);
  const uint32_t sxy = gte_read_data(14);
  x = (int16_t)(sxy & 0xFFFFu);
  y = (int16_t)(sxy >> 16);
  z = gte_read_data(19);
}

uint32_t flat_colour(Core* c, uint32_t actor_flags, uint32_t normal) {
  // The bit01 arm at 0x80023534..23650, through RGB2. Keep the GTE hardware operation rather than
  // reimplementing its saturation/colour rules in game code.
  const uint32_t a = c->mem_r16(0x800770C8u);
  const uint32_t b = c->mem_r16(0x800770CCu);
  const uint32_t d = c->mem_r16(0x800770D0u);
  gte_write_ctrl(16, a | (b << 16));
  gte_write_ctrl(17, (a << 16) | d);
  gte_write_ctrl(18, b | (d << 16));
  gte_write_ctrl(19, a | (b << 16));
  gte_write_ctrl(20, d);

  gte_write_data(11, (uint32_t)sx8((uint8_t)(normal >> 24)));
  gte_write_data(9,  (uint32_t)sx8((uint8_t)(normal >> 16)));
  gte_write_data(10, (uint32_t)sx8((uint8_t)(normal >> 8)));
  gte_op(c, 0x4A49E012u);

  const uint32_t light = c->mem_r32(0x8006E3D8u + (actor_flags >> 22));
  gte_write_data(8, (light >> 23) & 0x1Eu);
  gte_op(c, 0x4B90003Du);
  gte_write_ctrl(13, (light << 4) & 0xFF0u);
  gte_write_ctrl(14, (light >> 4) & 0xFF0u);
  gte_write_ctrl(15, (light >> 12) & 0xFF0u);
  gte_write_data(6, 0x00FFFFFFu);
  gte_op(c, 0x4B38041Cu);
  uint32_t colour = gte_read_data(22) & 0x00FFFFFFu;

  // The high-nibble brightening arm at 0x80023654; absent in the measured stage-13 records
  // (actor_flags=0x0B000000), but it is part of the semantic variant and costs nothing to preserve.
  const uint32_t boost = actor_flags >> 28;
  if (boost) {
    const int base = (int32_t)gte_read_data(9) - (int)(boost << 7) - (int32_t)gte_read_ctrl(13);
    if (base > 0) {
      const int add = base * 2;
      const auto ch = [add](uint32_t v) { return (uint32_t)std::min(255, ((int)v + add) >> 4); };
      colour = ch(gte_read_data(9)) | (ch(gte_read_data(10)) << 8) |
               (ch(gte_read_data(11)) << 16);
    }
  }
  return colour;
}

void zero_actor(Core* c, uint32_t actor) {
  for (uint32_t o = 0; o < kActorSize; o += 4u) c->mem_w32(actor + o, 0);
}

void build_text(Core* c, uint32_t string, int32_t& x, const int32_t pos[3],
                const int32_t scale[3], int32_t digit_advance, uint8_t style) {
  bool previous_digit_or_punct = true;
  for (uint32_t p = string; c->mem_r8(p); ++p) {
    const uint8_t ch = c->mem_r8(p);
    if (ch == 0x20u) {
      x += (scale[0] * 3) / 4;
      previous_digit_or_punct = true;
      continue;
    }
    uint32_t actor = c->mem_r32(kPoolCursor) - kActorSize;
    c->mem_w32(kPoolCursor, actor);
    zero_actor(c, actor);
    c->mem_w32(actor + 0x0Cu, (uint32_t)x);
    c->mem_w32(actor + 0x10u, (uint32_t)pos[1]);
    c->mem_w32(actor + 0x14u, (uint32_t)pos[2]);
    if (ch == 0x21u || ch == 0x3Fu) previous_digit_or_punct = true;
    if (!previous_digit_or_punct) {
      c->mem_w32(actor + 0x10u, (uint32_t)((int32_t)c->mem_r32(actor + 0x10u) + scale[1]));
      c->mem_w32(actor + 0x14u, (uint32_t)scale[2]);
    }
    uint16_t mesh = 0x4Cu;
    if (ch >= '0' && ch <= '9') mesh = (uint16_t)(ch + 0xD4u);
    else if (ch >= 'A' && ch <= 'Z') mesh = (uint16_t)(ch + 0x169u);
    else if (ch == '!') mesh = 0x4Bu;
    else if (ch == '?') mesh = 0x116u;
    else if (ch == '.') mesh = 0x147u;
    else if (ch != ',')
      c->mem_w32(actor + 0x10u,
                 (uint32_t)((int32_t)c->mem_r32(actor + 0x10u) - (scale[0] * 2) / 3));
    c->mem_w16(actor + 0x36u, mesh);
    c->mem_w8(actor + 0x47u, 0x7Fu);
    c->mem_w8(actor + 0x4Fu, style);
    c->mem_w8(actor + 0x50u, 0xFFu);
    x += previous_digit_or_punct ? digit_advance : scale[0];
    previous_digit_or_punct = ch >= '0' && ch <= '9';
  }
}

void append_pending_actors(Core* c) {
  uint32_t qi = 0;
  while (qi < kQueueCapacity && c->mem_r32(kQueue + qi * 4u)) ++qi;
  uint32_t p = c->mem_r32(kPoolCursor), end = c->mem_r32(kPoolEnd);
  while (p != end && qi < kQueueCapacity) {
    c->mem_w32(kQueue + qi++ * 4u, p);
    p += kActorSize;
  }
  c->mem_w32(kPoolCursor, p);
  if (qi < kQueueCapacity) c->mem_w32(kQueue + qi * 4u, 0);
  else lucent::error("spriteq", "native queue builder exhausted all {} entries; no terminator fits",
                     kQueueCapacity);
}

bool emit_screen_queue(Core* c) {
  const ProjParams& pp = c->rsub.projParams;
  float ofx, ofy, H;
  pp.requireGeom("Spyro screen sprite queue", ofx, ofy, H);
  (void)ofx; (void)ofy; (void)H;  // screen actors program OFX/OFY; RTPS reads the game's H from GTE
  GpuState& gs = c->game->gpu;
  ProducerScope producer(&c->rsub.producerScope, kGuestProducer,
                         "spriteq:RasterizeSpritePrimQueue.screen");
  uint64_t emitted = 0, rejected_world = 0, rejected_variant = 0;
  uint64_t candidate_tri = 0, candidate_quad = 0, culled_tri = 0, culled_quad = 0;
  uint64_t nclip_tri = 0, nclip_quad = 0, depth_tri = 0, depth_quad = 0;
  int min_x = std::numeric_limits<int>::max(), min_y = min_x;
  int max_x = std::numeric_limits<int>::min(), max_y = max_x;

  for (uint32_t qi = 0; qi < kQueueCapacity; ++qi) {
    const uint32_t actor = c->mem_r32(kQueue + qi * 4u);
    if (!actor) break;
    // Guest 0x80022A2C clears this per-actor transform-status byte on entry and marks it after the
    // transform is installed. It is persistent game state, not renderer scratch; omitting it lets
    // later frame logic observe a state the original producer never leaves behind.
    c->mem_w8(actor + 0x51u, 0u);
    if ((c->mem_r8(actor + 0x50u) & 0x80u) == 0) { rejected_world++; continue; }
    const uint16_t mesh_index = c->mem_r16(actor + 0x36u);
    const uint32_t mesh = c->mem_r32(kMeshTable + (uint32_t)mesh_index * 4u);
    if (!mesh) continue;
    const uint32_t nvtx = c->mem_r8(mesh + 0u);
    const uint32_t nprim = c->mem_r8(mesh + 1u);
    const uint32_t vertices = c->mem_r32(mesh + 4u) & 0x7FFFFFFFu;
    const uint32_t stream = c->mem_r32(mesh + 0x0Cu);
    if (nvtx > 128u) { lucent::error("spriteq", "mesh {} has {} vertices, native cap is 128", mesh_index, nvtx); continue; }
    if (!setup_screen_gte(c, actor)) { rejected_variant += nprim; continue; }
    c->mem_w8(actor + 0x51u, 1u);
    int projected_x[128], projected_y[128];
    uint32_t projected_z[128];
    for (uint32_t i = 0; i < nvtx; ++i)
      project_screen_vertex(c, vertices + i * 3u, projected_x[i], projected_y[i], projected_z[i]);

    for (uint32_t pi = 0; pi < nprim; ++pi) {
      const uint32_t packed = c->mem_r32(stream + pi * 8u);
      const uint32_t normal = c->mem_r32(stream + pi * 8u + 4u);
      if ((packed & 1u) == 0u || (packed & 2u) != 0u) { rejected_variant++; continue; }
      uint32_t vi[4] = { (packed >> 23u) & 0x7Fu, (packed >> 16u) & 0x7Fu,
                         (packed >> 9u) & 0x7Fu, (packed >> 2u) & 0x7Fu };
      const int count = vi[2] == vi[3] ? 3 : 4;
      if (count == 3) candidate_tri++; else candidate_quad++;
      if (vi[0] >= nvtx || vi[1] >= nvtx || vi[2] >= nvtx || vi[3] >= nvtx) continue;
      int xs[4]{}, ys[4]{}, us[4]{}, vs[4]{};
      for (int i = 0; i < count; ++i) {
        // SXY is packet-local. The PSX GPU applies DRAWENV E5 after packet submission; direct
        // RenderQueue producers must apply that same offset themselves (as fx_title_menu does).
        xs[i] = gs.s_off_x + projected_x[vi[i]];
        ys[i] = gs.s_off_y + projected_y[vi[i]];
        min_x = std::min(min_x, xs[i]); max_x = std::max(max_x, xs[i]);
        min_y = std::min(min_y, ys[i]); max_y = std::max(max_y, ys[i]);
      }
      // Guest uses NCLIP on the projected FIFO, not a host cross product. Keep its signed MAC0
      // result and saturation behavior exact; the host substitute dropped ten live faces at timer 171.
      for (int i = 0; i < 3; ++i) {
        const uint32_t sxy = (uint16_t)projected_x[vi[i]] |
                             ((uint32_t)(uint16_t)projected_y[vi[i]] << 16);
        gte_write_data(12u + (uint32_t)i, sxy);
      }
      gte_op(c, 0x4B400006u);
      int32_t nclip = (int32_t)gte_read_data(24);
      const int32_t maca = nclip;
      int32_t macb = 0;
      // Quad rule at 0x80023418/0x800236C8: if NCLIP(v0,v1,v2) is not positive, replace SXY0
      // with v3 and accept when NCLIP(v3,v1,v2) is negative. A triangle has no second test.
      bool visible = nclip > 0;
      if (!visible && count == 4) {
        const uint32_t sxy3 = (uint16_t)projected_x[vi[3]] |
                              ((uint32_t)(uint16_t)projected_y[vi[3]] << 16);
        gte_write_data(12, sxy3);
        gte_op(c, 0x4B400006u);
        macb = (int32_t)gte_read_data(24);
        visible = macb < 0;
      }
      if (cfg_str("PSXPORT_SPRITE_QUEUE_FACE_TRACE") &&
          (int32_t)c->mem_r32(kStage13Timer) == 171) {
        lucent::info("spriteface",
                     "leg=native qi={} mesh={} pi={} packed={:08X} count={} "
                     "sxy={}:{};{}:{};{}:{};{}:{} maca={} macb={} visible={}",
                     qi, mesh_index, pi, packed, count,
                     projected_x[vi[0]], projected_y[vi[0]],
                     projected_x[vi[1]], projected_y[vi[1]],
                     projected_x[vi[2]], projected_y[vi[2]],
                     projected_x[vi[3]], projected_y[vi[3]], maca, macb, visible ? 1 : 0);
      }
      if (!visible) {
        if (count == 3) nclip_tri++; else nclip_quad++;
        if (count == 3) culled_tri++; else culled_quad++;
        continue;
      }
      const int32_t trz = (int32_t)gte_read_ctrl(7);
      const int32_t depth_bias = std::max(trz - 256, 0) * 4;
      const int64_t depth_sum = (int64_t)projected_z[vi[0]] + projected_z[vi[1]] +
                                projected_z[vi[2]] + projected_z[vi[3]] - depth_bias;
      if (depth_sum <= 0) {
        if (count == 3) depth_tri++; else depth_quad++;
        if (count == 3) culled_tri++; else culled_quad++;
        continue;
      }
      const int sort_key = (int)(depth_sum >> 5) + (int)(packed & 3u);
      const uint32_t colour = flat_colour(c, c->mem_r32(actor + 0x4Cu), normal);
      unsigned char rs[4], gr[4], bs[4];
      for (int i = 0; i < 4; ++i) {
        rs[i] = (uint8_t)colour; gr[i] = (uint8_t)(colour >> 8); bs[i] = (uint8_t)(colour >> 16);
      }
      c->game->rq.emitOrQueue(c, 1, RQ_HUD, RQ_OM_2D_FG, count, 0, 0,
                              xs, ys, nullptr, nullptr, us, vs, rs, gr, bs, nullptr,
                              3, 0, 0, 0, 0, gs.s_tw_mx, gs.s_tw_my, gs.s_tw_ox, gs.s_tw_oy,
                              gs.s_da_x0, gs.s_da_y0, gs.s_da_x1, gs.s_da_y1, 0, nullptr,
                              sort_key);
      emitted++;
    }
  }
  lucent::debug("spriteq", "native screen queue: present={} state={} timer={} emitted={} candidates=(tri {},quad {}) culled=(tri {},quad {}; nclip {},{} depth {},{}) rejected_world={} rejected_variant={} bbox=({},{})..({},{})",
                spyro_producer_run_present_count(), c->mem_r32(kStage13State),
                (int32_t)c->mem_r32(kStage13Timer), emitted, candidate_tri, candidate_quad,
                culled_tri, culled_quad, nclip_tri, nclip_quad, depth_tri, depth_quad,
                rejected_world, rejected_variant,
                emitted ? min_x : 0, emitted ? min_y : 0, emitted ? max_x : 0, emitted ? max_y : 0);
  return rejected_world == 0 && rejected_variant == 0;
}

void trace_reference_faces(Core* c) {
  if (!cfg_str("PSXPORT_SPRITE_QUEUE_FACE_TRACE") ||
      (int32_t)c->mem_r32(kStage13Timer) != 171) return;
  uint64_t records = 0, candidates = 0;
  for (uint32_t qi = 0; qi < kQueueCapacity; ++qi) {
    const uint32_t actor = c->mem_r32(kQueue + qi * 4u);
    if (!actor) break;
    if ((c->mem_r8(actor + 0x50u) & 0x80u) == 0) continue;
    records++;
    const uint16_t mesh_index = c->mem_r16(actor + 0x36u);
    const uint32_t mesh = c->mem_r32(kMeshTable + (uint32_t)mesh_index * 4u);
    if (!mesh || !setup_screen_gte(c, actor)) continue;
    const uint32_t nvtx = c->mem_r8(mesh + 0u);
    const uint32_t nprim = c->mem_r8(mesh + 1u);
    const uint32_t vertices = c->mem_r32(mesh + 4u) & 0x7FFFFFFFu;
    const uint32_t stream = c->mem_r32(mesh + 0x0Cu);
    if (nvtx > 128u) continue;
    int px[128], py[128]; uint32_t pz[128];
    for (uint32_t i = 0; i < nvtx; ++i)
      project_screen_vertex(c, vertices + i * 3u, px[i], py[i], pz[i]);
    for (uint32_t pi = 0; pi < nprim; ++pi) {
      const uint32_t packed = c->mem_r32(stream + pi * 8u);
      if ((packed & 1u) == 0u || (packed & 2u) != 0u) continue;
      uint32_t vi[4] = {(packed >> 23u) & 0x7Fu, (packed >> 16u) & 0x7Fu,
                        (packed >> 9u) & 0x7Fu, (packed >> 2u) & 0x7Fu};
      if (vi[0] >= nvtx || vi[1] >= nvtx || vi[2] >= nvtx || vi[3] >= nvtx) continue;
      const int count = vi[2] == vi[3] ? 3 : 4;
      for (int i = 0; i < 3; ++i)
        gte_write_data(12u + (uint32_t)i, (uint16_t)px[vi[i]] |
          ((uint32_t)(uint16_t)py[vi[i]] << 16));
      gte_op(c, 0x4B400006u);
      const int32_t maca = (int32_t)gte_read_data(24);
      int32_t macb = 0;
      bool visible = maca > 0;
      if (!visible && count == 4) {
        gte_write_data(12, (uint16_t)px[vi[3]] |
          ((uint32_t)(uint16_t)py[vi[3]] << 16));
        gte_op(c, 0x4B400006u);
        macb = (int32_t)gte_read_data(24);
        visible = macb < 0;
      }
      candidates++;
      lucent::info("spriteface",
                   "leg=reference qi={} mesh={} pi={} packed={:08X} count={} "
                   "sxy={}:{};{}:{};{}:{};{}:{} maca={} macb={} visible={}",
                   qi, mesh_index, pi, packed, count,
                   px[vi[0]], py[vi[0]], px[vi[1]], py[vi[1]],
                   px[vi[2]], py[vi[2]], px[vi[3]], py[vi[3]], maca, macb, visible ? 1 : 0);
    }
  }
  lucent::info("spriteface", "leg=reference timer=171 scanned_records={} candidates={}",
               records, candidates);
  lucent::info("spritematrix", "phase=replica cr={:08X},{:08X},{:08X},{:08X},{:08X}",
               gte_read_ctrl(0), gte_read_ctrl(1), gte_read_ctrl(2),
               gte_read_ctrl(3), gte_read_ctrl(4));
}

}  // namespace

void spyro_trace_reference_sprite_faces(Core* c) { trace_reference_faces(c); }

void spyro_trace_reference_sprite_packets(Core* c, uint32_t begin, uint32_t end) {
  if (!cfg_str("PSXPORT_SPRITE_QUEUE_FACE_TRACE") ||
      (int32_t)c->mem_r32(kStage13Timer) != 171) return;
  lucent::info("spritematrix", "phase=guest cr={:08X},{:08X},{:08X},{:08X},{:08X}",
               gte_read_ctrl(0), gte_read_ctrl(1), gte_read_ctrl(2),
               gte_read_ctrl(3), gte_read_ctrl(4));
  uint32_t packet = 0;
  for (uint32_t p = begin; p < end;) {
    const uint32_t tag = c->mem_r32(p);
    const uint32_t bytes = ((tag >> 24) + 1u) * 4u;
    if (bytes < 20u || p + bytes > end) break;
    const uint32_t op = c->mem_r8(p + 7u) & 0xFCu;
    if (op == 0x20u || op == 0x28u) {
      int x[4]{}, y[4]{};
      const int count = op == 0x20u ? 3 : 4;
      for (int i = 0; i < count; ++i) {
        const uint32_t xy = c->mem_r32(p + 8u + (uint32_t)i * 4u);
        x[i] = (int16_t)xy; y[i] = (int16_t)(xy >> 16);
      }
      if (count == 3) { x[3] = x[2]; y[3] = y[2]; }
      lucent::info("spritepacket", "packet={} count={} sxy={}:{};{}:{};{}:{};{}:{}",
                   packet, count, x[0], y[0], x[1], y[1], x[2], y[2], x[3], y[3]);
    }
    packet++; p += bytes;
  }
  lucent::info("spritepacket", "timer=171 scanned_packets={} bytes={}", packet, end - begin);
}

bool SpyroRenderer::stage13Mode3Render() const {
  Core* c = mC;
  const uint32_t original_pool = c->mem_r32(kPoolCursor);
  const uint32_t state = c->mem_r32(kStage13State);
  const int32_t timer = (int32_t)c->mem_r32(kStage13Timer);
  if (state == 2u && timer > 0x8B) {
    int32_t x = 0;
    const int32_t pos[3] = {0, 0x78, 0x1400};
    const int32_t scale[3] = {0x0E, 1, 0x1600};
    int32_t stop = 0;
    const uint32_t text = c->mem_r32(kStage13Text);
    if (text == 0u) { x = 0x5C; build_text(c, 0x80010CF0u, x, pos, scale, 0x10, 0x0B); stop = 0xB8; }
    else if (text == 1u && c->mem_r8(kContinueFlag) == 0u) {
      x = 100; build_text(c, 0x80010D28u, x, pos, scale, 0x10, 0x0B); stop = 0xB6;
    } else if (text == 1u) {
      x = 0x50; build_text(c, 0x80010D0Cu, x, pos, scale, 0x10, 0x0B); stop = 0xBC;
    } else { x = 0x68; build_text(c, 0x80010D40u, x, pos, scale, 0x10, 0x0B); stop = 0xB2; }
    if (timer < stop)
      c->mem_w32(kPoolCursor, c->mem_r32(kPoolCursor) +
                 (uint32_t)(((stop - timer) >> 1) * (int32_t)kActorSize));
    uint32_t actor = original_pool - kActorSize;
    for (int32_t i = 0; (int32_t)actor >= (int32_t)c->mem_r32(kPoolCursor);
         ++i, actor -= kActorSize) {
      const int32_t phase = timer - (i + 0x8C);
      uint8_t value;
      if (phase < 0x38) value = (uint8_t)(phase * 8 + 0x40);
      else {
        const uint32_t angle = (uint32_t)(timer * 4 + i * 12) & 0xFFu;
        value = (uint8_t)(c->mem_r16(0x8006CC78u + angle * 2u) >> 7);
      }
      c->mem_w8(actor + 0x46u, value);
    }
  }

  // The handler clears both draw-env background colours, then rebuilds the request queue.
  for (uint32_t a : {0x80076EF9u, 0x80076EFAu, 0x80076EFBu,
                     0x80076F7Du, 0x80076F7Eu, 0x80076F7Fu}) c->mem_w8(a, 0);
  c->mem_w32(kQueue, 0);
  append_pending_actors(c);
  const bool complete_queue = emit_screen_queue(c);

  // First ownership slice of the separate 0x80023AC4 pass: resolve all three
  // animation layers now, from the same live state the eventual face producer
  // will consume.  It deliberately emits zero faces, so the completeness gate
  // below remains loud until projection/face ownership lands.
  const bool complete_pose = state != 2u || spyro_paired_actor_decode_pose(c);

  // 0x80023AC4 is a different producer, not a missing variant of this one. Refuse the frame while it
  // is armed so the native path cannot present a plausible text-only picture as complete.
  if (state == 2u && cfg_str("PSXPORT_SPRITE_QUEUE_SCREEN_TEST")) {
    static bool warned = false;
    if (!warned) {
      warned = true;
      lucent::warn("spriteq", "DIAGNOSTIC: presenting the native screen-queue layer while the separate "
                              "paired-actor pass 0x80023AC4 is OMITTED. This is a layer-isolation test, "
                              "not a complete native frame.");
    }
    return complete_queue && complete_pose;
  }
  return complete_queue && complete_pose && state != 2u;
}
