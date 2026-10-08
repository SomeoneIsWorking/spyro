#include "terrain_world_pass.h"

#include "core.h"
#include "game.h"
#include "gpu_native_internal.h"
#include "render_queue.h"
#include "spyro_context.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <lucent/log.h>
#include <span>

namespace spyro {
namespace {

// SET THE GPU'S DRAW AREA, through the GPU's own command path: the two one-word GP0 commands the
// guest issues, in the encoding the GPU decodes (X in bits 0..9, Y in bits 10..18 of each).
//
// Not a private poke at the state, because the state IS a command's effect: the guest's next
// primitive reads it, and the only way to be sure the value that lands is the value meant is to
// send the same word the guest sends.
void setDrawArea(Core &core, int x0, int y0, int x1, int y1) {
  const auto corner = [](int x, int y) {
    return (static_cast<std::uint32_t>(x) & 0x3FFu) |
           ((static_cast<std::uint32_t>(y) & 0x3FFu) << 10);
  };
  gpu_gp0(&core, 0xE3000000u | corner(x0, y0));
  gpu_gp0(&core, 0xE4000000u | corner(x1, y1));
}

} // namespace

TerrainWorldPass::TerrainWorldPass(Game &game,
                                   guest_terrain::Facts facts,
                                   guest_render_globals::Globals globals)
    : game_(game), facts_(facts), globals_(globals) {}

void TerrainWorldPass::capture(Core &core, TerrainField &into) const {
  into.camera = guest_terrain::readCamera(core, globals_);
  into.packetBudget = spyro::context(core).terrainPacketBudget;
  into.visibleSectors = spyro::context(core).terrainVisibility;
  into.captured = true;
}

void TerrainWorldPass::beginPresentation(Core &core,
                                         psx::frame::CapturedFrameView frame,
                                         bool interpolating) {
  (void)interpolating;
  // ONE REAL SCENE FRAME, OR NOT ONE AT ALL.
  //
  // Spyro's scene runs at 30 Hz on a fraction of the 60 Hz display fields, so a presentation is not
  // a scene frame: several fields can be delivered, and presented, between two of the guest's own
  // scene ticks. Sampling the camera per PRESENTATION therefore makes `previous_` and `present_`
  // the same camera on every one of those fields, and the in-between between them is that camera
  // again — which is judder at exactly the place this product exists to remove it.
  //
  // The key is the guest's OWN fact, `sceneProducerTicks`: the number of times the scene producer
  // has run. It advances only on a real scene tick, so the endpoints stay the two most recent REAL
  // scene frames however many display fields were delivered between them.
  const std::uint32_t sceneTick = spyro::context(core).sceneProducerTicks;
  if (captured_ && sceneTick == sceneTick_) {
    return;
  }
  sceneTick_ = sceneTick;
  captured_ = true;
  sceneAdvanced_ = true;

  // The three facts a frame is identified by, all read now because now is the only time they are
  // true: the packets this field's terrain linked, the camera its drawer read, and the sectors its
  // visibility call named. The packet set and the visibility bytes were captured during the guest's
  // own draw, before the field owner presented anything.
  // The scratchpad holds one visibility byte per sector, so a sector count the guest declares
  // larger than the scratchpad is not a level this pass can present — and it is checked HERE rather
  // than at construction because the count is the booted LEVEL's, not the process's: at
  // construction the guest has not run and the word is still zero.
  const std::uint32_t sectors = core.mem_r32(facts_.classify.sectorCount);
  if (sectors == 0 || sectors > guest_terrain::kScratchpadBytes) {
    lucent::error("terrworld",
                  "refusing a temporal world pass: this image declares {} sectors, and the "
                  "scratchpad holds at most {}",
                  sectors,
                  guest_terrain::kScratchpadBytes);
    std::abort();
  }
  // The passes' arenas, as they stand. A pass is keyed by the arena it allocated into, so this is
  // every pass the guest has run into a distinct arena, each with its own linked addresses and its
  // own [begin, end) — and `owns` picks by containment, which is what makes the answer exact even
  // though the guest presents one buffer's pass while the next draws into the other.
  arenas_.clear();
  const std::vector<spyro::Context::TerrainPacketArena> &run = spyro::context(core).terrainArenas;
  arenas_.assign(run.begin(), run.end());
  for (spyro::Context::TerrainPacketArena &arena : arenas_) {
    std::sort(arena.packets.begin(), arena.packets.end());
    arena.packets.erase(std::unique(arena.packets.begin(), arena.packets.end()),
                        arena.packets.end());
  }

  capture(core, present_);

  // THE CLIP WINDOW, from the guest's own captured terrain: the draw area the first of this field's
  // OWN packets was enqueued under. Every captured RqItem records it, so this is a fact about the
  // guest's frame and not a rectangle this pass chose. An in-between's own geometry is clipped to
  // it before it is submitted (see `reconstruct`), because its camera sits between two of the
  // guest's and reaches ground nearer than either did.
  present_.hasClip = false;
  for (const RqItem &item : frame.items) {
    if (!owns(item)) {
      continue;
    }
    present_.clipX0 = item.da_x0;
    present_.clipY0 = item.da_y0;
    present_.clipX1 = item.da_x1;
    present_.clipY1 = item.da_y1;
    present_.hasClip = true;
    break;
  }

  ownsField_ = !arenas_.empty() && !present_.visibleSectors.empty();
}

bool TerrainWorldPass::eligible(const Core &) const {
  return ownsField_ && previous_.captured;
}

bool TerrainWorldPass::owns(const RqItem &item) const {
  if (!ownsField_) {
    return false;
  }
  // WHICH PASS PRODUCED THIS ADDRESS: the arena containing it is the pass that allocated there, and
  // that pass's own linked set answers whether it linked this packet.
  //
  // AN ADDRESS INSIDE A PASS'S RANGE IS NOT NECESSARILY IN THAT PASS'S SET, so this does not return
  // on the first range that contains the address. MEASURED 2026-10-03: the cursor wanders rather
  // than alternating between two fixed arenas, so successive passes' ranges OVERLAP, and an address
  // in an older pass's range is usually not in that older pass's linked set. Returning there
  // answered false for 600 of 719 items on every frame after the first four.
  //
  // Containment narrows the search to the passes that could have produced it; the linked sets
  // decide.
  for (const spyro::Context::TerrainPacketArena &arena : arenas_) {
    if (item.guest_packet < arena.begin || item.guest_packet >= arena.end) {
      continue;
    }
    if (std::binary_search(arena.packets.begin(), arena.packets.end(), item.guest_packet)) {
      return true;
    }
  }
  return false;
}

void TerrainWorldPass::reconstruct(Core &core, float t) {
  RenderQueue *const sink = core.game->rqRedirect;
  if (sink == nullptr) {
    lucent::error("terrworld",
                  "terrain in-between requested with no reconstruction queue open; the framework's "
                  "in-between slot is the only caller that opens one");
    std::abort();
  }

  const float f = std::clamp(t, 0.0f, 1.0f);
  const guest_terrain::InBetweenCamera camera = guest_terrain::cameraBetween(
      core, globals_, previous_.camera, present_.camera, static_cast<double>(f));
  const std::vector<std::uint8_t> unioned =
      guest_terrain::unionVisibility(previous_.visibleSectors, present_.visibleSectors);
  guest_terrain::Rebuild rebuild(core, facts_, globals_, camera, unioned, present_.packetBudget);
  report_.visibleSectors = rebuild.visibleSectors();

  // THE GUEST'S OWN CLIP WINDOW, for exactly as long as this field's packets are replayed.
  //
  // The traversal clips a polygon only when ALL of its vertices leave the window (that is retail's
  // own test), so a polygon that straddles the bottom is drawn and the GPU's draw area is what
  // keeps it off the rows below. An in-between's camera is BETWEEN two of the guest's, so its
  // nearest ground is nearer than either endpoint's and it straddles rows the guest's own frame
  // never reached: MEASURED 2026-10-03 on SCUS_944.25, 497 of its submitted items straddled y=228
  // where the guest's own frame had none, and the twelve rows below the guest's window were painted
  // with terrain.
  //
  // The window is the one the guest's OWN identical packet stream was clipped to, taken from the
  // captured queue, and it is put back afterwards: the draw area is GPU state the guest owns, and
  // this pass is not the guest.
  const int savedX0 = core.game->gpu.s_da_x0;
  const int savedY0 = core.game->gpu.s_da_y0;
  const int savedX1 = core.game->gpu.s_da_x1;
  const int savedY1 = core.game->gpu.s_da_y1;
  if (present_.hasClip) {
    setDrawArea(core, present_.clipX0, present_.clipY0, present_.clipX1, present_.clipY1);
  }
  rebuild.forEachPacket(
      [&core](std::uint32_t, std::uint32_t packet, std::span<const std::uint32_t> words) {
        gpu_replay_guest_packet(&core, packet, words.data(), static_cast<unsigned>(words.size()));
      });
  setDrawArea(core, savedX0, savedY0, savedX1, savedY1);

  report_.packets = static_cast<std::uint32_t>(sink->n);
  report_.factorPerMille = static_cast<std::uint32_t>(std::lround(static_cast<double>(f) * 1000.0));
}

void TerrainWorldPass::rotate(Core &core) {
  (void)core;
  // Only a REAL SCENE FRAME advances the endpoints. A presentation that carried no new scene tick
  // presented the same camera twice, and retiring the older endpoint for it would leave the next
  // in-between interpolating between a frame and itself.
  if (!sceneAdvanced_) {
    return;
  }
  sceneAdvanced_ = false;
  previous_ = std::move(present_);
  present_ = TerrainField{};
}

std::unique_ptr<::InBetweenStrategy> makeTerrainWorldPass(Game &game,
                                                          guest_terrain::Facts facts,
                                                          guest_render_globals::Globals globals) {
  return std::make_unique<TerrainWorldPass>(game, facts, globals);
}

} // namespace spyro
