#include "temporal_scene.h"

#include "terrain_emit.h"

#include "actor_stage.h"
#include "actor_temporal.h"
#include "core.h"
#include "field_shaded_queue_emit.h"
#include "field_shaded_queue_temporal.h"
#include "fx_actor_draw.h"
#include "fx_paired_actor.h"
#include "game.h"
#include "painter_object_layer.h"
#include "render_queue.h"
#include "secondary_actor_emit.h"
#include "secondary_actor_temporal.h"
#include "spyro_context.h"
#include "temporal_scene_source.h"

#include <cstdlib>
#include <lucent/log.h>
#include <vector>

namespace {

bool producerItem(const RqItem &item, uint32_t producer) {
  return item.layer == RQ_WORLD && item.has_xyf && item.painter_object == producer;
}

// Every self-contained layer reconstructs through identical steps over different endpoint types:
// pair against the previous frame, rebuild the recipe, publish into the destination. Only the
// history, its census and the log channel differ, so the steps are written once and the layer is
// the argument. An admitted source that then refuses presentation is a contradiction — admission
// replayed this exact call — so it terminates rather than presenting a frame missing one layer.
template <class History>
void reconstructLayer(Core &core, const History &history, float t, const char *channel) {
  if (!history.eligible()) {
    return;
  }
  typename History::Census census{};
  const auto status = core.game && core.game->rqRedirect
                          ? history.emit(core, *core.game->rqRedirect, t, census)
                          : spyro::actor_stage::Temporal::NoEndpoints;
  if (!spyro::actor_stage::completed(status)) {
    lucent::error(channel,
                  "FATAL: admitted source refused presentation t={} status={}",
                  t,
                  spyro::actor_stage::name(status));
    std::abort();
  }
}

// The same replay, into the admission sink instead of the destination, where a refusal is the
// answer being looked for rather than a contradiction.
template <class History>
std::function<bool(RenderQueue &, float)>
layerSampler(Core &core, const History &history, const char *channel) {
  return [&core, &history, channel](RenderQueue &sink, float t) {
    typename History::Census census{};
    const auto status = history.emit(core, sink, t, census);
    if (spyro::actor_stage::completed(status)) {
      return true;
    }
    lucent::debug(channel,
                  "preflight refused t={} status={} actors={} interpolated={}",
                  t,
                  spyro::actor_stage::name(status),
                  census.actors,
                  census.interpolated);
    return false;
  };
}

class SpyroTemporalScene final : public TemporalSceneSource {
public:
  explicit SpyroTemporalScene(Game &game) : game_(game) {}

  bool eligible(const Core &core) const override {
    if (&core != &game_.core) {
      return false;
    }
    const auto &context = spyro_context(core);
    return context.pairedActor.temporal_eligible || context.worldTemporal.eligible() ||
           context.actorTemporal.eligible() || context.secondaryActorTemporal.eligible() ||
           context.shadedQueueTemporal.eligible() || context.terrainTemporal.eligible();
  }

  bool owns(const RqItem &item) const override {
    const auto &context = spyro_context(game_.core);
    return (context.pairedActor.temporal_eligible && producerItem(item, 0x80023AC4u)) ||
           (context.worldTemporal.eligible() &&
            producerItem(item, spyro::world_temporal::kProducerKey)) ||
           (context.actorTemporal.eligible() &&
            producerItem(item, spyro::actor_draw::kProducerKey)) ||
           (context.secondaryActorTemporal.eligible() &&
            producerItem(item, spyro::secondary_actor_emit::kProducerKey)) ||
           (context.shadedQueueTemporal.eligible() &&
            producerItem(item, spyro::field_shaded_queue_emit::kProducerKey)) ||
           (context.terrainTemporal.eligible() &&
            producerItem(item, spyro::terrain_emit::kProducerKey));
  }

  void reconstruct(Core &core, float t) override {
    const auto &context = spyro_context(core);
    if (context.pairedActor.temporal_eligible) {
      spyro_paired_actor_fps60_world_pass(&core, t);
    }
    if (context.worldTemporal.eligible() &&
        (!core.game || !core.game->rqRedirect ||
         !context.worldTemporal.emit(core, *core.game->rqRedirect, t))) {
      lucent::error("worldtemporal", "FATAL: admitted world source refused presentation t={}", t);
      std::abort();
    }
    reconstructLayer(core, context.actorTemporal, t, "actortemporal");
    reconstructLayer(core, context.secondaryActorTemporal, t, "secondarytemporal");
    reconstructLayer(core, context.shadedQueueTemporal, t, "shadedtemporal");
    reconstructLayer(core, context.terrainTemporal, t, "terraintemporal");
  }

  void rotate(Core &core) override {
    spyro_paired_actor_fps60_rotate(&core);
    spyro_context(core).worldTemporal.rotate();
    spyro_context(core).actorTemporal.rotate();
    spyro_context(core).secondaryActorTemporal.rotate();
    spyro_context(core).shadedQueueTemporal.rotate();
    spyro_context(core).terrainTemporal.rotate();
  }

private:
  Game &game_;
};

} // namespace

// The same source emitters and queue planner as presentation, with an isolated sink and no
// presentation counters. A midpoint refusal rejects the complete world interval.
SpyroTemporalSceneAdmission::SpyroTemporalSceneAdmission() = default;
SpyroTemporalSceneAdmission::~SpyroTemporalSceneAdmission() = default;

bool SpyroTemporalSceneAdmission::interval(Core &core,
                                           const char *label,
                                           const std::function<bool(RenderQueue &, float)> &emit) {
  if (!sink_) {
    sink_ = std::make_unique<RenderQueue>(RenderQueue::Observation::Admission);
  }
  auto &sink = *sink_;
  sink.game = core.game;
  DisplayPassGuard readonly(core.rsub.mode);
  for (float t : {0.0f, 0.5f, 1.0f}) {
    sink.reset();
    if (!emit(sink, t)) {
      return false;
    }
    sink.finalize(&core, label);
    std::vector<const RqItem *> stream;
    stream.reserve(static_cast<size_t>(sink.n));
    for (int i = 0; i < sink.n; ++i) {
      stream.push_back(&sink.items[i]);
    }
    // emitItemStream does no playback for an empty source. The painter planner deliberately
    // refuses a stream with zero grouped faces, so it applies only when there is output to replay.
    if (stream.empty()) {
      continue;
    }
    const auto plan = planPainterItemStream(stream);
    lucent::debug("worldtemporal",
                  "preflight {} painter t={} refusal={} item={} partitioned={}/{}",
                  label,
                  t,
                  static_cast<unsigned>(plan.stats.refusal),
                  plan.stats.refusal_item,
                  plan.stats.partitioned_items,
                  stream.size());
    if (!plan.accepted() || plan.stats.partitioned_items != stream.size()) {
      return false;
    }
  }
  return true;
}

bool SpyroTemporalSceneAdmission::world(Core &core, bool paired) {
  const auto &context = spyro_context(core);
  return interval(
      core, "world-temporal-preflight", [&context, paired, &core](RenderQueue &sink, float t) {
        if (paired &&
            spyro_paired_actor_rebuild_sample(
                &core, sink, context.pairedActor.previous, context.pairedActor.current, t) ==
                SpyroPairedRebuildResult::Refused) {
          lucent::debug("worldtemporal", "preflight refused stage=paired t={}", t);
          return false;
        }
        return context.worldTemporal.emit(core, sink, t);
      });
}

bool SpyroTemporalSceneAdmission::actors(Core &core) {
  const auto &history = spyro_context(core).actorTemporal;
  if (!history.paired()) {
    return false;
  }
  return interval(core, "actor-temporal-preflight", layerSampler(core, history, "actortemporal"));
}

bool SpyroTemporalSceneAdmission::secondaryActors(Core &core) {
  const auto &history = spyro_context(core).secondaryActorTemporal;
  if (!history.paired()) {
    return false;
  }
  return interval(
      core, "secondary-actor-temporal-preflight", layerSampler(core, history, "secondarytemporal"));
}

bool SpyroTemporalSceneAdmission::shadedQueue(Core &core) {
  const auto &history = spyro_context(core).shadedQueueTemporal;
  if (!history.paired()) {
    return false;
  }
  return interval(
      core, "shaded-queue-temporal-preflight", layerSampler(core, history, "shadedtemporal"));
}

bool SpyroTemporalSceneAdmission::terrain(Core &core) {
  const auto &history = spyro_context(core).terrainTemporal;
  if (!history.paired()) {
    return false;
  }
  return interval(
      core, "terrain-temporal-preflight", layerSampler(core, history, "terraintemporal"));
}

void spyro_temporal_scene_begin(
    Core &core, uint64_t scene, bool pairedScene, bool reference, bool active) {
  auto &context = spyro_context(core);
  context.worldTemporal.begin(scene, reference, active);
  context.actorTemporal.begin(scene, reference, active);
  context.secondaryActorTemporal.begin(scene, reference, active);
  context.shadedQueueTemporal.begin(scene, reference, active);
  context.terrainTemporal.begin(scene, reference, active);
  spyro_paired_actor_frame_begin(context.pairedActor, pairedScene, reference, active);
}

void spyro_temporal_scene_prepare(Core &core) {
  auto &context = spyro_context(core);
  auto &paired = context.pairedActor;
  paired.temporal_eligible = false;
  context.worldTemporal.admit(false);
  // Regular actors carry their own endpoints, so their admission neither depends on the world
  // source nor is lost to one of its refusals below.
  context.actorTemporal.admit(context.temporalAdmission.actors(core));
  lucent::debug("actortemporal",
                "actor interval frame={} admitted={}",
                context.actorTemporal.frameSerial(),
                context.actorTemporal.eligible());
  context.secondaryActorTemporal.admit(context.temporalAdmission.secondaryActors(core));
  lucent::debug("secondarytemporal",
                "secondary actor interval frame={} admitted={}",
                context.secondaryActorTemporal.frameSerial(),
                context.secondaryActorTemporal.eligible());
  context.shadedQueueTemporal.admit(context.temporalAdmission.shadedQueue(core));
  lucent::debug("shadedtemporal",
                "shaded queue interval frame={} admitted={}",
                context.shadedQueueTemporal.frameSerial(),
                context.shadedQueueTemporal.eligible());
  context.terrainTemporal.admit(context.temporalAdmission.terrain(core));
  lucent::debug("terraintemporal",
                "terrain interval frame={} admitted={}",
                context.terrainTemporal.frameSerial(),
                context.terrainTemporal.eligible());
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
  context.worldTemporal.admit(context.temporalAdmission.world(core, paired.temporal_eligible));
  lucent::debug("worldtemporal",
                "world interval frame={} admitted={} paired={}",
                context.worldTemporal.frameSerial(),
                context.worldTemporal.eligible(),
                paired.temporal_eligible);
}

std::unique_ptr<TemporalSceneSource> spyro_temporal_scene_source(Game &game) {
  return std::make_unique<SpyroTemporalScene>(game);
}
