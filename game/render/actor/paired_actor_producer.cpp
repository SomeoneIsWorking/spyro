#include "paired_actor_producer.h"

#include "core.h"
#include "frame_env.h"
#include "game.h"
#include "paired_actor.h"
#include "paired_actor_color_fade.h"
#include "paired_actor_decode.h"
#include "paired_actor_pose.h"
#include "paired_actor_projection.h"
#include "paired_actor_temporal.h"
#include "producer_scope.h"
#include "proj_vtx.h"
#include "render_queue.h"
#include "spyro_context.h"
#include "spyro_flame_matrix.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include <lucent/log.h>

namespace {

using namespace spyro::paired_actor;
using spyro::paired_actor_projection::projectRtps;

bool refuse_shipping(spyro::paired_actor::FrameState &state, const char *why) {
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

} // namespace

bool spyro::paired_actor_producer::captureAndSubmit(Core *c,
                                                    spyro::paired_actor::FrameState &state,
                                                    bool authoredReplay) {
  if (++state.invocations != 1) {
    return refuse_shipping(state, "second invocation in one drawn frame");
  }
  const uint32_t fadeControl = c->mem_r32(spyro::paired_actor_color_fade::kControlAddress);
  ++state.parser_scanned;
  (spyro::paired_actor_color_fade::active(fadeControl) ? state.parser_faded
                                                       : state.parser_normal)++;
  lucent::debug("pairedactor",
                "colour-fade reachability: scanned={} plain={} faded={} control=0x{:08X}",
                state.parser_scanned,
                state.parser_normal,
                state.parser_faded,
                fadeControl);

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

  spyro::paired_actor::Transform transform{};
  const bool transformOk = build_transform(c, transform);
  // Layer 0 is placed by the instance position alone; layers 1 and 2 add a per-layer root offset
  // decoded from the animation's root words. A detached head or wing shows up here as a layer
  // translation that is not a small offset from layer 0's, so the failing side is named before any
  // pixel is inspected. Printed on the refusing path too, where the values are the defaults.
  lucent::debug(
      "pairedroot",
      "0x80023AC4 transform ok={} layer0_tr={},{},{} layer1_tr={},{},{} "
      "layer2_tr={},{},{} root1={},{},{} root2={},{},{} words={:08X}/{:08X}/{:08X}/{:08X}",
      transformOk,
      (int32_t)transform.layer_cr[0][5],
      (int32_t)transform.layer_cr[0][6],
      (int32_t)transform.layer_cr[0][7],
      (int32_t)transform.layer_cr[1][5],
      (int32_t)transform.layer_cr[1][6],
      (int32_t)transform.layer_cr[1][7],
      (int32_t)transform.layer_cr[2][5],
      (int32_t)transform.layer_cr[2][6],
      (int32_t)transform.layer_cr[2][7],
      transform.root_input[0][0],
      transform.root_input[0][1],
      transform.root_input[0][2],
      transform.root_input[1][0],
      transform.root_input[1][1],
      transform.root_input[1][2],
      transform.root_words[0],
      transform.root_words[1],
      transform.root_words[2],
      transform.root_words[3]);
  if (!transformOk) {
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
  // 0x80024110 hands the composed matrix to the flame renderer, past the cull test above and before
  // the layer 2 rotation restores the layer 0 matrix, so layer 1 is the state retail publishes.
  const bool flameMatrix = spyro::flame_matrix::publish(c,
                                                        {transform.layer_cr[1][0],
                                                         transform.layer_cr[1][1],
                                                         transform.layer_cr[1][2],
                                                         transform.layer_cr[1][3],
                                                         transform.layer_cr[1][4]});
  lucent::debug("pairedroot", "0x80024110 flame matrix published={}", flameMatrix);
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
      projected.push_back(projectRtps(d0, (uint16_t)v.x, cr));
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
  // Where the guest builds its faded copy: before the parser sees a single primitive, and over the
  // whole table rather than per face. Only the entries this model's offsets reach are built here,
  // and the fade is per entry, so the two tables agree everywhere either is read.
  spyro::paired_actor_color_fade::apply(fadeControl, base);
  auto faces = spyro::paired_actor::resolve_normal_faces(
      primitives.primitives, projected, {base}, transform.depth_origin, transform.ot_shift);
  state.candidates = faces.candidates;
  state.faces = (uint32_t)faces.faces.size();
  state.semiFaces =
      (uint32_t)std::count_if(faces.faces.begin(), faces.faces.end(), [](const ResolvedFace &face) {
        return face.material.semiTransparent();
      });
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
    if ((face.material.command & ~ResolvedMaterial::kSemiTransparentBit) !=
        (face.quad ? 0x3Cu : 0x34u)) {
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
  spyro::paired_actor::Frame captured{};
  captured.valid = true;
  captured.frameSerial = spyro::context(*c).worldTemporal.frameSerial();
  captured.epoch = state.stage2_epoch;
  captured.layer_counts = decoded;
  captured.authored_replay = authoredReplay;
  captured.transform = transform;
  captured.primitives = primitives.primitives;
  captured.materials = base;
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
    state.endpoints_compatible =
        spyro::paired_actor::framesCompatible(state.previous, state.current);
    spyro::paired_actor::logFrameCompatibility(
        state.previous, state.current, state.endpoints_compatible);
    lucent::debug("pairedactor",
                  "native joined zero-output invocation: candidates={} faces=0 vertices={}",
                  state.candidates,
                  vertexCount);
    return true;
  }
  RenderQueue &rq = c->game->rq;
  if (spyro::paired_actor::emitCapturedEndpoint(c, rq, captured, captured.gpu) !=
      spyro::paired_actor::RebuildResult::Emitted) {
    return refuse_shipping(state, "captured endpoint rebuild rejected prevalidated frame");
  }
  state.current = std::move(captured);
  state.endpoints_compatible = spyro::paired_actor::framesCompatible(state.previous, state.current);
  spyro::paired_actor::logFrameCompatibility(
      state.previous, state.current, state.endpoints_compatible);
  uint32_t grouped = 0;
  uint32_t groupedSemi = 0;
  for (int i = 0; i < rq.n; ++i) {
    if (rq.items[i].painter_object == kProducerAddress) {
      ++grouped;
      const RqItem &item = rq.items[i];
      groupedSemi += item.semi ? 1u : 0u;
      if (item.painter_flags || item.layer != RQ_WORLD || item.order_mode != RQ_OM_DEPTH ||
          item.mode == 3) {
        lucent::error("pairedactor",
                      "FATAL: emitted painter item violates prevalidated planner contract");
        abort();
      }
    }
  }
  const auto expectedSemi =
      (uint32_t)std::count_if(faces.faces.begin(), faces.faces.end(), [](const ResolvedFace &face) {
        return face.material.semiTransparent();
      });
  if (groupedSemi != expectedSemi) {
    lucent::error("pairedactor",
                  "FATAL: painter semi accounting grouped={} expected={} after atomic emit",
                  groupedSemi,
                  state.semiFaces);
    abort();
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
