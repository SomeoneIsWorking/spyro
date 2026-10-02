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

void draw(Core &core, const Facts &facts, const guest_render_globals::Globals &globals) {
  guest_render_globals::spillBorrowedRegisters(core, globals);
  TerrainFrame frame(core, facts, globals, ScreenBounds(facts.nativeWidth, horizontalMargin(core)));
  frame.scratch = core.mem_r32(globals.scratchBaseWord) - facts.scratchListsBelowEnd;
  gte_write_data(gte::kVxy1, frame.scratch);
  {
    spyro::PreservedReturnAddress returnAddress(core);
    spyro::callGuestJumpedFrom(
        core, facts.overrideName, facts.visibilityCallSite, facts.sectorVisibility, 0u);
  }
  core.mem_w32(facts.visibleSectorCount, core.r[2]);
  classifySectors(frame);
  drawDetailSectors(frame);
  drawTranslucentSectors(frame);
  splitCoarsePolygons(frame);
  splitFinePolygons(frame);
  resplitOversizedPrimitives(frame);
  drawFarSectors(frame);
  // SCUS_944.25 80029118: the exit. Retail reloads the borrowed registers from the save area, which
  // the native passes never changed, and returns with whatever its last pass left in v0 and v1.
  core.mem_w32(globals.primitiveCursor, frame.primitive);
  core.r[2] = frame.v0;
  core.r[3] = frame.v1;
}

} // namespace spyro::guest_terrain
