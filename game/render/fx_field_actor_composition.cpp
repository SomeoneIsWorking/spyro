#include "fx_field_actor_composition.h"

#include "actor_face_submitter.h"
#include "actor_recipe_capture.h"
#include "core.h"
#include "field_shaded_queue_recipe.h"
#include "field_shaded_queue_scene.h"
#include "field_shaded_queue_submitter.h"
#include "game.h"
#include "gpu_vk.h"
#include "guest_globals.h"
#include "producer_scope.h"
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
constexpr uint32_t kShadedProducer = 0x80022a2cu;

bool shadedReady(const spyro::field_shaded_queue_recipe::Recipe &recipe) {
  return recipe.status == spyro::field_shaded_queue_recipe::Status::Ready ||
         recipe.status == spyro::field_shaded_queue_recipe::Status::ValidEmpty;
}

bool drawAreaReady(const GpuState &gpu) {
  return gpu.s_da_x0 <= gpu.s_da_x1 && gpu.s_da_y0 <= gpu.s_da_y1;
}

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
  spyro::field_shaded_queue_recipe::Recipe shadedRecipe{};
  spyro::field_shaded_queue_submitter::Plan shadedPlan{};
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
    shadedRecipe = spyro::field_shaded_queue_recipe::derive(shadedFrame.input);
    if (!shadedReady(shadedRecipe)) {
      return spyro::refuse(kChannel,
                           kShadedProducer,
                           "shaded recipe={} actor=0x{:08X} primitive={} candidates={}",
                           spyro::field_shaded_queue_recipe::statusName(shadedRecipe.status),
                           shadedRecipe.firstUnsupportedActor,
                           shadedRecipe.firstUnsupportedPrimitive,
                           shadedRecipe.candidates);
    }
    shadedPlan = spyro::field_shaded_queue_submitter::prepare(queue, kShadedProducer, shadedRecipe);
    if (shadedPlan.status != spyro::field_shaded_queue_submitter::Status::Ready &&
        shadedPlan.status != spyro::field_shaded_queue_submitter::Status::ValidEmpty) {
      return spyro::refuse(kChannel,
                           kShadedProducer,
                           "shaded submission={} admission_ready={} queued={} existing_faces={}",
                           spyro::field_shaded_queue_submitter::statusName(shadedPlan.status),
                           shadedPlan.admission.ready,
                           shadedPlan.admission.queued,
                           shadedPlan.admission.existingFaces);
    }
  }

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
  if (!drawAreaReady(core->game->gpu)) {
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
  if (shadedPlan.status == spyro::field_shaded_queue_submitter::Status::Ready) {
    ProducerScope producer(&core->rsub.producerScope, kShadedProducer, "spriteq:world-shaded");
    spyro::field_shaded_queue_submitter::submit(
        core, queue, kShadedProducer, shadedRecipe, shadedPlan);
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
  if (composition.secondary) {
    // An empty picture is still an endpoint: the next frame can interpolate against a scene that
    // drew nothing.
    spyro_context(*core).secondaryActorTemporal.retain(std::move(secondaryFrame));
  }
  return {};
}

} // namespace

spyro::ProducerRefusal spyro_field_actor_composition_submit(Core *core,
                                                            FieldActorComposition composition) {
  const auto refusal = compose(core, composition);
  if (refusal && composition.secondary && core != nullptr && core->game != nullptr) {
    // This layer was asked to compose and something refused, so it published no picture and there
    // is no endpoint to interpolate toward. The interval that would have ended here is dropped
    // rather than spanning the gap. A shaded-only call must NOT refuse here: the secondary call
    // that shares its logic frame has already retained that frame's endpoint.
    spyro_context(*core).secondaryActorTemporal.refuse();
  }
  return refusal;
}
