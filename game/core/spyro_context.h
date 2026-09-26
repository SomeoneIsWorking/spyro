#pragma once

#include "actor_temporal.h"
#include "archive_transfer.h"
#include "field_2d_overlay.h"
#include "field_shaded_queue_temporal.h"
#include "fx_paired_actor.h"
#include "presentation_owner.h"
#include "runtime_run.h"
#include "secondary_actor_temporal.h"
#include "temporal_scene.h"
#include "terrain_temporal.h"
#include "world_temporal.h"

class Core;

// One title-level context composed from the states owned by cohesive subsystems. Adding another
// subsystem does not turn its renderer state into the definition of the whole game context.
struct SpyroContext {
  spyro::ArchiveTransfer archiveTransfer{};
  spyro::RuntimeRun run{};
  SpyroPairedActorFrameState pairedActor{};
  spyro::world_temporal::History worldTemporal{};
  spyro::actor_temporal::History actorTemporal{};
  spyro::secondary_actor_temporal::History secondaryActorTemporal{};
  spyro::field_shaded_queue_temporal::History shadedQueueTemporal{};
  spyro::terrain_temporal::History terrainTemporal{};
  // The FIELD arm's screen-space 2D layer: the logic frame's capture, and the two-endpoint history
  // that reconstructs it in an in-between present. One pair of members because they are one concept
  // — an endpoint and the interval over it — and splitting them would leave the frame's capture in
  // one owner and the interval that consumes it in another.
  spyro::field_2d_overlay::Frame overlayFrame{};
  spyro::field_2d_overlay::History overlayTemporal{};
  SpyroTemporalSceneAdmission temporalAdmission{};
  SpyroPresentationOwner presentationOwner{};
};

SpyroContext &spyro_context(Core &core);
const SpyroContext &spyro_context(const Core &core);
