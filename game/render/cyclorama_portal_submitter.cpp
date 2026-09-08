#include "cyclorama_portal_submitter.h"

#include "core.h"
#include "game.h"
#include "gpu_vk.h"
#include "producer_scope.h"
#include "render_queue.h"
#include "scene_painter_order.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace spyro::cyclorama_portal_submitter {
namespace {

constexpr uint32_t kReplayDomain = scene_painter_order::kActorWorldTerrainDomain;

bool isMeshFrame(const cyclorama_portal_mesh::PortalFrame &frame) {
  using cyclorama_portal_mesh::Status;
  return frame.status == Status::Ready || frame.status == Status::NearFamilyUnsupported;
}

uint32_t familyKey(const cyclorama_portal_mesh::PortalFrame &frame) {
  return frame.status == cyclorama_portal_mesh::Status::NearFamilyUnsupported
             ? cyclorama_portal_mesh::kNearProducerKey
             : cyclorama_portal_mesh::kProducerKey;
}

bool validVertex(const cyclorama_portal_mesh::Vertex &vertex) {
  return std::isfinite(vertex.screenX) && std::isfinite(vertex.screenY) &&
         std::isfinite(vertex.viewZ);
}

} // namespace

Plan prepare(const Core *core,
             const RenderQueue &queue,
             uint32_t producerKey,
             std::span<const Draw> draws) {
  Plan plan{};
  plan.producerKey = producerKey;
  if (core == nullptr || core->game == nullptr) {
    plan.status = Status::InvalidCore;
    return plan;
  }
  if (producerKey != cyclorama_portal_mesh::kProducerKey &&
      producerKey != cyclorama_portal_mesh::kNearProducerKey) {
    plan.status = Status::InvalidFamily;
    return plan;
  }

  std::vector<uint32_t> portalOrdinals;
  for (size_t drawIndex = 0; drawIndex < draws.size(); ++drawIndex) {
    const Draw &draw = draws[drawIndex];
    if (draw.frame == nullptr || draw.recipe == nullptr) {
      plan.status = Status::InvalidDraw;
      plan.faces.clear();
      return plan;
    }
    const auto &frame = *draw.frame;
    const auto &recipe = *draw.recipe;
    if (frame.status == cyclorama_portal_mesh::Status::ValidEmpty &&
        recipe.status == cyclorama_portal_mesh::Status::ValidEmpty) {
      continue;
    }
    if (!isMeshFrame(frame) || recipe.status != cyclorama_portal_mesh::Status::Ready) {
      plan.status = Status::InvalidRecipe;
      plan.faces.clear();
      return plan;
    }
    if (familyKey(frame) != producerKey) {
      plan.status = Status::InvalidFamily;
      plan.faces.clear();
      return plan;
    }
    if (!frame.maskVisible) {
      plan.status = Status::InvalidAperture;
      plan.faces.clear();
      return plan;
    }
    if (std::find(portalOrdinals.begin(), portalOrdinals.end(), frame.portalOrdinal) !=
        portalOrdinals.end()) {
      plan.status = Status::InvalidOrder;
      plan.faces.clear();
      return plan;
    }
    portalOrdinals.push_back(frame.portalOrdinal);
    for (size_t faceIndex = 0; faceIndex < recipe.faces.size(); ++faceIndex) {
      const auto &face = recipe.faces[faceIndex];
      for (const auto &vertex : face.vertices) {
        if (!validVertex(vertex)) {
          plan.status = Status::InvalidRecipe;
          plan.faces.clear();
          return plan;
        }
      }
      const auto replay = scene_painter_order::cycloramaPortal(
          frame.otBin, frame.portalOrdinal, (uint32_t)faceIndex);
      if (!replay.authored()) {
        plan.status = Status::InvalidOrder;
        plan.faces.clear();
        return plan;
      }
      plan.faces.push_back({drawIndex, faceIndex, replay});
    }
  }

  if (plan.faces.empty()) {
    plan.status = Status::ValidEmpty;
    return plan;
  }
  plan.admission =
      painter_submission::preflight(queue, producerKey, plan.faces.size(), kReplayDomain);
  if (!plan.admission.ready) {
    plan.status = Status::QueueCapacityExceeded;
    plan.faces.clear();
    return plan;
  }
  const uint32_t baseSequence = queue.consumed ? 0u : queue.seq;
  if (plan.faces.size() - 1u > std::numeric_limits<uint32_t>::max() - baseSequence ||
      ((plan.admission.queued || plan.admission.existingObjects) &&
       !gpu_vk_order_bias_distinguishes(baseSequence + (uint32_t)plan.faces.size() - 1u))) {
    plan.status = Status::OrderPrecisionExceeded;
    plan.faces.clear();
    return plan;
  }
  const GpuState &gpu = core->game->gpu;
  if (gpu.s_da_x0 > gpu.s_da_x1 || gpu.s_da_y0 > gpu.s_da_y1) {
    plan.status = Status::InvalidDrawArea;
    plan.faces.clear();
    return plan;
  }
  plan.status = Status::Ready;
  return plan;
}

void submit(Core *core, RenderQueue &queue, std::span<const Draw> draws, const Plan &plan) {
  if (plan.status != Status::Ready || core == nullptr || core->game == nullptr ||
      plan.producerKey == 0 || plan.faces.empty()) {
    return;
  }
  const GpuState gpu = core->game->gpu;
  int drawRight = gpu.s_da_x1;
  if (gpu_vk_wide_engine(core)) {
    drawRight = std::max(drawRight, gpu_vk_wide_engine_w(core) - 1);
  }
  ProducerScope producer(&core->rsub.producerScope,
                         plan.producerKey,
                         plan.producerKey == cyclorama_portal_mesh::kNearProducerKey
                             ? "cyclorama:portal-near"
                             : "cyclorama:portal-far");
  RenderQueue::PainterObjectScope painter(queue, plan.producerKey);
  for (const FaceRef &ref : plan.faces) {
    if (ref.draw >= draws.size() || draws[ref.draw].frame == nullptr ||
        draws[ref.draw].recipe == nullptr || ref.face >= draws[ref.draw].recipe->faces.size()) {
      return;
    }
    const auto &face = draws[ref.draw].recipe->faces[ref.face];
    int xs[3]{}, ys[3]{}, us[3]{}, vs[3]{};
    float screenX[3]{}, screenY[3]{}, depth[3]{};
    unsigned char red[3]{}, green[3]{}, blue[3]{};
    for (size_t vertexIndex = 0; vertexIndex < face.vertices.size(); ++vertexIndex) {
      const auto &vertex = face.vertices[vertexIndex];
      xs[vertexIndex] = vertex.sx + gpu.s_off_x;
      ys[vertexIndex] = vertex.sy + gpu.s_off_y;
      screenX[vertexIndex] = vertex.screenX + (float)gpu.s_off_x;
      screenY[vertexIndex] = vertex.screenY + (float)gpu.s_off_y;
      depth[vertexIndex] = core->rsub.projParams.pzToOrd(vertex.viewZ);
      red[vertexIndex] = (uint8_t)vertex.rgb;
      green[vertexIndex] = (uint8_t)(vertex.rgb >> 8);
      blue[vertexIndex] = (uint8_t)(vertex.rgb >> 16);
    }
    queue.emitOrQueue(core,
                      1,
                      RQ_WORLD,
                      RQ_OM_DEPTH,
                      3,
                      0,
                      0,
                      xs,
                      ys,
                      screenX,
                      screenY,
                      us,
                      vs,
                      red,
                      green,
                      blue,
                      depth,
                      3,
                      0,
                      0,
                      0,
                      0,
                      gpu.s_tw_mx,
                      gpu.s_tw_my,
                      gpu.s_tw_ox,
                      gpu.s_tw_oy,
                      gpu.s_da_x0,
                      gpu.s_da_y0,
                      drawRight,
                      gpu.s_da_y1,
                      0,
                      nullptr,
                      -1,
                      0.0f,
                      face.gouraud ? 1 : 0,
                      gpu.s_tp_dither,
                      ref.replay);
  }
}

const char *statusName(Status status) {
  switch (status) {
  case Status::Ready:
    return "ready";
  case Status::ValidEmpty:
    return "valid empty";
  case Status::InvalidCore:
    return "invalid core";
  case Status::InvalidDraw:
    return "invalid draw";
  case Status::InvalidFamily:
    return "invalid family";
  case Status::InvalidAperture:
    return "invalid aperture";
  case Status::InvalidRecipe:
    return "invalid recipe";
  case Status::InvalidOrder:
    return "invalid order";
  case Status::QueueCapacityExceeded:
    return "queue capacity exceeded";
  case Status::OrderPrecisionExceeded:
    return "order precision exceeded";
  case Status::InvalidDrawArea:
    return "invalid draw area";
  }
  return "unknown";
}

} // namespace spyro::cyclorama_portal_submitter
