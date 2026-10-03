#pragma once

#include <vector>

#include "actor_temporal.h"
#include "archive_transfer.h"
#include "field_2d_overlay.h"
#include "field_owner.h"
#include "field_shaded_queue_temporal.h"
#include "fx_paired_actor.h"
#include "guest_projection_owner.h"
#include "hud_draw_context.h"
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
  // The screen-space widget whose draw is in progress, for the emitters a title's HUD shares
  // between widgets. See `game/render/hud_draw_context.h` for why a return address is not
  // enough to classify the widget reaching it.
  spyro::hud_draw_context::Draw hudDraw{};
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
  // How many times this Core's NATIVE SCENE producers (the terrain drawer and the moby visibility
  // walk) have run. It is a tick and not a flag because the guest asks the temporal product about a
  // field that may not have drawn any scene at all, and the question is "did the scene move since
  // you last looked", which a flag cleared elsewhere would answer wrongly whenever the clearing and
  // the asking fall on different fields. Two Cores in one process must not share it, which is why
  // it lives here rather than in a process-wide counter.
  std::uint32_t sceneProducerTicks = 0;
  // ONE TERRAIN PASS'S LINKED PACKETS, AND THE RANGE IT ALLOCATED THEM INTO.
  //
  // This is the terrain's identity in a captured queue, by the guest's own packet addresses and by
  // nothing else: MEASURED 2026-10-02 on SCUS_944.25, a field's captured queue carries exactly 719
  // of one pass's links, and no other queue item's packet address is among them (1355 items, 719
  // hits, 0 false positives).
  //
  // IT IS ONE RECORD PER PASS, KEYED BY ARENA, AND THAT IS THE WHOLE POINT. MEASURED 2026-10-03:
  // the guest DOUBLE-BUFFERS. One pass runs per scene tick into one of two packet arenas — the
  // primitive cursor alternates between 0x801A2984.. and 0x801C04BC.. — and the queue presented on
  // that tick carries the OTHER pass. A single union of every pass's links therefore describes a
  // pass the presented queue does not hold, and a consumer matching against it found 0 of 1355
  // items on every frame but the first.
  //
  // So the owner keeps one record per arena and REPLACES a record whenever a pass writes that arena
  // again. A consumer then selects the record whose [begin, end) contains the address it is asking
  // about, which by construction is the pass that produced that address — so it covers exactly the
  // pass the presented queue holds, with no tick counting and no window to get wrong.
  struct TerrainPacketArena {
    // The half-open range this pass allocated into: the primitive cursor's value when the pass
    // started, and one past its last packet. A pass only ever grows `end`, and `end` is what makes
    // the containment test above exact rather than a range guess.
    std::uint32_t begin = 0;
    std::uint32_t end = 0;
    // The linked addresses, sorted and unique: the consumer asks membership many times per frame.
    std::vector<std::uint32_t> packets;
  };
  // Small and bounded. MEASURED: two arenas alternate, so this holds both plus a margin; a third
  // entry means the cursor moved somewhere new, and the oldest is dropped rather than grown.
  static constexpr std::size_t kMaxTerrainArenas = 64;
  std::vector<TerrainPacketArena> terrainArenas;

  // Begin a pass's record for the arena at `begin`. An arena already recorded is REPLACED, because
  // a second pass into the same arena is a second pass over the same packets and the first one's
  // addresses are stale the moment the second writes them.
  TerrainPacketArena &beginTerrainArena(std::uint32_t begin) {
    for (TerrainPacketArena &arena : terrainArenas) {
      if (arena.begin == begin) {
        arena.end = begin;
        arena.packets.clear();
        return arena;
      }
    }
    if (terrainArenas.size() >= kMaxTerrainArenas) {
      terrainArenas.erase(terrainArenas.begin());
    }
    terrainArenas.push_back(TerrainPacketArena{begin, begin, {}});
    return terrainArenas.back();
  }

  // The sector-visibility bytes the guest's own visibility call left in the scratchpad, copied at
  // the moment they exist. The drawer's classification CLEARS each byte as it consumes it, so this
  // answer is gone by the time the frame is presented, and an in-between has to be handed it rather
  // than find it. The bytes are read here, in the real field, before the clear — the one place they
  // are true.
  std::vector<std::uint8_t> terrainVisibility;
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
