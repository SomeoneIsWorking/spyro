#include "paired_actor_temporal.h"

#include "actor_ot_coalescer.h"
#include "core.h"
#include "frame_env.h"
#include "game.h"
#include "gpu_vk.h"
#include "native_projection.h"
#include "painter_object_layer.h"
#include "painter_submission_preflight.h"
#include "paired_actor_depth.h"
#include "paired_actor_projection.h"
#include "proj_params.h"
#include "render_queue.h"
#include "scene_painter_order.h"
#include "spyro_context.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <span>
#include <vector>

namespace {

using namespace spyro::paired_actor;
using spyro::paired_actor_projection::projectRtps;
using spyro::paired_actor_projection::roundScreen;

bool preflight_paired(const RenderQueue &queue, size_t faces, bool authoredReplay) {
  const uint32_t domain =
      authoredReplay ? spyro::scene_painter_order::kActorWorldTerrainDomain : 0u;
  const auto plan = spyro::painter_submission::preflight(queue, kProducerAddress, faces, domain);
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

spyro::paired_actor::RebuildResult
emit_faces(Core *c,
           RenderQueue &rq,
           std::span<const spyro::paired_actor::ResolvedFace> faces,
           bool authoredReplay,
           uint32_t depthNear,
           uint32_t control,
           const spyro::paired_actor::GpuSnapshot &destination) {
  if (faces.empty()) {
    return spyro::paired_actor::RebuildResult::NoOutput;
  }
  if (!preflight_paired(rq, faces.size(), authoredReplay)) {
    return spyro::paired_actor::RebuildResult::Refused;
  }
  const auto mapping = global_bins(faces, authoredReplay, depthNear, control);
  if (!mapping.valid) {
    return spyro::paired_actor::RebuildResult::Refused;
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
  ProducerScope producer(&c->rsub.producerScope, kProducerAddress, "pairedactor:normal");
  RenderQueue::PainterObjectScope painter(rq, kProducerAddress);
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
                   face.material.semiTransparent() ? 1 : 0,
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
                   (int)face.blendMode(),
                   nullptr,
                   -1,
                   0.0f,
                   0,
                   0,
                   authoredReplay ? spyro::scene_painter_order::pairedActor(mapping.bins[faceIndex],
                                                                            faceOrdinal)
                                  : PainterReplayOrder{});
  }
  return spyro::paired_actor::RebuildResult::Emitted;
}

spyro::paired_actor::RebuildResult emit_interpolated(Core *c,
                                                     RenderQueue &rq,
                                                     const spyro::paired_actor::Frame &prev,
                                                     const spyro::paired_actor::Frame &cur,
                                                     float t) {
  const auto &destination = spyro::paired_actor::temporalDestination(prev, cur);
  if (!std::isfinite(t)) {
    return spyro::paired_actor::RebuildResult::Refused;
  }
  if (t == 0.0f) {
    return spyro::paired_actor::emitCapturedEndpoint(c, rq, prev, destination);
  }
  if (t == 1.0f) {
    return spyro::paired_actor::emitCapturedEndpoint(c, rq, cur, destination);
  }
  if (t < 0.0f || t > 1.0f) {
    return spyro::paired_actor::RebuildResult::Refused;
  }
  if (!spyro::paired_actor::framesCompatible(prev, cur) ||
      prev.transform.ot_shift != cur.transform.ot_shift) {
    return spyro::paired_actor::RebuildResult::Refused;
  }
  std::vector<spyro::paired_actor::ProjectedVertex> pa, pb;
  if (!spyro::paired_actor::projectCaptured(prev, pa) ||
      !spyro::paired_actor::projectCaptured(cur, pb) || pa.size() != pb.size()) {
    return spyro::paired_actor::RebuildResult::Refused;
  }
  std::vector<spyro::paired_actor::ProjectedVertex> pm;
  if (!spyro::paired_actor::interpolateProjected(pa, pb, cur.transform, t, pm)) {
    return spyro::paired_actor::RebuildResult::Refused;
  }
  const auto depth = spyro::paired_actor_depth::interpolate(prev.transform.base_mac[2],
                                                            cur.transform.base_mac[2],
                                                            cur.transform.depth_bias,
                                                            cur.transform.ot_control,
                                                            t);
  if (!depth) {
    return spyro::paired_actor::RebuildResult::Refused;
  }
  auto resolved = spyro::paired_actor::resolve_normal_faces_continuous(
      cur.primitives, pm, {cur.materials}, depth->origin, depth->shift);
  if (!resolved) {
    return spyro::paired_actor::RebuildResult::Refused;
  }
  return emit_faces(c,
                    rq,
                    resolved.faces,
                    cur.authored_replay,
                    depth->near,
                    cur.transform.ot_control,
                    destination);
}

} // namespace

spyro::paired_actor::RebuildResult
spyro::paired_actor::emitCapturedEndpoint(Core *c,
                                          RenderQueue &rq,
                                          const spyro::paired_actor::Frame &frame,
                                          const spyro::paired_actor::GpuSnapshot &destination) {
  bool duplicate = false;
  const int queued = rq.consumed ? 0 : rq.n;
  for (int i = 0; i < queued; ++i) {
    duplicate |= rq.items[i].painter_object == spyro::paired_actor::kProducerAddress;
  }
  if (!spyro::paired_actor::rebuildRecipeEligible(frame, duplicate)) {
    return spyro::paired_actor::RebuildResult::Refused;
  }
  std::vector<spyro::paired_actor::ProjectedVertex> projected;
  if (!spyro::paired_actor::projectCaptured(frame, projected)) {
    return spyro::paired_actor::RebuildResult::Refused;
  }
  auto resolved = spyro::paired_actor::resolve_normal_faces(frame.primitives,
                                                            projected,
                                                            {frame.materials},
                                                            frame.transform.depth_origin,
                                                            frame.transform.ot_shift);
  if (!resolved) {
    return spyro::paired_actor::RebuildResult::Refused;
  }
  return emit_faces(c,
                    rq,
                    resolved.faces,
                    frame.authored_replay,
                    frame.transform.depth_near,
                    frame.transform.ot_control,
                    destination);
}

bool spyro::paired_actor::framesCompatible(const spyro::paired_actor::Frame &a,
                                           const spyro::paired_actor::Frame &b) {
  return a.valid && b.valid && !a.culled && !b.culled && a.topology == b.topology &&
         a.epoch == b.epoch && a.layer_counts == b.layer_counts &&
         a.authored_replay == b.authored_replay && a.primitives.size() == b.primitives.size() &&
         a.materials == b.materials && a.transform.ofx == b.transform.ofx &&
         a.transform.ofy == b.transform.ofy && a.transform.h == b.transform.h &&
         a.transform.ot_control == b.transform.ot_control &&
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

bool spyro::paired_actor::rebuildRecipeEligible(const spyro::paired_actor::Frame &frame,
                                                bool duplicate) {
  return frame.valid && !frame.culled && !duplicate;
}

bool spyro::paired_actor::projectCaptured(const spyro::paired_actor::Frame &frame,
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
      out.push_back(spyro::paired_actor_projection::projectRtps(d0, (uint16_t)v[0], cr));
    }
  }
  return at == frame.pose.size();
}

const spyro::paired_actor::GpuSnapshot &
spyro::paired_actor::temporalDestination(const spyro::paired_actor::Frame &,
                                         const spyro::paired_actor::Frame &current) {
  return current.gpu;
}

bool spyro::paired_actor::interpolateProjected(
    std::span<const spyro::paired_actor::ProjectedVertex> a,
    std::span<const spyro::paired_actor::ProjectedVertex> b,
    const spyro::paired_actor::Transform &tr,
    float t,
    std::vector<spyro::paired_actor::ProjectedVertex> &out) {
  if (a.size() != b.size()) {
    return false;
  }
  out.clear();
  out.reserve(a.size());
  const psxport::native_projection::ProjectionParams projection{
      (int32_t)tr.ofx, (int32_t)tr.ofy, (uint16_t)tr.h};
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
    const auto projected = psxport::native_projection::project_view(
        {p.raw_view_x, p.raw_view_y, p.raw_view_z}, projection);
    p.view_z = projected.pz;
    p.screen_x = projected.px;
    p.screen_y = projected.py;
    p.x = (int16_t)std::clamp(spyro::paired_actor_projection::roundScreen(p.screen_x), -1024, 1023);
    p.y = (int16_t)std::clamp(spyro::paired_actor_projection::roundScreen(p.screen_y), -1024, 1023);
    p.depth = (uint16_t)std::clamp(p.raw_view_z, 0.0f, 65535.0f);
    p.view_x = (int16_t)irx;
    p.view_y = (int16_t)iry;
    out.push_back(p);
  }
  return true;
}

spyro::paired_actor::RebuildResult spyro::paired_actor::rebuildEndpoint(
    Core *c, RenderQueue &target, const spyro::paired_actor::Frame &frame) {
  return spyro::paired_actor::emitCapturedEndpoint(c, target, frame, frame.gpu);
}

void spyro::paired_actor::frameBegin(spyro::paired_actor::FrameState &state,
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
  state.invocations = state.groups = state.candidates = state.faces = state.semiFaces = 0;
  state.culled = false;
  state.refusal = nullptr;
}

void spyro::paired_actor::fps60Rotate(Core *c) {
  auto &state = spyro::paired_actor::state(c);
  state.temporal_eligible = false;
  if (state.current.valid && !state.refusal) {
    state.previous = std::move(state.current);
  } else {
    state.previous = {};
  }
  state.current = {};
  state.endpoints_compatible = false;
}

spyro::paired_actor::RebuildResult
spyro::paired_actor::rebuildSample(Core *core,
                                   RenderQueue &target,
                                   const spyro::paired_actor::Frame &previous,
                                   const spyro::paired_actor::Frame &current,
                                   float t) {
  return emit_interpolated(core, target, previous, current, t);
}

void spyro::paired_actor::fps60WorldPass(Core *c, float t) {
  if (!c || !c->game || !c->game->rqRedirect) {
    lucent::error("pairedactor", "FATAL: fps60 paired pass has no redirected sink");
    abort();
  }
  auto &state = spyro::paired_actor::state(c);
  if (!state.endpoints_compatible) {
    lucent::error("pairedactor", "FATAL: fps60 paired pass called without compatible endpoints");
    abort();
  }
  RenderQueue &sink = *c->game->rqRedirect;
  const int before = sink.n;
  const auto result = spyro::paired_actor::rebuildSample(c, sink, state.previous, state.current, t);
  ++state.temporal.calls;
  if (t == 0.0f || t == 1.0f) {
    ++state.temporal.endpoint_calls;
  } else if (t > 0.0f && t < 1.0f) {
    ++state.temporal.midpoint_calls;
  } else {
    lucent::error("pairedactor", "FATAL: fps60 paired pass received invalid t={}", t);
    abort();
  }
  if (result == spyro::paired_actor::RebuildResult::Emitted) {
    ++state.temporal.emitted;
  }
  if (result == spyro::paired_actor::RebuildResult::NoOutput) {
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
  if (result == spyro::paired_actor::RebuildResult::Refused) {
    lucent::error(
        "pairedactor", "FATAL: fps60 paired pass refused t={:.3f} result={}", t, (int)result);
    abort();
  }
}

bool spyro::paired_actor::fps60Eligible(spyro::paired_actor::FrameState &state) {
  ++state.temporal.eligibility_checks;
  if (!spyro::paired_actor::framesCompatible(state.previous, state.current)) {
    return false;
  }
  std::vector<spyro::paired_actor::ProjectedVertex> a, b;
  if (!spyro::paired_actor::projectCaptured(state.previous, a) ||
      !spyro::paired_actor::projectCaptured(state.current, b)) {
    lucent::debug(
        "pairedactor",
        "temporal face census: checks={} projected={} resolved={} accepted={} projection=FAIL",
        state.temporal.eligibility_checks,
        state.temporal.projected_checks,
        state.temporal.resolved_checks,
        state.temporal.accepted_checks);
    return false;
  }
  ++state.temporal.projected_checks;
  std::vector<spyro::paired_actor::ProjectedVertex> mid;
  if (!spyro::paired_actor::interpolateProjected(a, b, state.current.transform, 0.5f, mid)) {
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
      state.current.primitives, mid, {state.current.materials}, depth->origin, depth->shift);
  if (rm) {
    ++state.temporal.resolved_checks;
  }
  const bool accepted = rm && global_bins(rm.faces,
                                          state.current.authored_replay,
                                          depth->near,
                                          state.current.transform.ot_control)
                                  .valid;
  state.temporal.accepted_checks += accepted ? 1 : 0;
  lucent::debug("pairedactor",
                "temporal continuous census: checks={} projected={} resolved={} accepted={} "
                "candidates={} midpoint_faces={} error={}",
                state.temporal.eligibility_checks,
                state.temporal.projected_checks,
                state.temporal.resolved_checks,
                state.temporal.accepted_checks,
                rm ? rm.candidates : 0,
                rm ? rm.faces.size() : 0,
                rm && rm.error.empty() ? "none" : rm.error.c_str());
  if (accepted) {
    ++state.temporal.eligible_intervals;
  }
  return accepted;
}

bool spyro::paired_actor::frameFinish(const spyro::paired_actor::FrameState &state,
                                      bool reference_leg,
                                      bool expect_group) {
  const uint32_t expected = expect_group ? 1u : 0u;
  const bool validZero = expect_group && (state.culled || state.faces == 0) && state.groups == 0;
  const bool ok = !state.refusal && (state.groups == expected || validZero) &&
                  state.invocations == (expect_group ? 1u : 0u);
  // The caller aborts on a false, so the failing gate must say why at a level the default run
  // prints; as a debug-only line it made that abort read as a bare crash with no diagnosis.
  lucent::log(ok ? lucent::Level::Debug : lucent::Level::Error,
              "pairedactor",
              lucent::format("ownership gate: leg={} armed_groups={}/{} invocations={} faces={} "
                             "semi={} culled={} refusal={} => {}",
                             reference_leg ? "reference" : "native",
                             state.groups,
                             expected,
                             state.invocations,
                             state.faces,
                             state.semiFaces,
                             state.culled,
                             state.refusal ? state.refusal : "none",
                             ok ? "PASS" : "FAIL"));
  return ok;
}
