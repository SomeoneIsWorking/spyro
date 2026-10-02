#include "guest_terrain_drawer.h"

#include "core.h"
#include "guest_gte.h"
#include "guest_terrain_frame.h"
#include "guest_terrain_passes.h"
#include "guest_widescreen_owner.h"
#include "native_execution.h"

namespace spyro::guest_terrain {
namespace {

// The GTE register and command numbers are shared vocabulary (guest_gte.h), not this pass's.
namespace gte = guest_gte;

// The columns the presentation shows past each side of the authored window at the latched aspect.
// This is the ONE question every horizontal cull in this render path asks, and it is answered by
// the title's own widescreen owner rather than by re-deriving the margin from the plan here: two
// implementations of "how much wider is the picture" is two answers to one question.
//
// A Core whose projection leaves are not bound has no owner and is not widening, so it has no
// margin: retail's authored window is then the correct cull and this path is unchanged.
int horizontalMargin(Core &core) {
  const GuestWidescreenOwner *owner = GuestWidescreenOwner::of(core);
  return owner == nullptr ? 0 : owner->horizontalMargin();
}

} // namespace

Drawer::Drawer(Core &core,
               const Facts &facts,
               const guest_render_globals::Globals &globals,
               TerrainMemory &memory,
               FrameMode mode)
    : core_(core), facts_(facts), globals_(globals), memory_(memory), mode_(mode) {}

void Drawer::run() {
  const bool real = mode_ == FrameMode::RealField;
  // 80023BCE: the borrowed registers are spilled to the save area, which is guest RAM the level's
  // own code reads back at the drawer's own exit.
  if (real) {
    guest_render_globals::spillBorrowedRegisters(core_, globals_);
  }
  TerrainFrame frame(core_,
                     facts_,
                     globals_,
                     ScreenBounds(facts_.nativeWidth, horizontalMargin(core_)),
                     memory_,
                     mode_);
  frame.scratch = core_.mem_r32(globals_.scratchBaseWord) - facts_.scratchListsBelowEnd;
  gte_write_data(gte::kVxy1, frame.scratch);
  if (real) {
    // The drawer's one call out of its own body. Only the guest can answer it, so an in-between
    // field is handed the sectors its visibility was decided for, not asked again.
    {
      spyro::PreservedReturnAddress returnAddress(core_);
      spyro::callGuestJumpedFrom(
          core_, facts_.overrideName, facts_.visibilityCallSite, facts_.sectorVisibility, 0u);
    }
    core_.mem_w32(facts_.visibleSectorCount, core_.r[2]);
  }
  classifySectors(frame);
  drawDetailSectors(frame);
  drawTranslucentSectors(frame);
  splitCoarsePolygons(frame);
  splitFinePolygons(frame);
  resplitOversizedPrimitives(frame);
  drawFarSectors(frame);
  if (real) {
    // SCUS_944.25 80029118: the exit. Retail reloads the borrowed registers from the save area,
    // which the native passes never changed, and returns with whatever its last pass left in v0 and
    // v1. Both are the guest's own state: the cursor word is where the next pass allocates from and
    // v0/v1 are the O32 result registers the guest's caller reads.
    core_.mem_w32(globals_.primitiveCursor, frame.primitive);
    core_.r[2] = frame.v0;
    core_.r[3] = frame.v1;
  }
}

void draw(Core &core, const Facts &facts, const guest_render_globals::Globals &globals) {
  GuestMemory memory(core);
  Drawer(core, facts, globals, memory, FrameMode::RealField).run();
}

} // namespace spyro::guest_terrain