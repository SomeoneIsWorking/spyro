#include "field_shaded_queue_submitter.h"

#include "core.h"
#include "game.h"
#include "gpu_vk.h"
#include "hud_layout.h"
#include "render_queue.h"
#include "scene_painter_order.h"
#include "ui_anchor.h"
#include "wide_screen_space.h"

#include <algorithm>

namespace spyro::field_shaded_queue_submitter {
namespace {

// The horizontal shift a face is DRAWN with, on top of the widened projection it already carries.
//
// The twelve `g_Hud` Mobys are screen-space HUD parts that the guest authored against 512 columns
// and this path projects about the widened centre, which moves every one of them by the margin: a
// gem counter authored 46 px from the left edge lands 132 px in. `hud_layout` says which side of
// the frame each part belongs to and `ui_anchor` turns that into the correction, so the gem counter
// goes back to the left edge, the lives to the right one and the dragon count stays centred. Every
// other Moby (world actors, the glyphs a menu builds in the HUD arena, placed by their own
// producers) is left exactly as projected. This is draw-side only: the Moby records are guest
// memory and are never written. At 4:3 the correction is zero.
class HudAnchor {
public:
  explicit HudAnchor(Core *core) : frame_(spyro::ui_anchor::frame(core)) {}

  std::int32_t shiftFor(std::uint32_t actor) {
    const auto part = spyro::hud_layout::mobyPart(actor);
    if (!part) {
      return 0;
    }
    if (actor != lastActor_) {
      lastActor_ = actor;
      lastShift_ =
          spyro::ui_anchor::correctionAndReport({part->element, part->index}, part->anchor, frame_);
    }
    return lastShift_;
  }

private:
  spyro::ui_anchor::Frame frame_;
  std::uint32_t lastActor_ = 0;
  std::int32_t lastShift_ = 0;
};

} // namespace

Plan prepare(const RenderQueue &queue,
             uint32_t producerKey,
             const field_shaded_queue_recipe::Recipe &recipe) {
  Plan plan{};
  if (recipe.status == field_shaded_queue_recipe::Status::ValidEmpty) {
    return plan;
  }
  if (recipe.status != field_shaded_queue_recipe::Status::Ready || recipe.faces.empty()) {
    plan.status = Status::InvalidRecipe;
    return plan;
  }
  for (const auto &face : recipe.faces) {
    if ((face.vertexCount != 3u && face.vertexCount != 4u) ||
        !scene_painter_order::queuedWorld(face.worldBin, face.paintGroup, face.otBin).authored()) {
      plan.status = Status::InvalidOrder;
      return plan;
    }
  }
  plan.admission = painter_submission::preflight(
      queue, producerKey, recipe.faces.size(), scene_painter_order::kActorWorldTerrainDomain);
  if (!plan.admission.ready) {
    plan.status = Status::QueueCapacityExceeded;
    return plan;
  }
  plan.status = Status::Ready;
  return plan;
}

void submit(Core *core,
            RenderQueue &queue,
            uint32_t producerKey,
            const field_shaded_queue_recipe::Recipe &recipe,
            const Plan &plan) {
  if (plan.status != Status::Ready || recipe.status != field_shaded_queue_recipe::Status::Ready) {
    return;
  }
  const GpuState gpu = core->game->gpu;
  const int drawRight = wide_screen_space::drawAreaRight(core, gpu.s_da_x1);
  RenderQueue::PainterObjectScope painter(queue, producerKey);
  HudAnchor hudAnchor(core);
  for (const auto &face : recipe.faces) {
    core->rsub.diag.beginObject(face.actor);
    const std::int32_t anchorShift = hudAnchor.shiftFor(face.actor);
    int xs[4]{}, ys[4]{}, us[4]{}, vs[4]{};
    float screenX[4]{}, screenY[4]{}, depth[4]{};
    unsigned char red[4]{}, green[4]{}, blue[4]{};
    for (uint32_t i = 0; i < face.vertexCount; ++i) {
      xs[i] = face.vertices[i].sx + gpu.s_off_x + anchorShift;
      ys[i] = face.vertices[i].sy + gpu.s_off_y;
      screenX[i] = face.vertices[i].screenX + (float)(gpu.s_off_x + anchorShift);
      screenY[i] = face.vertices[i].screenY + (float)gpu.s_off_y;
      depth[i] = core->rsub.projParams.pzToOrd(face.vertices[i].viewZ);
      red[i] = (uint8_t)face.rgb[i];
      green[i] = (uint8_t)(face.rgb[i] >> 8);
      blue[i] = (uint8_t)(face.rgb[i] >> 16);
    }
    queue.emitOrQueue(core,
                      1,
                      RQ_WORLD,
                      RQ_OM_DEPTH,
                      face.vertexCount,
                      face.semiTransparent ? 1 : 0,
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
                      scene_painter_order::queuedWorld(face.worldBin, face.paintGroup, face.otBin));
  }
}

} // namespace spyro::field_shaded_queue_submitter

const char *spyro::field_shaded_queue_submitter::statusName(Status status) {
  switch (status) {
  case Status::Ready:
    return "Ready";
  case Status::ValidEmpty:
    return "ValidEmpty";
  case Status::InvalidRecipe:
    return "InvalidRecipe";
  case Status::InvalidOrder:
    return "InvalidOrder";
  case Status::QueueCapacityExceeded:
    return "QueueCapacityExceeded";
  }
  return "<unknown>";
}
