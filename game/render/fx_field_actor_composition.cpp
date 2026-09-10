#include "fx_field_actor_composition.h"

#include "actor_face_submitter.h"
#include "actor_recipe_capture.h"
#include "core.h"
#include "field_shaded_queue_recipe.h"
#include "field_shaded_queue_scene.h"
#include "field_shaded_queue_submitter.h"
#include "game.h"
#include "gpu_vk.h"
#include "producer_scope.h"
#include "scene_painter_order.h"
#include "secondary_actor_recipe.h"
#include "secondary_actor_scene.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <lucent/log.h>
#include <span>

namespace {

constexpr uint32_t kSecondaryProducer = 0x80020f34u;
constexpr uint32_t kShadedProducer = 0x80022a2cu;

bool secondaryReady(const spyro::secondary_actor_recipe::Recipe &recipe) {
  return recipe.status == spyro::secondary_actor_recipe::Status::Ready ||
         recipe.status == spyro::secondary_actor_recipe::Status::ValidEmpty;
}

bool shadedReady(const spyro::field_shaded_queue_recipe::Recipe &recipe) {
  return recipe.status == spyro::field_shaded_queue_recipe::Status::Ready ||
         recipe.status == spyro::field_shaded_queue_recipe::Status::ValidEmpty;
}

bool drawAreaReady(const GpuState &gpu) {
  return gpu.s_da_x0 <= gpu.s_da_x1 && gpu.s_da_y0 <= gpu.s_da_y1;
}

} // namespace

bool spyro_field_actor_composition_submit(Core *core, FieldActorComposition composition) {
  if (core == nullptr || core->game == nullptr) {
    return false;
  }

  spyro::secondary_actor_scene::Frame secondaryFrame{};
  spyro::secondary_actor_recipe::Recipe secondaryRecipe{};
  spyro::actor_face_submitter::Plan secondaryPlan{};
  if (composition.secondary) {
    const auto secondaryScene = spyro::secondary_actor_scene::prepare(core, secondaryFrame);
    if (secondaryScene != spyro::secondary_actor_scene::Status::Ready) {
      lucent::debug("fieldactors",
                    "REFUSED secondary scene={} records={} shadows={}",
                    spyro::secondary_actor_scene::status_name(secondaryScene),
                    secondaryFrame.records.size(),
                    secondaryFrame.shadows.size());
      return false;
    }
    if (gpu_vk_wide_engine(core)) {
      const int32_t center = gpu_vk_wide_engine_w(core) / 2;
      for (auto &record : secondaryFrame.records) {
        record.actor.input.projection.ofx = center << 16;
        record.actor.expected = spyro::actor_prefix::build(record.actor.input);
      }
    }
    secondaryRecipe = spyro::secondary_actor_recipe::derive(secondaryFrame);
    if (!secondaryReady(secondaryRecipe)) {
      lucent::debug("fieldactors",
                    "REFUSED secondary recipe={} reason={} record={} source_word={}",
                    (uint32_t)secondaryRecipe.status,
                    (uint32_t)secondaryRecipe.firstReason,
                    secondaryRecipe.firstUnsupportedRecord,
                    secondaryRecipe.firstUnsupportedSourceWord);
      return false;
    }
    secondaryPlan = spyro::actor_face_submitter::prepare(
        core->game->rq, kSecondaryProducer, secondaryRecipe.outputs, secondaryRecipe.faces);
    if (secondaryPlan.status != spyro::actor_face_submitter::Status::Ready &&
        secondaryPlan.status != spyro::actor_face_submitter::Status::ValidEmpty) {
      lucent::debug(
          "fieldactors", "REFUSED secondary submission={}", (uint32_t)secondaryPlan.status);
      return false;
    }
  }

  RenderQueue &queue = core->game->rq;
  spyro::field_shaded_queue_scene::Frame shadedFrame{};
  spyro::field_shaded_queue_recipe::Recipe shadedRecipe{};
  spyro::field_shaded_queue_submitter::Plan shadedPlan{};
  if (composition.shaded) {
    const int32_t clipRight =
        gpu_vk_wide_engine(core) ? std::max(512, gpu_vk_wide_engine_w(core)) : 512;
    const auto shadedScene = spyro::field_shaded_queue_scene::prepare(core, clipRight, shadedFrame);
    if (shadedScene != spyro::field_shaded_queue_scene::Status::Ready) {
      lucent::debug("fieldactors",
                    "REFUSED shaded scene={} records={} shadows={}",
                    spyro::field_shaded_queue_scene::statusName(shadedScene),
                    shadedFrame.input.records.size(),
                    shadedFrame.shadows.size());
      return false;
    }
    shadedRecipe = spyro::field_shaded_queue_recipe::derive(shadedFrame.input);
    if (!shadedReady(shadedRecipe)) {
      lucent::debug("fieldactors",
                    "REFUSED shaded recipe={} actor=0x{:08X} primitive={}",
                    (uint32_t)shadedRecipe.status,
                    shadedRecipe.firstUnsupportedActor,
                    shadedRecipe.firstUnsupportedPrimitive);
      return false;
    }
    shadedPlan = spyro::field_shaded_queue_submitter::prepare(queue, kShadedProducer, shadedRecipe);
    if (shadedPlan.status != spyro::field_shaded_queue_submitter::Status::Ready &&
        shadedPlan.status != spyro::field_shaded_queue_submitter::Status::ValidEmpty) {
      lucent::debug("fieldactors", "REFUSED shaded submission={}", (uint32_t)shadedPlan.status);
      return false;
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
    lucent::debug("fieldactors", "REFUSED combined shadow cursor=0x{:08X}", shadedShadowCursor);
    return false;
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
      lucent::debug("fieldactors",
                    "REFUSED batch admission={} item={} existing_objects={} existing_faces={}",
                    (uint32_t)admission.refusal,
                    admission.refusal_item,
                    admission.existing_objects,
                    admission.existing_faces);
      return false;
    }
  }
  if (!drawAreaReady(core->game->gpu)) {
    return false;
  }

  // Every source and queue check is complete. The commits are adjacent and precede either
  // publication, so a refusal cannot leave a partially updated shared shadow list.
  if (composition.secondary) {
    spyro::secondary_actor_scene::commit(core, secondaryFrame);
  }
  if (composition.shaded) {
    spyro::field_shaded_queue_scene::commit(core, shadedFrame);
  }
  if (secondaryPlan.status == spyro::actor_face_submitter::Status::Ready) {
    ProducerScope producer(&core->rsub.producerScope, kSecondaryProducer, "actor:secondary");
    spyro::actor_face_submitter::submit(core,
                                        queue,
                                        kSecondaryProducer,
                                        spyro::actor_face_submitter::Layer::Secondary,
                                        secondaryRecipe.faces,
                                        secondaryPlan);
  }
  if (shadedPlan.status == spyro::field_shaded_queue_submitter::Status::Ready) {
    ProducerScope producer(&core->rsub.producerScope, kShadedProducer, "spriteq:world-shaded");
    spyro::field_shaded_queue_submitter::submit(
        core, queue, kShadedProducer, shadedRecipe, shadedPlan);
  }
  lucent::debug("fieldactors",
                "PASS secondary_faces={} shaded_faces={} secondary_shadows={} shaded_shadows={} "
                "shadow_cursor=0x{:08X}",
                secondaryRecipe.faces.size(),
                shadedRecipe.faces.size(),
                secondaryFrame.shadows.size(),
                shadedFrame.shadows.size(),
                shadedShadowCursor + static_cast<uint32_t>(shadedFrame.shadows.size()) * 8u);
  return true;
}
