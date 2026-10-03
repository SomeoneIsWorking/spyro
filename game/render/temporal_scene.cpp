#include "temporal_scene.h"

#include "terrain_emit.h"

#include "field_2d_overlay_recipe.h"

#include "actor_stage.h"
#include "actor_temporal.h"
#include "core.h"
#include "field_2d_overlay.h"
#include "field_shaded_queue_emit.h"
#include "field_shaded_queue_temporal.h"
#include "fx_actor_draw.h"
#include "fx_paired_actor.h"
#include "game.h"
#include "in_between_strategy.h"
#include "instance_pairing.h"
#include "interp_census.h"
#include "painter_object_layer.h"
#include "render_queue.h"
#include "secondary_actor_emit.h"
#include "secondary_actor_temporal.h"
#include "spyro_context.h"

#include <cstdlib>
#include <lucent/log.h>
#include <vector>

namespace {

// The census owner lives in the `spyro` project namespace, and this scene composes owners from
// several of them. A namespace ALIAS rather than a `using`, because Clang rejects a
// using-declaration that names a namespace, and rather than a repeated `spyro::` prefix on eleven
// call sites.
namespace interp_census = spyro::interp_census;

// A layer's own pairing walk in the census owner's terms. This is a field-for-field translation and
// nothing else: the owner is `instance_pairing::walk`, and its invariant is that `interpolated +
// noPredecessor + incompatible + refused == records` whenever the layer ran. Reporting a layer's
// records as interpolated without the other three would be reporting the walk's own definition of
// success rather than its measurement, so all four travel together.
interp_census::LayerCensus censusOf(const spyro::instance_pairing::Census &census) {
  return {
      .records = census.actors,
      .interpolated = census.interpolated,
      .noPredecessor = census.unpaired(),
      .incompatible = census.incompatible,
      .samplerRefused = census.refused,
      .ran = true,
  };
}

bool producerItem(const RqItem &item, uint32_t producer) {
  return item.layer == RQ_WORLD && item.has_xyf && item.painter_object == producer;
}

// Every self-contained layer reconstructs through identical steps over different endpoint types:
// pair against the previous frame, rebuild the recipe, publish into the destination. Only the
// history, its census and the log channel differ, so the steps are written once and the layer is
// the argument. An admitted source that then refuses presentation is a contradiction — admission
// replayed this exact call — so it terminates rather than presenting a frame missing one layer.
template <class History>
void reconstructLayer(Core &core,
                      const History &history,
                      float t,
                      const char *channel,
                      interp_census::Category category,
                      bool account) {
  if (!history.eligible()) {
    return;
  }
  typename History::Census census{};
  const auto status = core.game && core.game->rqRedirect
                          ? history.emit(core, *core.game->rqRedirect, t, census)
                          : spyro::actor_stage::Temporal::NoEndpoints;
  if (account) {
    spyro_context(core).interpCensus.recordLayer(category, censusOf(census));
  }
  if (!spyro::actor_stage::completed(status)) {
    lucent::error(channel,
                  "FATAL: admitted source refused presentation t={} status={}",
                  t,
                  spyro::actor_stage::name(status));
    std::abort();
  }
}

// The 2D overlay publishes RQ_HUD items through the producers' own submitters rather than a painter
// plan, so it takes the target queue as an argument instead of the redirect. Same rule as
// reconstructLayer: an admitted source that then refuses presentation is a contradiction, because
// admission replayed this exact call, so it terminates rather than presenting a frame missing a
// layer.
void reconstructOverlay(Core &core,
                        const spyro::field_2d_overlay::History &history,
                        float t,
                        bool account) {
  if (!history.eligible()) {
    return;
  }
  spyro::field_2d_overlay::Census census{};
  const auto status =
      core.game && core.game->rqRedirect
          ? history.emit(core, *core.game->rqRedirect, static_cast<double>(t), census)
          : spyro::actor_stage::Temporal::NoEndpoints;
  if (account) {
    spyro_context(core).interpCensus.recordLayer(interp_census::Category::Hud, censusOf(census));
  }
  if (!spyro::actor_stage::completed(status)) {
    lucent::error("field2dtemporal",
                  "FATAL: admitted 2D overlay refused presentation t={} status={}",
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

class SpyroTemporalScene final : public InBetweenStrategy {
public:
  explicit SpyroTemporalScene(Game &game) : game_(game) {}

  bool eligible(const Core &core) const override {
    if (&core != &game_.core) {
      return false;
    }
    const auto &context = spyro_context(core);
    return context.pairedActor.temporal_eligible || context.worldTemporal.eligible() ||
           context.actorTemporal.eligible() || context.secondaryActorTemporal.eligible() ||
           context.shadedQueueTemporal.eligible() || context.terrainTemporal.eligible() ||
           context.overlayTemporal.eligible();
  }

  // THE 2D OVERLAY IS CLAIMED BY LAYER, NOT BY PAINTER OBJECT, AND THE REASON IS THE FRAMEWORK'S.
  // Every other predicate here asks for a `painter_object`, because a native producer publishes one
  // and the item carries its producer. A 2D item cannot: the painter planner validates every item
  // that has a painter object and refuses any that is not RQ_WORLD with RQ_OM_DEPTH
  // (external/psxport/runtime/psx/painter_object_layer.cpp:12-14, `validateFace`), and
  // `RenderQueue::emitItemStream` aborts the frame on a refused plan
  // (external/psxport/runtime/psx/render_queue.cpp:522-537). A 2D item is RQ_HUD by definition, so
  // giving it a painter object would abort the product. It is therefore identified by its layer,
  // and that is only sound because the source is admissible in a FIELD arm alone — the front-end
  // title menu, the level-transition tally and the dragon burst also emit RQ_HUD, and none of them
  // is ever on a frame whose overlay interval was admitted.
  //
  // MEASURED, and this is what makes "only sound" a fact rather than a hope. Over the
  // field-weighted route this layer-3 census saw 794 RQ_HUD groups, and they split by gamestate as
  // 738 on GS_TitleScreen, 27 on GS_Cutscene and 29 on GS_Playing. Every one of the 29 field-frame
  // items is a single full-width 225-tall quad at y=8 — this owner's own fade, `setXYWH(f4, 0, 8,
  // 512, 240-16)` widened to the live render width — and 28 of them are TIER1, which is this
  // source's own reconstruction. So on a field frame NOTHING ELSE reaches RQ_HUD, and the
  // layer-scoped claim competes with nobody. The other four first-party RQ_HUD publishers are all
  // off the field arm by construction: `fx_title_menu.cpp:246` and the stage-13 renderer's
  // screen-class sprite path (`fx_sprite_queue.cpp:426`, reached only from `stage13Mode3Render` at
  // fx_sprite_queue.cpp:695 under `kStageFrontEnd`), plus the level-transition tally and the dragon
  // burst.
  static bool overlayItem(const RqItem &item) {
    return item.layer == RQ_HUD;
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
            producerItem(item, spyro::terrain_emit::kProducerKey)) ||
           (context.overlayTemporal.eligible() && overlayItem(item));
  }

  void reconstruct(Core &core, float t) override {
    auto &census = spyro_context(core).interpCensus;
    const bool inBetween = census.isInBetween(t);
    const auto &context = spyro_context(core);
    if (context.pairedActor.temporal_eligible) {
      spyro_paired_actor_fps60_world_pass(&core, t);
    }
    // THE CAMERA ROW. One camera sample per in-between present, and it is compatible exactly when
    // the world source admitted this interval, because `worldTemporal::compatible` is what compares
    // the two endpoints' camera and frame provenance and the joint camera check below is part of
    // the same admission. A paired present with the world source absent is the one case the world's
    // answer does not cover, so it reports the joint mismatch rather than borrowing a pass.
    if (inBetween) {
      census.cameraSample(context.worldTemporal.eligible(),
                          context.worldTemporal.eligible() ? interp_census::Reason::None
                                                           : interp_census::Reason::CameraMismatch);
    }
    if (context.worldTemporal.eligible() &&
        (!core.game || !core.game->rqRedirect ||
         !context.worldTemporal.emit(core, *core.game->rqRedirect, t))) {
      lucent::error("worldtemporal", "FATAL: admitted world source refused presentation t={}", t);
      std::abort();
    }
    reconstructLayer(core,
                     context.actorTemporal,
                     t,
                     "actortemporal",
                     interp_census::Category::Actors,
                     inBetween);
    reconstructLayer(core,
                     context.secondaryActorTemporal,
                     t,
                     "secondarytemporal",
                     interp_census::Category::Actors,
                     inBetween);
    reconstructLayer(core,
                     context.shadedQueueTemporal,
                     t,
                     "shadedtemporal",
                     interp_census::Category::World,
                     inBetween);
    reconstructLayer(core,
                     context.terrainTemporal,
                     t,
                     "terraintemporal",
                     interp_census::Category::World,
                     inBetween);
    reconstructOverlay(core, context.overlayTemporal, t, inBetween);
    // LAST, after every layer above has published into the redirect queue. The presenter's sink is
    // emptied before this call, so the queue is the reconstruction's own output only at its end —
    // walking it at the start counted zero reconstructed items while the run was in fact emitting
    // millions, which is the "confident answer about the wrong subject" this census exists to
    // avoid.
    census.reconstruct(core, t);
  }

  void rotate(Core &core) override {
    // One logic frame closes here: both of its presents have run, so the reconstructed items and
    // every layer's record census are all in. The next frame's admission opens the next one.
    spyro_context(core).interpCensus.endLogicFrame();
    spyro_paired_actor_fps60_rotate(&core);
    spyro_context(core).worldTemporal.rotate();
    spyro_context(core).actorTemporal.rotate();
    spyro_context(core).secondaryActorTemporal.rotate();
    spyro_context(core).shadedQueueTemporal.rotate();
    spyro_context(core).terrainTemporal.rotate();
    spyro_context(core).overlayTemporal.rotate();
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

// THE 2D OVERLAY IS PREFLIGHTED WITHOUT THE PAINTER PLANNER, AND THAT IS NOT A SHORTCUT. The
// planner exists to group world faces into painter objects: it refuses a stream with no grouped
// face at all
// (`PainterObjectRefusal::Empty`), and it refuses any item carrying a painter object that is not
// RQ_WORLD with RQ_OM_DEPTH. This layer's items are RQ_HUD by definition, so the planner has
// nothing to say about them and would refuse the interval for a reason that says nothing about the
// overlay. What it DID protect — a midpoint that cannot be queued, and a midpoint presenting a
// degenerate bar or quad — the producers' own submitters check, and this runs them at all three
// samples.
bool SpyroTemporalSceneAdmission::overlay(Core &core) {
  const auto &history = spyro_context(core).overlayTemporal;
  if (!history.paired()) {
    return false;
  }
  if (!sink_) {
    sink_ = std::make_unique<RenderQueue>(RenderQueue::Observation::Admission);
  }
  auto &sink = *sink_;
  sink.game = core.game;
  DisplayPassGuard readonly(core.rsub.mode);
  for (float t : {0.0f, 0.5f, 1.0f}) {
    sink.reset();
    spyro::field_2d_overlay_recipe::Census census{};
    if (!spyro::actor_stage::completed(history.emit(core, sink, static_cast<double>(t), census))) {
      lucent::debug("field2dtemporal", "preflight refused t={}", t);
      return false;
    }
    lucent::debug("field2dtemporal",
                  "preflight t={} draws={} interpolated={} incompatible={} absent={}",
                  t,
                  census.actors,
                  census.interpolated,
                  census.incompatible,
                  census.absent);
  }
  return true;
}

void spyro_temporal_scene_begin(
    Core &core, uint64_t scene, bool pairedScene, bool reference, bool active) {
  auto &context = spyro_context(core);
  context.worldTemporal.begin(scene, reference, active);
  context.actorTemporal.begin(scene, reference, active);
  context.secondaryActorTemporal.begin(scene, reference, active);
  context.shadedQueueTemporal.begin(scene, reference, active);
  context.terrainTemporal.begin(scene, reference, active);
  context.overlayTemporal.begin(scene, reference, active);
  spyro_paired_actor_frame_begin(context.pairedActor, pairedScene, reference, active);
}

void spyro_temporal_scene_prepare(Core &core) {
  auto &context = spyro_context(core);
  // The denominator, taken after every producer has submitted: one walk of the captured queue,
  // partitioned by the category its publisher belongs to.
  context.interpCensus.beginLogicFrame(core);
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
  // The overlay is admitted LAST and independently: it is the only layer whose queue items carry no
  // painter object, so nothing above can vouch for it, and a refusal here must cost this layer's
  // in-between quads and nothing else.
  context.overlayTemporal.admit(context.temporalAdmission.overlay(core));
  // The endpoint's own part census rides on the admission line because this is the ONE line per
  // logic frame, and it is what gives the per-emit census its denominator. Measured over the
  // field-weighted route: 515 admitted intervals, 2,575 emit calls (three preflight samples plus
  // the presenter's two in-betweens per interval — 1,545 + 1,030, exact), and 501 of the 515
  // endpoints empty. `endpoint=0/0/0 gates=0/0/1` on an empty one says WHY: the guest's three call
  // sites decided that, and the derivation reproduced the decision. Without it the 501 reads as a
  // failure rate; with it, it reads as what it is.
  const auto *endpoint = context.overlayTemporal.current();
  using PartCounts = spyro::field_2d_overlay_recipe::PartCounts;
  const auto parts =
      endpoint ? spyro::field_2d_overlay_recipe::countParts(*endpoint) : PartCounts{};
  lucent::debug("field2dtemporal",
                "2D overlay interval frame={} admitted={} endpoint={}/{}/{} gates={}{}{}",
                context.overlayTemporal.frameSerial(),
                context.overlayTemporal.eligible(),
                parts.fade,
                parts.border,
                parts.sprites,
                endpoint && endpoint->gates.fade ? 1 : 0,
                endpoint && endpoint->gates.border ? 1 : 0,
                endpoint && endpoint->gates.sprites ? 1 : 0);
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

std::unique_ptr<InBetweenStrategy> spyro_temporal_scene_source(Game &game) {
  return std::make_unique<SpyroTemporalScene>(game);
}
