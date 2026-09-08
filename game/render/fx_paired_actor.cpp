// Owns renderer 0x80023AC4's normal arm; its alternate/status-plane arm remains a loud refusal.
#include "fx_paired_actor.h"
#include "actor_ot_coalescer.h"
#include "paired_actor_depth.h"

#include "core.h"
#include "frame_env.h"
#include "game.h"
#include "gpu_vk.h"
#include "native_projection.h"
#include "painter_object_layer.h"
#include "painter_submission_preflight.h"
#include "paired_actor_decode.h"
#include "paired_actor_pose.h"
#include "paired_actor_temporal_evidence.h"
#include "producer_scope.h"
#include "proj_params.h"
#include "proj_vtx.h"
#include "render_queue.h"
#include "scene_painter_order.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <lucent/log.h>
#include <numeric>
#include <span>
#include <vector>

namespace {

using namespace spyro::paired_actor;

constexpr uint32_t kProducerKey = 0x80023AC4u;

struct SyntheticLink {
  uint32_t address, next;
};

bool validate_synthetic_ot(bool corruptTail, bool corruptLink) {
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

bool validate_synthetic_global(bool corruptGlobalLink) {
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

spyro::paired_actor::ProjectedVertex
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

int round_screen(float v) {
  return (int)(v < 0.0f ? v - 0.5f : v + 0.5f);
}

bool refuse_shipping(SpyroPairedActorFrameState &state, const char *why) {
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

uint64_t topology_fingerprint(const std::array<uint32_t, 3> &counts,
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

bool frames_compatible(const SpyroPairedFrame &a, const SpyroPairedFrame &b) {
  return a.valid && b.valid && !a.culled && !b.culled && a.topology == b.topology &&
         a.epoch == b.epoch && a.layer_counts == b.layer_counts &&
         a.authored_replay == b.authored_replay && a.primitives.size() == b.primitives.size() &&
         a.materials == b.materials && a.override_control == b.override_control &&
         a.transform.ofx == b.transform.ofx && a.transform.ofy == b.transform.ofy &&
         a.transform.h == b.transform.h && a.transform.ot_control == b.transform.ot_control &&
         a.transform.depth_bias == b.transform.depth_bias &&
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

bool preflight_paired(const RenderQueue &queue, size_t faces, bool authoredReplay) {
  const uint32_t domain =
      authoredReplay ? spyro::scene_painter_order::kActorWorldTerrainDomain : 0u;
  const auto plan = spyro::painter_submission::preflight(queue, kProducerKey, faces, domain);
  if (!plan.ready) {
    return false;
  }
  const uint32_t firstSequence = queue.consumed ? 0u : queue.seq;
  if (faces - 1u > UINT32_MAX - firstSequence) {
    return false;
  }
  // Isolated groups share the renderer's finite depth-tie channel with ordinary draws.
  return authoredReplay || !plan.queued ||
         gpu_vk_order_bias_distinguishes(firstSequence + (uint32_t)faces - 1u);
}

bool rebuild_recipe_eligible(const SpyroPairedFrame &frame, bool duplicate) {
  return frame.valid && !frame.culled && !duplicate;
}

bool project_captured(const SpyroPairedFrame &frame,
                      std::vector<spyro::paired_actor::ProjectedVertex> &out);

spyro::actor_ot_coalescer::Result
global_bins(std::span<const spyro::paired_actor::ResolvedFace> faces,
            bool authoredReplay,
            uint32_t depthNear,
            uint32_t control) {
  if (!authoredReplay) {
    return {true, {}};
  }
  std::vector<uint32_t> bins;
  bins.reserve(faces.size());
  for (const auto &face : faces) {
    bins.push_back(face.ot_bin);
  }
  return spyro::actor_ot_coalescer::map({0u, depthNear, control}, bins);
}

SpyroPairedRebuildResult emit_faces(Core *c,
                                    RenderQueue &rq,
                                    std::span<const spyro::paired_actor::ResolvedFace> faces,
                                    bool authoredReplay,
                                    uint32_t depthNear,
                                    uint32_t control,
                                    const SpyroPairedGpuSnapshot &destination) {
  if (faces.empty()) {
    return SpyroPairedRebuildResult::NoOutput;
  }
  if (!preflight_paired(rq, faces.size(), authoredReplay)) {
    return SpyroPairedRebuildResult::Refused;
  }
  const auto mapping = global_bins(faces, authoredReplay, depthNear, control);
  if (!mapping.valid) {
    return SpyroPairedRebuildResult::Refused;
  }
  std::vector<size_t> replay(faces.size());
  std::iota(replay.begin(), replay.end(), size_t{0});
  if (authoredReplay) {
    // The shared OT appends each local bucket's source FIFO. Continuous depth remains useful
    // for isolated groups, but must not reorder two authored faces inside the same bucket.
    std::stable_sort(replay.begin(), replay.end(), [&](size_t left, size_t right) {
      const auto &a = faces[left];
      const auto &b = faces[right];
      if (a.ot_bin != b.ot_bin) {
        return a.ot_bin > b.ot_bin;
      }
      if (a.source_ordinal != b.source_ordinal) {
        return a.source_ordinal < b.source_ordinal;
      }
      return a.fragment_ordinal < b.fragment_ordinal;
    });
  }
  ProducerScope producer(&c->rsub.producerScope, kProducerKey, "pairedactor:normal");
  RenderQueue::PainterObjectScope painter(rq, kProducerKey);
  for (uint32_t faceOrdinal = 0; faceOrdinal < faces.size(); ++faceOrdinal) {
    const size_t faceIndex = replay[faceOrdinal];
    const auto &face = faces[faceIndex];
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
      depth[v] = proj_pz_to_ord((float)face.vertex[v].view_z);
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
                   0.0f,
                   0,
                   0,
                   authoredReplay ? spyro::scene_painter_order::pairedActor(mapping.bins[faceIndex],
                                                                            faceOrdinal)
                                  : PainterReplayOrder{});
  }
  return SpyroPairedRebuildResult::Emitted;
}

SpyroPairedRebuildResult emit_captured_endpoint(Core *c,
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
  if (!project_captured(frame, projected)) {
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
  return emit_faces(c,
                    rq,
                    resolved.faces,
                    frame.authored_replay,
                    frame.transform.depth_near,
                    frame.transform.ot_control,
                    destination);
}

bool project_captured(const SpyroPairedFrame &frame,
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
      const uint32_t d0 = (uint16_t)v[1] | ((uint32_t)(uint16_t)v[2] << 16);
      out.push_back(project_rtps(d0, (uint16_t)v[0], cr));
    }
  }
  return at == frame.pose.size();
}

const SpyroPairedGpuSnapshot &temporal_destination(const SpyroPairedFrame &,
                                                   const SpyroPairedFrame &current) {
  return current.gpu;
}

bool interpolate_projected(std::span<const spyro::paired_actor::ProjectedVertex> a,
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
    p.view_z = (int16_t)std::clamp(std::max(h * 0.5f, p.raw_view_z), -32768.0f, 32767.0f);
    const float scale = h / (float)p.view_z;
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

SpyroPairedRebuildResult emit_interpolated(
    Core *c, RenderQueue &rq, const SpyroPairedFrame &prev, const SpyroPairedFrame &cur, float t) {
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
  const auto depth = spyro::paired_actor_depth::interpolate(prev.transform.base_mac[2],
                                                            cur.transform.base_mac[2],
                                                            cur.transform.depth_bias,
                                                            cur.transform.ot_control,
                                                            t);
  if (!depth) {
    return SpyroPairedRebuildResult::Refused;
  }
  auto resolved = spyro::paired_actor::resolve_normal_faces_continuous(
      cur.primitives, pm, {cur.materials, cur.override_control}, depth->origin, depth->shift);
  if (!resolved) {
    return SpyroPairedRebuildResult::Refused;
  }
  return emit_faces(c,
                    rq,
                    resolved.faces,
                    cur.authored_replay,
                    depth->near,
                    cur.transform.ot_control,
                    destination);
}

bool submit_native(Core *c, SpyroPairedActorFrameState &state, bool authoredReplay) {
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
  captured.authored_replay = authoredReplay;
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
    spyro_paired_actor_log_frame_compatibility(
        state.previous, state.current, state.endpoints_compatible);
    lucent::debug("pairedactor",
                  "native joined zero-output invocation: candidates={} faces=0 vertices={}",
                  state.candidates,
                  vertexCount);
    return true;
  }
  RenderQueue &rq = c->game->rq;
  if (emit_captured_endpoint(c, rq, captured, captured.gpu) != SpyroPairedRebuildResult::Emitted) {
    return refuse_shipping(state, "captured endpoint rebuild rejected prevalidated frame");
  }
  state.current = std::move(captured);
  state.endpoints_compatible = frames_compatible(state.previous, state.current);
  spyro_paired_actor_log_frame_compatibility(
      state.previous, state.current, state.endpoints_compatible);
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
  return submit_native(c, state, false);
}

bool spyro_paired_actor_submit_field(Core *c, SpyroPairedActorFrameState &state) {
  return submit_native(c, state, true);
}

SpyroPairedRebuildResult
spyro_paired_actor_rebuild_endpoint(Core *c, RenderQueue &target, const SpyroPairedFrame &frame) {
  return emit_captured_endpoint(c, target, frame, frame.gpu);
}

void spyro_paired_actor_frame_begin(SpyroPairedActorFrameState &state,
                                    bool state2,
                                    bool reference_leg,
                                    bool fps60_active) {
  state.temporal_eligible = false;
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

void spyro_paired_actor_fps60_rotate(Core *c) {
  auto &state = spyro_paired_actor_state(c);
  state.temporal_eligible = false;
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
  ++state.temporal.calls;
  if (t == 0.0f || t == 1.0f) {
    ++state.temporal.endpoint_calls;
  } else if (t > 0.0f && t < 1.0f) {
    ++state.temporal.midpoint_calls;
  } else {
    lucent::error("pairedactor", "FATAL: fps60 paired pass received invalid t={}", t);
    abort();
  }
  if (result == SpyroPairedRebuildResult::Emitted) {
    ++state.temporal.emitted;
  }
  if (result == SpyroPairedRebuildResult::NoOutput) {
    ++state.temporal.no_output;
  }
  lucent::debug("pairedactor",
                "temporal pass census: calls={} midpoint={} endpoint={} emitted={} no_output={} "
                "t={:.3f} sink_added={} result={}",
                state.temporal.calls,
                state.temporal.midpoint_calls,
                state.temporal.endpoint_calls,
                state.temporal.emitted,
                state.temporal.no_output,
                t,
                sink.n - before,
                (int)result);
  if (result == SpyroPairedRebuildResult::Refused) {
    lucent::error(
        "pairedactor", "FATAL: fps60 paired pass refused t={:.3f} result={}", t, (int)result);
    abort();
  }
}

bool spyro_paired_actor_fps60_eligible(SpyroPairedActorFrameState &state) {
  ++state.temporal.eligibility_checks;
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
  const auto depth = spyro::paired_actor_depth::interpolate(state.previous.transform.base_mac[2],
                                                            state.current.transform.base_mac[2],
                                                            state.current.transform.depth_bias,
                                                            state.current.transform.ot_control,
                                                            0.5f);
  if (!depth) {
    return false;
  }
  auto rm = spyro::paired_actor::resolve_normal_faces_continuous(
      state.current.primitives,
      mid,
      {state.current.materials, state.current.override_control},
      depth->origin,
      depth->shift);
  if (rm) {
    ++resolved;
  }
  const bool accepted = rm && global_bins(rm.faces,
                                          state.current.authored_replay,
                                          depth->near,
                                          state.current.transform.ot_control)
                                  .valid;
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
  if (accepted) {
    ++state.temporal.eligible_intervals;
  }
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

int spyro_paired_actor_selftest() {
  int checks = 0;
  bool ok = true;
  auto expect = [&](bool pass, const char *what) {
    ++checks;
    if (!pass) {
      lucent::error("selftest", "FAIL(pairedpose): {}", what);
    }
    return pass;
  };

  ok &=
      expect(spyro_paired_temporal_selftest(), "temporal presenter evidence rejects partial runs");
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
