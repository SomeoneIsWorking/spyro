#pragma once

#include "actor_temporal.h"
#include "archive_transfer.h"
#include "field_2d_overlay.h"
#include "field_owner.h"
#include "field_shaded_queue_temporal.h"
#include "fx_paired_actor.h"
#include "guest_projection_owner.h"
#include "interp_census.h"
#include "load_ledger.h"
#include "margin_object_census.h"
#include "moby_shadow_list.h"
#include "presentation_owner.h"
#include "runtime_run.h"
#include "secondary_actor_temporal.h"
#include "sector_visibility.h"
#include "temporal_scene.h"
#include "terrain_temporal.h"
#include "world_temporal.h"

class Core;

// One title-level context composed from the states owned by cohesive subsystems. Adding another
// subsystem does not turn its renderer state into the definition of the whole game context.
struct SpyroContext {
  spyro::ArchiveTransfer archiveTransfer{};
  // One record per CD read the transfer above performed, and the coverage denominator for the
  // 31-site issuer census. It lives beside the transfer because it records that transfer's work,
  // and per-Core because two Cores in one process must not pool their operations.
  spyro::load_ledger::Ledger loadLedger{};
  spyro::RuntimeRun run{};
  // The selected title's field owner, published by its frame driver. Non-owning: the driver
  // holds it and is installed into the Game before the first host turn can arrive, so the
  // back-pointer is valid for the whole process and is how a host turn with only a Core
  // reaches "a field happened".
  spyro::FieldOwner *fieldOwner = nullptr;
  // The selected title's guest-projection owner (Spyro 2's widescreen owner), published by the
  // runtime that registers its projection overrides. Non-owning, and null on every title that has
  // no projection of its own. See `guest_projection_owner.h` for why the seam exists at all.
  GuestProjectionOwner *projectionHook = nullptr;
  SpyroPairedActorFrameState pairedActor{};
  spyro::world_temporal::History worldTemporal{};
  // The drawn half of the sector-visibility table the last world submission published; its guest
  // half is D_800771C8 itself. Same lifetime and timing as that guest table, so a reader that used
  // to read D_800771C8 sees exactly the widening of what the guest reads (issue 0152).
  spyro::sector_visibility::Table drawnSectors{};
  // The drawn half of the Moby shadow list; its guest half is the list at 0x800724F4. Reset and
  // appended by the same three passes at the same points as that list's cursor, and read by the
  // shadow producer in its place, so margin Mobys cast shadows without entering guest RAM.
  spyro::moby_shadow_list::List drawnMobyShadows{};
  // Per-class count of objects drawn past the guest's own 512-column window, accumulated across the
  // run. Diagnostic, not state: nothing in the shipping path reads it, and the 4:3 control run
  // answers the same question with the same denominators (issue 0154).
  spyro::margin_object_census::Recorder marginCensus{};
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
  // What the interpolated present rebuilt, per draw category. It belongs to the frame's context
  // rather than to a file-scope object so two Cores in one process cannot share one route's
  // counters.
  spyro::interp_census::Census interpCensus{};
  SpyroPresentationOwner presentationOwner{};
};

SpyroContext &spyro_context(Core &core);
const SpyroContext &spyro_context(const Core &core);
