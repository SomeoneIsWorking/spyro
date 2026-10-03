// guest_terrain_frame.h — the state one call of the terrain drawer carries from pass to pass.
//
// The drawer is one hand-written routine that runs its passes back to back (see
// guest_terrain_passes.h). Retail threads a handful of values through all of them in registers it
// never spills: the next free primitive packet (fp), the ordering table, the scratch block the
// sector lists live in, and the screen window the outcodes test. This is that set, plus the two
// result registers: retail returns with v0 and v1 holding whatever its last pass left in them, and
// the O32 contract makes both observable, so each pass records what it leaves there.
//
// The per-image facts the passes read (this title's addresses, this title's authored window) are
// carried on the frame rather than compiled in, which is what lets two titles share these passes.
//
// TWO THINGS THE FRAME NOW OWNS THAT IT DID NOT BEFORE.
//
// `FrameMode`. The passes are the same seven passes whichever field is being drawn, but they are
// not the same WORK: the real field asks the guest which sectors are visible, animates the sector
// data, publishes the visibility groups the moby walk reads, stores the primitive cursor and leaves
// v0/v1 where the caller will read them. An in-between field runs no guest code and must write no
// guest byte, so each of those is behind `realField()`. Every guard is a WRITE THE GUEST MUST SEE,
// or a call only the guest can make; nothing else is gated, and the gating is what makes the real
// field provably unchanged.
//
// `TerrainMemory`. The five ranges the passes own for the length of a field (see
// guest_terrain_memory.h) are read and written through one accessor, so the same traversal runs
// over the guest's RAM for a real field and over host memory for an in-between.
#pragma once

#include "core.h"
#include "guest_gte.h"
#include "guest_render_globals.h"
#include "guest_terrain_facts.h"
#include "guest_terrain_memory.h"
#include "guest_terrain_screen.h"
#include "spyro_context.h"

#include <cstdint>
#include <vector>

namespace spyro::guest_terrain {

// The sector lists, as offsets from the scratch block base (VXY1, kScratchListsBelowEnd below the
// frame's scratch block end). The classification pass builds the first three; the two near passes
// defer polygons into the last two for subdivision.
inline constexpr std::uint32_t kDetailList = 0x2000; // textured sectors, near enough for detail
inline constexpr std::uint32_t kTranslucentList = 0x2200; // translucent sectors (flag 0x5000)
inline constexpr std::uint32_t kFineSplitList = 0x2300; // polygons with a vertex nearer than 0x140
inline constexpr std::uint32_t kCoarseSplitList = 0x2400; // polygons near enough to split once
inline constexpr std::uint32_t kFarList = 0x2A00;         // untextured low-detail sectors
// The fine pass reuses kCoarseSplitList, already walked, as the queue of primitives too large for
// the GPU (guest_terrain_resplit.cpp); kSplitPrimitiveCursor is its end.
inline constexpr std::uint32_t kResplitQueue = kCoarseSplitList;

// The drawer's scratchpad. Every pass reuses the same region for its vertex cache. It is scratchpad
// RAM, not guest RAM, and therefore the same address in every image of this family.
inline constexpr std::uint32_t kScratchpad = 0x1F800000u;
// The whole scratchpad, and therefore every address the passes own above it.
inline constexpr std::uint32_t kScratchpadBytes = 0x400u;
inline constexpr std::uint32_t kVertexDepths = 0x1F8002A8u; // the detail passes' SZ halfwords
inline constexpr std::uint32_t kSplitPrimitiveCursor =
    0x1F8003FCu; // one past kResplitQueue's last entry

// A CAMERA THE RENDER READS, held by the frame rather than read from the globals word, so an
// in-between field can present a camera between two of the guest's own.
//
// A real field reads the guest's own five words, byte for byte as retail did. An in-between does
// not read them at all: it carries a camera BUILT from the guest's own state by the guest's own
// builder (guest_camera_builder.h), which is why there are two matrices here — the drawer's
// (row 1 scaled by 5/8) and the classification pass's (unscaled), which are two different words in
// the guest's memory for the same three angles.
//
// Each entry is ONE signed 32-bit value, not a packed pair: a GTE rotation-matrix element and a
// world position component are each a single fixed-point word, and every pass reads them whole
// (`asSigned(frame_.positionWord(0)) >> 4`, `gte_write_ctrl(kRotation0 + i,
// frame_.rotationWord(i))`). The word counts are the GTE's own (guest_gte.h), not numbers written
// twice.
struct InBetweenCamera {
  std::uint32_t rotation[guest_gte::kRotationWords];
  // The classification pass's own matrix, unscaled. Read only by `classificationRotationWord`, and
  // only when there is no guest to read the guest's from.
  std::uint32_t viewRotation[guest_gte::kRotationWords];
  std::uint32_t position[3];
};

// How one ordering-table bin is laid out: retail's table is an array of (head, previous) word
// pairs, and the mark it publishes is a bin's SLOT ADDRESS. The ordinal is the same ordering with
// no address in it, which is what lets an in-between field's own table be compared against the same
// mark without either table being at a guest address.
inline constexpr std::uint32_t kOrderingTableStride = 8;
inline constexpr std::uint32_t kOrderingTableBytes = 0x1000; // 512 bins, the draw's own publish

// THE PRIMITIVE ARENA'S EXTENT. The drawer allocates UPWARD from the primitive cursor and stops
// `kPrimitiveArenaBytes` below the scratch block's TOP — that is, at `scratch +
// kPrimitiveArenaBytes`. The far pass ends its own primitive buffer at the same word
// (guest_terrain_far.cpp), and an in-between reserves exactly the same range, so the number lives
// here rather than in whichever of the three happened to need it first.
inline constexpr std::uint32_t kPrimitiveArenaBytes = 0x1000;

// One field's worth of drawing. `RealField` is the guest's own draw, byte for byte. `InBetween` is
// a frame between two of them: the same passes, the same data, host working memory, no guest call
// and no guest write.
enum class FrameMode : std::uint8_t { RealField, InBetween };

// What one `linkAndAdvance` did, all three parts of it the passes need. `head` is the bin's
// previous head (zero when the bin was empty, which is what the far pass's mark test keys on);
// `primitive` is the packet just linked, which retail leaves in a register for its own v0;
// `markKey` is the bin's ordinal, the mark comparison's own key.
struct Linked {
  std::uint32_t head = 0;
  std::uint32_t primitive = 0;
  std::uint32_t markKey = 0;
};

struct TerrainFrame {
  TerrainFrame(Core &core,
               const Facts &facts,
               const guest_render_globals::Globals &globals,
               ScreenBounds bounds,
               TerrainMemory &memory,
               FrameMode mode)
      : core(core), facts(facts), globals(globals), bounds(bounds), memory(memory), mode(mode) {}

  // Link the primitive at `primitive` as the new head of ordering-table bin `bin`, advance past its
  // `bytes` bytes, and report what it did (and the same five instructions in every pass; which
  // registers they leave the head and `primitive >> 16` in differs by pass).
  //
  // `orderingTable` is the frame's own member, the BASE the guest published and the detail pass
  // loaded -- not `globals.orderingTable`, which is the WORD ADDRESS it was loaded from. Retail
  // indexes the table through the published base, so this must too.
  Linked linkAndAdvance(std::uint32_t bin, std::uint32_t bytes) {
    const std::uint32_t slot = markSlot(bin);
    // A bin the IN-BETWEEN has not linked into yet is empty, whatever the guest's table says.
    //
    // This is not a convenience. The guest's table is not 512 bins: its far pass names bins well
    // past 512 (MEASURED 2026-10-02 on SCUS_944.25: bin 573 at table 0x801DA44C, against a 512-bin
    // `kOrderingTableBytes`), and those high bins are exactly where the guest's published mark sits
    // — the mark IS a bin's slot address, and the high bins are adjacent to it. So the bytes above
    // the table hold the LAST real field's packets, and a bin read taken at face value would link
    // this field's packet behind one of them and hand the sink a chain the guest never built.
    //
    // A real field reads the guest's own table as retail did, which is the whole point: its bins
    // really do carry the previous field's chains, and the guest's own mark is what tells its draw
    // where they begin. Nothing about that is approximated here.
    if (!realField()) {
      if (bin >= linkedBins_.size()) {
        linkedBins_.resize(bin + 1, 0);
      }
    }
    const std::uint32_t head = (!realField() && linkedBins_[bin] == 0) ? 0u : memory.r32(slot);
    memory.w32(slot, primitive);
    if (!realField()) {
      linkedBins_[bin] = 1;
    }
    if (head == 0) {
      memory.w32(slot + 4, primitive);
    } else {
      linkAfter(head, primitive);
    }
    // A packet ends the chain until something is linked after it, and retail gets that for free:
    // the guest's packet pool is zeroed when the guest allocates from it, so a fresh packet's next
    // field already reads 0. An in-between's pool is host memory seeded from whatever the guest
    // left at those addresses, so the terminator has to be STATED on every packet — not only a
    // bin's first, because a later packet's next field is stale too, and an unstated one walks the
    // chain out of the packets this field built and into stale guest bytes, which decode to wild
    // vertices.
    //
    // A real field writes nothing here, which is what keeps it byte-identical to retail.
    if (!realField()) {
      linkAfter(primitive, 0);
    }
    if (arena != nullptr) {
      arena->packets.push_back(primitive);
      // One past this packet's LAST word, which is what makes the consumer's containment test
      // exact: the record's range then covers every address the pass allocated and nothing after
      // it.
      arena->end = std::max(arena->end, primitive + bytes);
    }
    const Linked linked{head, primitive, bin};
    primitive += bytes;
    return linked;
  }

  // Make the packet at `packet` point at `next`: its low 24 bits, the tag's address field.
  void linkAfter(std::uint32_t packet, std::uint32_t next) {
    memory.w16(packet, static_cast<std::uint16_t>(next));
    memory.w8(packet + 2, static_cast<std::uint8_t>(next >> 16));
  }

  // The guest's slot address for a bin, and the ordinal that address stands for. Retail publishes
  // the mark as a slot address and the drawer's own entry sets it one past the last bin, so the
  // ordinal form of that initial value is `kOrderingTableBytes / kOrderingTableStride`.
  [[nodiscard]] std::uint32_t markSlot(std::uint32_t bin) const {
    return orderingTable + bin * kOrderingTableStride;
  }
  [[nodiscard]] std::uint32_t markBinFrom(std::uint32_t slot) const {
    return (slot - orderingTable) / kOrderingTableStride;
  }

  // Whether this field is the guest's own. Everything the guest must see, and every call only the
  // guest can make, is behind this one answer.
  [[nodiscard]] bool realField() const {
    return mode == FrameMode::RealField;
  }

  [[nodiscard]] std::uint32_t rotationWord(std::uint32_t index) const {
    if (inBetweenCamera != nullptr) {
      return inBetweenCamera->rotation[index];
    }
    return core.mem_r32(globals.cameraRotation + 4 * index);
  }
  // THE CAMERA THE CLASSIFICATION ROTATES BY.
  //
  // The classification pass does not read the camera rotation: it reads a SECOND matrix the guest
  // keeps beside it (this image's `facts.classify.viewRotation`), which its bounding-sphere tests
  // use. A real field reads the guest's own word and is bit-identical to retail whatever the guest
  // has put there. An in-between has no guest to read, so it uses the one it built from the same
  // three angles — the builder writes both, one scaled and one not, exactly as the guest does.
  [[nodiscard]] std::uint32_t classificationRotationWord(std::uint32_t index) const {
    if (inBetweenCamera != nullptr) {
      return inBetweenCamera->viewRotation[index];
    }
    return core.mem_r32(facts.classify.viewRotation + 4 * index);
  }
  // WHERE THIS FIELD'S PACKETS START. The guest's own cursor for a real field; the window the
  // caller gave an in-between (guest_terrain_drawer.h says why it needs one).
  [[nodiscard]] std::uint32_t primitiveBase() const {
    return inBetweenArenaBase != 0 ? inBetweenArenaBase : core.mem_r32(globals.primitiveCursor);
  }
  [[nodiscard]] std::uint32_t positionWord(std::uint32_t index) const {
    if (inBetweenCamera != nullptr) {
      return inBetweenCamera->position[index];
    }
    return core.mem_r32(globals.cameraPosition + 4 * index);
  }

  // THE CAMERA THE RENDER READS, through the frame rather than through the globals word. A real
  // field has no override: `inBetweenCamera` is a POINTER that only an in-between ever sets, so the
  // guest's own field reads the guest's own words exactly as retail did, and there is no copy of
  // that camera anywhere to drift from bytes the guest can also change.
  const InBetweenCamera *inBetweenCamera = nullptr;
  // The packet arena an in-between was given, 0 for a real field. See `primitiveBase`.
  std::uint32_t inBetweenArenaBase = 0;

  Core &core;
  const Facts &facts;
  const guest_render_globals::Globals &globals;
  const ScreenBounds bounds;
  TerrainMemory &memory;
  const FrameMode mode;
  std::uint32_t scratch = 0;   // the scratch block base, VXY1
  std::uint32_t primitive = 0; // fp: the next free primitive packet
  // The record of the pass's OWN linked packets, and the arena it allocated them into — null unless
  // this field is a REAL one, because only the guest's own pass has packets a captured queue can
  // hold. Set once by Drawer::run at the pass's starting cursor; the record's `end` grows with the
  // pass, and it is what makes a consumer's "which pass produced this address" answer exact.
  spyro::Context::TerrainPacketArena *arena = nullptr;
  std::uint32_t orderingTable = 0;     // the ordering table's bin 0
  std::uint32_t textures = 0;          // the level's 48-byte texture records
  std::uint32_t fineSplitCursor = 0;   // next free kFineSplitList word
  std::uint32_t coarseSplitCursor = 0; // next free kCoarseSplitList word
  std::uint32_t v0 = 0;
  std::uint32_t v1 = 0;

  // Which ordering bins THIS traversal has linked into. One byte per bin, grown to the highest bin
  // the traversal names, so there is no a-priori ceiling on how far above 512 the far pass may
  // reach. An in-between's only; a real field reads the guest's own table and has no use for it.
  std::vector<std::uint8_t> linkedBins_;

public:
  // The bins THIS traversal linked, for whoever walks the table afterwards. Empty for a real field:
  // its table is the guest's own and is walked as the guest's own.
  [[nodiscard]] const std::vector<std::uint8_t> &linkedBins() const {
    return linkedBins_;
  }
};

} // namespace spyro::guest_terrain