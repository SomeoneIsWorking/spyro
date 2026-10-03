#include "terrain_world_pass.h"

#include "core.h"
#include "game.h"
#include "gpu_native_internal.h"
#include "guest_camera_builder.h"
#include "guest_gte.h"
#include "guest_terrain_drawer.h"
#include "ordering_table.h"
#include "render_queue.h"
#include "spyro_context.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <lucent/log.h>

namespace spyro {
namespace {

namespace gte = guest_gte;

// The sizes are the DRAWER'S OWN and are named by it, not here: the scratchpad and its extent
// (guest_terrain_frame.h), the scratch block's extent (the image's own `scratchListsBelowEnd`
// fact), the ordering table's (kOrderingTableBytes), and the fogged-colour buffer's (kFogColours).
using guest_terrain::kOrderingTableBytes;
using guest_terrain::kScratchpad;
using guest_terrain::kScratchpadBytes;
// The end of the 2 MB main-RAM window a guest packet address can name (ordering_table.h). It is the
// arena's real ceiling, and the only ceiling this pass states rather than reads.
constexpr std::uint32_t kMainRamEnd = psx::gpu::kKseg0Base + 0x200000u;

// The bit the detail pass's sign-clear strips from its flag cursor, and the width of the window
// that leaves behind. See the mapping site for why both exist.
constexpr std::uint32_t kKseg0SignBit = 0x80000000u;
constexpr std::uint32_t kFlagWindowBytes = 0x10000u;

// The DPCS-faded copy of a sector's colours, one window per field (guest_terrain_memory.h). Its
// extent is the drawer's own: a sector's colour array is at most this long, which is why the guest
// keeps one buffer and overwrites it per sector.
constexpr std::uint32_t kFoggedColourBytes = 0x1000u;

// THE ORDERING TABLE'S WINDOW, which is NOT kOrderingTableBytes.
//
// The guest's far pass names bins past the 512 the drawer's own publish covers (MEASURED
// 2026-10-02 on SCUS_944.25: bin 573), and the drawer writes each named bin's slot, so a window of
// 512 bins would refuse a write the traversal is entitled to make. Doubling it covers every bin a
// 1024-bin table could name, and a bin beyond THAT is refused by name instead of silently dropped.
constexpr std::uint32_t kOrderingWindowBytes = 2 * kOrderingTableBytes;

// THE SMALLEST PACKET ARENA AN IN-BETWEEN WILL ACCEPT, and why it is this size.
//
// MEASURED 2026-10-03 on SCUS_944.25 over a full title route: one field's terrain allocates
// 55,672 bytes of packets at its widest (the real field) and an in-between 21,940..36,128.
// Sixty-four kilobytes clears the widest real frame with room to spare, and anything less than that
// is a level whose arena the window above cannot hold — refused by name rather than presented as a
// truncated picture.
constexpr std::uint32_t kMinArenaBytes = 0x10000u;

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

// The visibility union, restored on the way out. The scratchpad is GUEST memory; the union is
// written into this pass's HOST copy of it, so there is nothing to restore — and saying so here is
// the reason this is a class rather than a bare pair of loops.
class TerrainWorldPass::VisibilityUnion {
public:
  VisibilityUnion(TerrainWorldPass &owner,
                  guest_terrain::TerrainMemory &memory,
                  const std::vector<std::uint8_t> &bytes)
      : memory_(memory), bytes_(bytes) {
    for (std::size_t i = 0; i < bytes_.size(); ++i) {
      if (bytes_[i] != 0) {
        memory_.w8(kScratchpad + static_cast<std::uint32_t>(i), bytes_[i]);
        ++visible_;
      }
    }
  }

  [[nodiscard]] std::uint32_t visible() const {
    return visible_;
  }

private:
  guest_terrain::TerrainMemory &memory_;
  const std::vector<std::uint8_t> &bytes_;
  std::uint32_t visible_ = 0;
};

TerrainWorldPass::TerrainWorldPass(Game &game,
                                   guest_terrain::Facts facts,
                                   guest_render_globals::Globals globals)
    : game_(game), facts_(facts), globals_(globals) {}

guest_camera::Angles TerrainWorldPass::cameraAngles(Core &core) const {
  const auto half = [&core](std::uint32_t at) {
    const std::uint32_t word = core.mem_r32(at) & 0xFFFFu;
    return word >= 0x8000u ? static_cast<std::int32_t>(word) - 0x10000
                           : static_cast<std::int32_t>(word);
  };
  const std::uint32_t at = globals_.cameraAngles;
  return guest_camera::Angles{half(at), half(at + 2), half(at + 4)};
}

void TerrainWorldPass::capture(Core &core, TerrainField &into) const {
  for (std::uint32_t i = 0; i < gte::kRotationWords; ++i) {
    into.camera.rotation[i] = core.mem_r32(globals_.cameraRotation + 4 * i);
  }
  for (std::uint32_t i = 0; i < 3; ++i) {
    into.camera.position[i] = core.mem_r32(globals_.cameraPosition + 4 * i);
  }
  // THE CAMERA'S INPUT, not its output: the three angles the guest's own builder read to produce
  // the five words captured above. Read here, at the presentation, which is the same point in the
  // field the matrices above were built from (MEASURED: rebuilding both matrices from these three
  // words reproduces the guest's own, bit for bit, for every camera on the route).
  into.angles = cameraAngles(core);
  into.visibleSectors = spyro_context(core).terrainVisibility;
  into.captured = true;
}

void TerrainWorldPass::beginPresentation(Core &core, CapturedFrameView frame, bool interpolating) {
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
  const std::uint32_t sceneTick = spyro_context(core).sceneProducerTicks;
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
  if (sectors == 0 || sectors > kScratchpadBytes) {
    lucent::error("terrworld",
                  "refusing a temporal world pass: this image declares {} sectors, and the "
                  "scratchpad holds at most {}",
                  sectors,
                  kScratchpadBytes);
    std::abort();
  }
  // The passes' arenas, as they stand. A pass is keyed by the arena it allocated into, so this is
  // every pass the guest has run into a distinct arena, each with its own linked addresses and its
  // own [begin, end) — and `owns` picks by containment, which is what makes the answer exact even
  // though the guest presents one buffer's pass while the next draws into the other.
  arenas_.clear();
  const std::vector<SpyroContext::TerrainPacketArena> &run = spyro_context(core).terrainArenas;
  arenas_.assign(run.begin(), run.end());
  for (SpyroContext::TerrainPacketArena &arena : arenas_) {
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
  for (const SpyroContext::TerrainPacketArena &arena : arenas_) {
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

  // THE CAMERA BETWEEN THE TWO ENDPOINTS, BUILT BY THE GUEST'S OWN BUILDER.
  //
  // The three ANGLES the guest keeps are interpolated, each the short way round its 4096-step turn,
  // and the guest's own camera builder (guest_camera_builder.cpp, recovered from 0x8001C2F8) turns
  // them back into the two matrices. The three POSITION words are interpolated as themselves: a
  // world coordinate is one signed 32-bit value, and the guest's own arithmetic wraps rather than
  // clamps, so the intermediate is 64-bit and the result wraps to 32.
  //
  // WHY THE ANGLES AND NOT THE FIVE WORDS. The five rotation words are what the builder WRITES
  // (docs/re-frontier.md `guest.camera-builder`): three rotations multiplied by the GTE, one row of
  // the product then scaled by 5/8 and truncated back into 16 bits. A packed pair of those elements
  // is not a value that moves linearly with the camera — one half changing sign carries into the
  // other's half of the word, and the truncation means two angles an eighth of a turn apart do not
  // give words halfway between theirs. Interpolating the INPUT and re-running the guest's own maths
  // is what makes the in-between a camera the guest's own camera code would have held.
  const float f = std::clamp(t, 0.0f, 1.0f);
  guest_terrain::InBetweenCamera camera{};
  const guest_camera::Angles angles =
      guest_camera::interpolate(previous_.angles, present_.angles, static_cast<double>(f));
  const guest_camera::Matrices matrices =
      guest_camera::Builder(core, globals_.cameraSineTable, globals_.cameraCosineTable)
          .build(angles);
  for (std::uint32_t i = 0; i < gte::kRotationWords; ++i) {
    camera.rotation[i] = matrices.projection[i];
    camera.viewRotation[i] = matrices.view[i];
  }
  // ONE SIGNED 32-BIT FIXED-POINT WORD, INTERPOLATED AS ITSELF. The intermediate is 64-bit and the
  // result wraps to 32 bits, because a position component overflows the same way in the guest's own
  // arithmetic and a clamped value would be a different camera.
  const auto lerpWord = [f](std::uint32_t from, std::uint32_t to) {
    const auto a = static_cast<std::int64_t>(static_cast<std::int32_t>(from));
    const auto b = static_cast<std::int64_t>(static_cast<std::int32_t>(to));
    const auto v = a + static_cast<std::int64_t>(
                           std::llround(static_cast<double>(b - a) * static_cast<double>(f)));
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(v));
  };
  for (std::uint32_t i = 0; i < 3; ++i) {
    camera.position[i] = lerpWord(previous_.camera.position[i], present_.camera.position[i]);
  }

  // The visibility union of the two endpoints. Drawn sectors are the ones visible in EITHER field,
  // so a sector that enters or leaves between the endpoints is present on both in-betweens that
  // straddle it, and the level's interior is never exposed by a decision taken for a different
  // camera.
  std::vector<std::uint8_t> unioned(present_.visibleSectors.size(), 0);
  for (std::size_t i = 0; i < unioned.size(); ++i) {
    unioned[i] = static_cast<std::uint8_t>(
        present_.visibleSectors[i] != 0 ||
        (i < previous_.visibleSectors.size() && previous_.visibleSectors[i] != 0));
  }

  // EVERY RANGE THE IN-BETWEEN OWNS, at guest-shaped addresses, over host bytes. Every address here
  // is READ from the guest's globals; none of them is written here.
  //
  // The windows are DISJOINT, and that is the whole point. The guest's arena is not: it grows
  // upward out of its own cursor, and by the time the presenter rebuilds the frame the cursor has
  // already advanced past everything the field's own producers allocated, so an in-between started
  // there can run past the scratch block above it. MEASURED 2026-10-03 on SCUS_944.25: an
  // in-between from 0x801D04B4 needed 35,468 bytes with the scratch block beginning at 0x801D7444,
  // and the packets it wrote over the scratch block's sector and split lists — which is what made
  // walkSplitList read the split-list word 0x0C000000 as a sector header and fault on 0x30000008
  // (the same fault, on the same line, as SCUS_944.67's).
  //
  // So the packet arena gets a window of its own, ABOVE every other range this traversal touches,
  // up to the end of the 2 MB window a guest packet address can name. The address has to be a
  // guest-shaped main-RAM one because a packet's chain link is a 24-bit main-RAM OFFSET
  // (terrain_packet_sink.h), so a host-only address could not be linked.
  //
  // Nothing else the traversal reads may live in that window, and the level's own records do not:
  // they are read through unmapped reads, which fall through to the guest, and they sit below every
  // one of these windows (MEASURED 2026-10-03: the sector and polygon records the traversal read
  // are at 0x8008xxxx, and the scratch blocks end at 0x801DA844, below the window's base).
  const std::uint32_t scratch =
      core.mem_r32(globals_.scratchBaseWord) - facts_.scratchListsBelowEnd;
  const std::uint32_t tableBase = core.mem_r32(globals_.orderingTable);
  const std::uint32_t frameTop = std::max({scratch + facts_.scratchListsBelowEnd,
                                           tableBase + kOrderingWindowBytes,
                                           facts_.foggedColours + kFoggedColourBytes});
  const PacketArena arena = packetArenaWindow(frameTop, kMainRamEnd);
  const std::uint32_t arenaBase = arena.base;
  const std::uint32_t arenaBytes = arena.bytes;
  if (arenaBytes < kMinArenaBytes) {
    lucent::error("terrworld",
                  "refusing a terrain in-between: the packet arena above this field's own state "
                  "runs from 0x{:08X} to the end of main RAM, which is {} bytes, and one field's "
                  "terrain needs more than {}",
                  arenaBase,
                  arenaBytes,
                  kMinArenaBytes);
    std::abort();
  }

  guest_terrain::HostMemory memory(core);
  // THE SCRATCHPAD, where every pass reuses the same region for its vertex cache.
  memory.mapEmpty(kScratchpad, kScratchpadBytes);
  // THE SCRATCH BLOCK, which is FRAME STATE and nothing else: the five sector lists, the split grid
  // cells and the per-polygon flag bytes are all written by this traversal, and the level's own
  // records are NOT here — they are read through unmapped reads. So it starts empty, because
  // seeding it hands the traversal the LAST REAL FIELD's lists, and a split entry naming a polygon
  // from that field makes the resplit and fine passes link a packet this field never built.
  //
  // AND IT RUNS TO `frameTop`, NOT TO THE BLOCK'S OWN TOP. The split lists grow upward from
  // kFineSplitList and kCoarseSplitList as this camera defers polygons, and retail sized the block
  // for the ONE camera its own field had. An in-between's camera is between two of the guest's, and
  // the splits it makes are the union of what each endpoint deferred — so it can reach further than
  // either did. MEASURED 2026-10-03 on SCUS_944.67: the first in-between of the route wrote a split
  // entry 0x30C4 bytes above the block, past its top at +0x3000, and the write was refused because
  // nothing owned it. Everything between the block and `frameTop` is this traversal's own state and
  // nothing else's — `frameTop` is the end of every range it does NOT own — so the window is the
  // honest extent. A split list that outgrows THAT is refused by name below, never truncated into
  // the ordering table's window.
  memory.mapEmpty(scratch, frameTop - scratch);
  // THE FLAG WINDOW. The detail pass's per-polygon clip-flag cursor is `scratch + (originY &
  // 0xFFFF)` and then sign-cleared (`flagCursor_ = (flagCursor_ << 1) >> 1`,
  // guest_terrain_detail.cpp), which strips bit 31 — so for a level whose scratch block is in KSEG0
  // the cursor lands in the low window just below it, and the pass writes its flag byte there.
  // Retail does this too and this reproduces it: the real field writes the same bytes through
  // GuestMemory every field, which is why the 4:3 md5 gate is the right place to notice if it ever
  // stopped. MEASURED 2026-10-02 on SCUS_944.25 at scratch 0x801BB444: real-field writes at
  // 0x001BB4C6 and 0x001D7640 among others.
  //
  // The window is exactly `scratch` with bit 31 cleared, 64 KB wide, because that is the only
  // transformation the cursor undergoes and the offset into it is `& 0xFFFF`. Nothing reads those
  // bytes back — the pass keeps the cursor in a register and only ever writes through it — so this
  // is host storage standing in for memory the guest also never reads.
  memory.mapEmpty(scratch & ~kKseg0SignBit, kFlagWindowBytes);
  // THE ORDERING TABLE'S SLOTS, empty: `linkAndAdvance` reads each bin's head to decide whether to
  // link at the head or behind the previous packet, and seeded, an in-between would link behind the
  // last real field's chain. The freshness an in-between needs from the table's bytes comes from
  // the frame's own record of which bins IT has linked (guest_terrain_frame.h), which is also the
  // only mechanism with no ceiling on how far above 512 the far pass may reach.
  memory.mapEmpty(tableBase, kOrderingWindowBytes);
  // THE FOGGED-COLOUR BUFFER, this field's own DPCS output, which no pass reads back from another
  // sector's.
  memory.mapEmpty(facts_.foggedColours, kFoggedColourBytes);
  // THE PACKET ARENA, this field's own, empty for the same reason the ordering table is.
  memory.mapEmpty(arenaBase, arenaBytes);

  // THE GTE, HANDED BACK AS IT WAS FOUND.
  //
  // The traversal issues the guest's own GTE instructions — RTPS, NCLIP, DPCS, MVMVA — and leaves
  // whatever they leave: the rotation matrix, the light matrix, the far colour, the IR/RGBC it was
  // given, the projection FIFO, the overflow flags. The guest reads all of those back after its own
  // field, and an in-between that left its own values there would change the guest's next frame.
  // The snapshot is a copy of the register file and the flags, NOT a sweep through the read ports:
  // reading DR12..DR14 pops the projection FIFO and reading a flag register clears it
  // (psxport's GTE_SaveRawState, gte_state.h).
  GteRawState gteBefore;
  GTE_SaveRawState(&gteBefore);

  guest_terrain::Drawer drawer(
      core, facts_, globals_, memory, guest_terrain::FrameMode::InBetween, &camera, arenaBase);
  const guest_terrain::TerrainFrame *ran = nullptr;
  {
    const VisibilityUnion sectors(*this, memory, unioned);
    ran = &drawer.run();
    report_.visibleSectors = sectors.visible();
  }

  // The order table and the arena the traversal just built, handed over in retail's flatten order.
  const terrain_packet_sink::Bound bound{
      .tableBase = tableBase,
      .arenaBase = arenaBase,
      .arenaBytes = arenaBytes,
      .linkedBins = &ran->linkedBins(),
  };

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
  terrain_packet_sink::submit(core, memory, bound);
  setDrawArea(core, savedX0, savedY0, savedX1, savedY1);

  GTE_RestoreRawState(&gteBefore);

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
