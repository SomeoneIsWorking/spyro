#pragma once

#include <vector>

#include "actor_temporal.h"
#include "archive_transfer.h"
#include "field_2d_overlay.h"
#include "field_owner.h"
#include "field_shaded_queue_temporal.h"
#include "guest_projection_owner.h"
#include "hud_draw_context.h"
#include "moby_shadow_list.h"
#include "paired_actor.h"
#include "presentation_owner.h"
#include "secondary_actor_temporal.h"
#include "sector_visibility.h"
#include "temporal_scene.h"
#include "terrain_temporal.h"
#include "world_temporal.h"

class Core;

namespace spyro {

// One title-level context composed from the states owned by cohesive subsystems. Adding another
// subsystem does not turn its renderer state into the definition of the whole game context.
struct Context {
  ArchiveTransfer archiveTransfer{};
  // The selected title's field owner, published by its frame driver. Non-owning: the driver
  // holds it and is installed before the first host turn can arrive.
  FieldOwner *fieldOwner = nullptr;
  // The selected title's guest-projection owner, published by the runtime that registers its
  // projection overrides; null on every title with no projection of its own.
  spyro::GuestProjectionOwner *projectionHook = nullptr;
  // The screen-space widget whose draw is in progress, for the emitters a title's HUD shares
  // between widgets.
  spyro::hud_draw_context::Draw hudDraw{};
  spyro::paired_actor::FrameState pairedActor{};
  world_temporal::History worldTemporal{};
  // The drawn half of the sector-visibility table the last world submission published; its guest
  // half is D_800771C8 itself.
  sector_visibility::Table drawnSectors{};
  // The drawn half of the Moby shadow list; its guest half is the list at 0x800724F4. Reset and
  // appended by the same three passes at the same points as that list's cursor, so margin Mobys
  // cast shadows without entering guest RAM.
  moby_shadow_list::List drawnMobyShadows{};
  actor_temporal::History actorTemporal{};
  secondary_actor_temporal::History secondaryActorTemporal{};
  field_shaded_queue_temporal::History shadedQueueTemporal{};
  terrain_temporal::History terrainTemporal{};
  // How many times this Core's NATIVE SCENE producers (the terrain drawer and the moby visibility
  // walk) have run. It is a tick and not a flag because the question is "did the scene move since
  // you last looked", which a flag cleared elsewhere would answer wrongly.
  std::uint32_t sceneProducerTicks = 0;
  // ONE TERRAIN PASS'S LINKED PACKETS, AND THE RANGE IT ALLOCATED THEM INTO.
  //
  // This is the terrain's identity in a captured queue, by the guest's own packet addresses alone.
  // The guest DOUBLE-BUFFERS: one pass runs per scene tick into one of two packet arenas and the
  // queue presented on that tick carries the OTHER pass, so a record per arena is replaced whenever
  // a pass writes that arena again. A consumer then selects the record whose [begin, end) contains
  // the address it asks about, which by construction is the pass that produced it.
  struct TerrainPacketArena {
    // The primitive cursor's value when the pass started, and one past its last packet. A pass
    // only ever grows `end`, and `end` is what makes the containment test exact rather than a
    // range guess.
    std::uint32_t begin = 0;
    std::uint32_t end = 0;
    // Sorted and unique: the consumer asks membership many times per frame.
    std::vector<std::uint32_t> packets;
  };
  // Two arenas alternate, so this holds both plus a margin; a third entry means the cursor moved
  // somewhere new, and the oldest is dropped rather than grown.
  static constexpr std::size_t kMaxTerrainArenas = 64;
  std::vector<TerrainPacketArena> terrainArenas;

  // Begin a pass's record for the arena at `begin`. An arena already recorded is replaced, because
  // a second pass into it is a second pass over the same packets.
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

  // The sector-visibility bytes the guest's own visibility call left in the scratchpad. The
  // drawer's classification clears each byte as it consumes it, so an in-between has to be handed
  // them rather than find them.
  std::vector<std::uint8_t> terrainVisibility;
  // How far the real field's packets could grow before its far pass stopped: the pool's top minus
  // the cursor the drawer started at. An in-between draws within the same budget.
  std::int32_t terrainPacketBudget = 0;
  // The FIELD arm's screen-space 2D layer: the logic frame's capture, and the two-endpoint history
  // that reconstructs it in an in-between present.
  spyro::field_2d_overlay::Frame overlayFrame{};
  spyro::field_2d_overlay::History overlayTemporal{};
  spyro::temporal_scene::Admission temporalAdmission{};
  PresentationOwner presentationOwner{};
};

// The per-Core context, reached from a Core alone because a native override is a plain function
// pointer with nowhere to hang a back-pointer. Absent before the title publishes it.
Context &context(Core &core);
const Context &context(const Core &core);

} // namespace spyro
