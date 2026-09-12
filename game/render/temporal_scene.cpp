#include "temporal_scene.h"

#include "core.h"
#include "fx_paired_actor.h"
#include "game.h"
#include "painter_object_layer.h"
#include "render_queue.h"
#include "spyro_context.h"
#include "temporal_scene_source.h"

#include <cstdlib>
#include <lucent/log.h>
#include <vector>

namespace {

bool producerItem(const RqItem &item, uint32_t producer) {
  return item.layer == RQ_WORLD && item.has_xyf && item.painter_object == producer;
}

class SpyroTemporalScene final : public TemporalSceneSource {
public:
  explicit SpyroTemporalScene(Game &game) : game_(game) {}

  bool eligible(const Core &core) const override {
    if (&core != &game_.core) {
      return false;
    }
    const auto &context = spyro_context(core);
    return context.pairedActor.temporal_eligible || context.worldTemporal.eligible;
  }

  bool owns(const RqItem &item) const override {
    const auto &context = spyro_context(game_.core);
    return (context.pairedActor.temporal_eligible && producerItem(item, 0x80023AC4u)) ||
           (context.worldTemporal.eligible &&
            producerItem(item, spyro::world_temporal::kProducerKey));
  }

  void reconstruct(Core &core, float t) override {
    const auto &context = spyro_context(core);
    if (context.pairedActor.temporal_eligible) {
      spyro_paired_actor_fps60_world_pass(&core, t);
    }
    if (context.worldTemporal.eligible &&
        (!core.game || !core.game->rqRedirect ||
         !context.worldTemporal.emit(core, *core.game->rqRedirect, t))) {
      lucent::error("worldtemporal", "FATAL: admitted world source refused presentation t={}", t);
      std::abort();
    }
  }

  void rotate(Core &core) override {
    spyro_paired_actor_fps60_rotate(&core);
    spyro_context(core).worldTemporal.rotate();
  }

private:
  Game &game_;
};

} // namespace

// The same source emitters and queue planner as presentation, with an isolated sink and no
// presentation counters. A midpoint refusal rejects the complete world interval.
SpyroTemporalSceneAdmission::SpyroTemporalSceneAdmission() = default;
SpyroTemporalSceneAdmission::~SpyroTemporalSceneAdmission() = default;

bool SpyroTemporalSceneAdmission::world(Core &core, bool paired) {
  if (!sink_) {
    sink_ = std::make_unique<RenderQueue>(RenderQueue::Observation::Admission);
  }
  auto &sink = sink_;
  sink->game = core.game;
  const auto &context = spyro_context(core);
  DisplayPassGuard readonly(core.rsub.mode);
  for (float t : {0.0f, 0.5f, 1.0f}) {
    sink->reset();
    if (paired && spyro_paired_actor_rebuild_sample(
                      &core, *sink, context.pairedActor.previous, context.pairedActor.current, t) ==
                      SpyroPairedRebuildResult::Refused) {
      lucent::debug("worldtemporal", "preflight refused stage=paired t={}", t);
      return false;
    }
    if (!context.worldTemporal.emit(core, *sink, t)) {
      return false;
    }
    sink->finalize(&core, "world-temporal-preflight");
    std::vector<const RqItem *> stream;
    stream.reserve(static_cast<size_t>(sink->n));
    for (int i = 0; i < sink->n; ++i) {
      stream.push_back(&sink->items[i]);
    }
    // emitItemStream does no playback for an empty source. The painter planner deliberately
    // refuses a stream with zero grouped faces, so it applies only when there is output to replay.
    if (!stream.empty()) {
      const auto plan = planPainterItemStream(stream);
      lucent::debug("worldtemporal",
                    "preflight painter t={} refusal={} item={} partitioned={}/{}",
                    t,
                    static_cast<unsigned>(plan.stats.refusal),
                    plan.stats.refusal_item,
                    plan.stats.partitioned_items,
                    stream.size());
      if (!plan.accepted() || plan.stats.partitioned_items != stream.size()) {
        return false;
      }
    }
  }
  return true;
}

void spyro_temporal_scene_begin(
    Core &core, uint64_t scene, bool pairedScene, bool reference, bool active) {
  auto &context = spyro_context(core);
  context.worldTemporal.begin(scene, reference, active);
  spyro_paired_actor_frame_begin(context.pairedActor, pairedScene, reference, active);
}

void spyro_temporal_scene_prepare(Core &core) {
  auto &context = spyro_context(core);
  auto &paired = context.pairedActor;
  paired.temporal_eligible = false;
  context.worldTemporal.eligible = false;
  if (paired.was_fps60_active && paired.endpoints_compatible) {
    // Preserve paired-only admission when the world lacks a complete matching source.
    paired.temporal_eligible = spyro_paired_actor_fps60_eligible(paired);
  }
  const char *why = nullptr;
  if (!context.worldTemporal.materializePending(core, why)) {
    lucent::debug(
        "worldtemporal", "REFUSED frame={} reason={}", context.worldTemporal.frameSerial(), why);
    return;
  }
  if (!context.worldTemporal.compatible(core, why)) {
    lucent::debug(
        "worldtemporal", "REFUSED frame={} reason={}", context.worldTemporal.frameSerial(), why);
    return;
  }
  if (paired.temporal_eligible &&
      !context.worldTemporal.camerasMatch(paired.previous, paired.current)) {
    lucent::debug("worldtemporal", "REFUSED joint camera/frame provenance");
    return;
  }
  context.worldTemporal.eligible = context.temporalAdmission.world(core, paired.temporal_eligible);
  lucent::debug("worldtemporal",
                "world interval frame={} admitted={} paired={}",
                context.worldTemporal.frameSerial(),
                context.worldTemporal.eligible,
                paired.temporal_eligible);
}

std::unique_ptr<TemporalSceneSource> spyro_temporal_scene_source(Game &game) {
  return std::make_unique<SpyroTemporalScene>(game);
}
