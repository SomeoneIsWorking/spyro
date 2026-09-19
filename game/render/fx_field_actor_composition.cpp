#include "fx_field_actor_composition.h"

#include "actor_face_submitter.h"
#include "actor_recipe_capture.h"
#include "core.h"
#include "draw_area.h"
#include "field_shaded_queue_emit.h"
#include "field_shaded_queue_scene.h"
#include "game.h"
#include "gpu_vk.h"
#include "guest_globals.h"
#include "scene_painter_order.h"
#include "secondary_actor_emit.h"
#include "secondary_actor_scene.h"
#include "spyro_context.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <lucent/log.h>
#include <span>

namespace {

constexpr const char *kChannel = "fieldactors";
constexpr uint32_t kSecondaryProducer = spyro::secondary_actor_emit::kProducerKey;
constexpr uint32_t kShadedProducer = spyro::field_shaded_queue_emit::kProducerKey;

spyro::ProducerRefusal compose(Core *core, FieldActorComposition composition) {
  if (core == nullptr || core->game == nullptr) {
    return spyro::refuse(kChannel, kSecondaryProducer, "no core");
  }

  spyro::secondary_actor_scene::Frame secondaryFrame{};
  spyro::secondary_actor_emit::Prepared secondary{};
  if (composition.secondary) {
    const auto secondaryScene = spyro::secondary_actor_scene::prepare(core, secondaryFrame);
    if (secondaryScene != spyro::secondary_actor_scene::Status::Ready) {
      return spyro::refuse(kChannel,
                           kSecondaryProducer,
                           "secondary scene={} records={} shadows={}",
                           spyro::secondary_actor_scene::status_name(secondaryScene),
                           secondaryFrame.records.size(),
                           secondaryFrame.shadows.size());
    }
    spyro::secondary_actor_emit::recenter(*core, secondaryFrame);
    secondary = spyro::secondary_actor_emit::prepare(*core, core->game->rq, secondaryFrame);
    const auto &recipe = secondary.recipe;
    if (secondary.status != spyro::secondary_actor_emit::Status::Ready &&
        secondary.status != spyro::secondary_actor_emit::Status::ValidEmpty) {
      return spyro::refuse(
          kChannel,
          kSecondaryProducer,
          "secondary stage={} recipe={} reason={} record={} source_word={} "
          "control=0x{:08X} lighting=0x{:08X}/{} submission={}",
          spyro::actor_stage::name(secondary.status),
          spyro::secondary_actor_recipe::status_name(recipe.status),
          (uint32_t)recipe.firstReason,
          recipe.firstUnsupportedRecord,
          recipe.firstUnsupportedSourceWord,
          recipe.firstUnsupportedControl,
          recipe.firstUnsupportedLighting,
          spyro::face_light::status_name(recipe.firstLightingStatus),
          spyro::actor_face_submitter::statusName(secondary.plan.submitter.status));
    }
  }
  const auto &secondaryRecipe = secondary.recipe;

  RenderQueue &queue = core->game->rq;
  spyro::field_shaded_queue_scene::Frame shadedFrame{};
  spyro::field_shaded_queue_emit::Prepared shaded{};
  if (composition.shaded) {
    const int32_t clipRight =
        gpu_vk_wide_engine(core) ? std::max(512, gpu_vk_wide_engine_w(core)) : 512;
    const auto shadedScene = spyro::field_shaded_queue_scene::prepare(core, clipRight, shadedFrame);
    if (shadedScene != spyro::field_shaded_queue_scene::Status::Ready) {
      return spyro::refuse(kChannel,
                           kShadedProducer,
                           "shaded scene={} records={} shadows={}",
                           spyro::field_shaded_queue_scene::statusName(shadedScene),
                           shadedFrame.input.records.size(),
                           shadedFrame.shadows.size());
    }
    shaded = spyro::field_shaded_queue_emit::prepare(*core, queue, shadedFrame.input);
    if (shaded.status != spyro::field_shaded_queue_emit::Status::Ready &&
        shaded.status != spyro::field_shaded_queue_emit::Status::ValidEmpty) {
      return spyro::refuse(kChannel,
                           kShadedProducer,
                           "shaded stage={} recipe={} actor=0x{:08X} primitive={} candidates={} "
                           "submission={} admission_ready={} queued={} existing_faces={}",
                           spyro::actor_stage::name(shaded.status),
                           spyro::field_shaded_queue_recipe::statusName(shaded.recipe.status),
                           shaded.recipe.firstUnsupportedActor,
                           shaded.recipe.firstUnsupportedPrimitive,
                           shaded.recipe.candidates,
                           spyro::field_shaded_queue_submitter::statusName(shaded.submitter.status),
                           shaded.submitter.admission.ready,
                           shaded.submitter.admission.queued,
                           shaded.submitter.admission.existingFaces);
    }
  }
  const auto &shadedRecipe = shaded.recipe;

  // Both scene preparations read the same cursor. Rebase the second commit to the first call's
  // output, preserving the retail one-list transaction instead of letting the second call overwrite
  // the first shadow entries. Validate the combined destination before any guest state changes.
  // With the secondary call absent its frame is empty and its cursor is zero, so the shaded call
  // keeps the list start its own preparation resolved rather than being rebased onto nothing.
  const uint32_t shadedShadowCursor =
      composition.secondary
          ? secondaryFrame.shadowCursor + static_cast<uint32_t>(secondaryFrame.shadows.size()) * 8u
          : shadedFrame.shadowCursor;
  if (!shadedFrame.shadows.empty() &&
      !spyro::actor_recipe_capture::physical_span(
          shadedShadowCursor + static_cast<uint32_t>(shadedFrame.shadows.size()) * 8u - 8u, 8u)) {
    return spyro::refuse(kChannel,
                         kSecondaryProducer,
                         "combined shadow cursor=0x{:08X} shadows={}",
                         shadedShadowCursor,
                         shadedFrame.shadows.size());
  }
  shadedFrame.shadowCursor = shadedShadowCursor;

  std::array<PainterObjectBatchEntry, 2> entries{{
      {kSecondaryProducer,
       secondaryRecipe.faces.size(),
       spyro::scene_painter_order::kActorWorldTerrainDomain},
      {kShadedProducer,
       shadedRecipe.faces.size(),
       spyro::scene_painter_order::kActorWorldTerrainDomain},
  }};
  size_t entryCount = 0;
  for (const auto &entry : entries) {
    if (entry.new_faces != 0) {
      entries[entryCount++] = entry;
    }
  }
  if (entryCount != 0) {
    const auto admission = queue.preflightPainterObjectBatch(
        std::span<const PainterObjectBatchEntry>(entries.data(), entryCount));
    if (!admission.accepted()) {
      return spyro::refuse(kChannel,
                           kSecondaryProducer,
                           "batch admission={} item={} existing_objects={} existing_faces={}",
                           (uint32_t)admission.refusal,
                           admission.refusal_item,
                           admission.existing_objects,
                           admission.existing_faces);
    }
  }
  if (!spyro::draw_area::ready(core->game->gpu)) {
    return spyro::refuse(kChannel,
                         kSecondaryProducer,
                         "draw area x=[{},{}] y=[{},{}] is inverted",
                         core->game->gpu.s_da_x0,
                         core->game->gpu.s_da_x1,
                         core->game->gpu.s_da_y0,
                         core->game->gpu.s_da_y1);
  }

  // Every source and queue check is complete. The commits are adjacent and precede either
  // publication, so a refusal cannot leave a partially updated shared shadow list.
  if (composition.secondary) {
    spyro::secondary_actor_scene::commit(core, secondaryFrame);
  }
  if (composition.shaded) {
    spyro::field_shaded_queue_scene::commit(core, shadedFrame);
  }
  if (composition.secondary) {
    spyro::secondary_actor_emit::publish(*core, queue, secondary);
    // An empty picture is still an endpoint: the next frame can interpolate against a scene that
    // drew nothing. A frame this layer did not compose at all is a gap, refused below.
  }
  if (composition.shaded) {
    spyro::field_shaded_queue_emit::publish(*core, queue, shaded);
  }
  lucent::debug("fieldactors",
                "PASS secondary_faces={} face_light={} shaded_faces={} secondary_shadows={} "
                "shaded_shadows={} shadow_cursor=0x{:08X}",
                secondaryRecipe.faces.size(),
                secondaryRecipe.faceLightFaces,
                shadedRecipe.faces.size(),
                secondaryFrame.shadows.size(),
                shadedFrame.shadows.size(),
                shadedShadowCursor + static_cast<uint32_t>(shadedFrame.shadows.size()) * 8u);
  // An empty picture is still an endpoint: the next frame can interpolate against a scene that
  // drew nothing.
  if (composition.secondary) {
    spyro_context(*core).secondaryActorTemporal.retain(std::move(secondaryFrame));
  }
  if (composition.shaded) {
    spyro_context(*core).shadedQueueTemporal.retain(std::move(shadedFrame.input));
  }
  return {};
}

} // namespace

spyro::ProducerRefusal spyro_field_actor_composition_submit(Core *core,
                                                            FieldActorComposition composition) {
  const auto refusal = compose(core, composition);
  if (!refusal || core == nullptr || core->game == nullptr) {
    return refusal;
  }
  // A layer this call was asked to compose published no picture, so there is no endpoint to
  // interpolate toward and the interval that would have ended here is dropped rather than spanning
  // the gap. Only the layers this call owns are refused: the other call that shares this logic
  // frame may already have retained its own endpoint.
  if (composition.secondary) {
    spyro_context(*core).secondaryActorTemporal.refuse();
  }
  if (composition.shaded) {
    spyro_context(*core).shadedQueueTemporal.refuse();
  }
  return refusal;
}
