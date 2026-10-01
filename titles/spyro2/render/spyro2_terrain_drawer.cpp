#include "spyro2_terrain_drawer.h"

#include "core.h"
#include "native_execution.h"
#include "spyro2_gte.h"
#include "spyro2_render_globals.h"
#include "spyro2_terrain_frame.h"
#include "spyro2_terrain_passes.h"
#include "spyro2_widescreen.h"
#include "spyro2_widescreen_math.h"

namespace spyro2 {
namespace {

constexpr std::string_view kOwner = "spyro2-terrain-drawer";
constexpr std::uint32_t kScratchListsBelowEnd = 0x3000u; // 80023BFC
constexpr std::uint32_t kVisibilityCallSite = 0x80023C04u;
constexpr std::uint32_t kSectorVisibility = 0x80048D18u;
constexpr std::uint32_t kVisibleSectorCount = 0x80067404u; // 80023C14: the call's v0

// The columns the presentation shows past each side of the authored 512 at the latched aspect.
std::int32_t horizontalMargin(Core &core) {
  const WidescreenOwner &widescreen = WidescreenOwner::of(core);
  if (!widescreen.latched() || !widescreen.plan().widescreen()) {
    return 0;
  }
  return widescreen_math::guestWindowLeft(widescreen.plan().projectionExtent.width);
}

void terrainDrawer(Core *core) {
  render_globals::spillBorrowedRegisters(*core);
  terrain::TerrainFrame frame(*core, terrain::ScreenBounds(horizontalMargin(*core)));
  frame.scratch = core->mem_r32(render_globals::kScratchBaseWord) - kScratchListsBelowEnd;
  gte_write_data(gte::kVxy1, frame.scratch);
  {
    spyro::PreservedReturnAddress returnAddress(*core);
    spyro::callGuestJumpedFrom(*core, kOwner, kVisibilityCallSite, kSectorVisibility, 0u);
  }
  core->mem_w32(kVisibleSectorCount, core->r[2]);
  terrain::classifySectors(frame);
  terrain::drawDetailSectors(frame);
  terrain::drawTranslucentSectors(frame);
  terrain::splitCoarsePolygons(frame);
  terrain::splitFinePolygons(frame);
  terrain::resplitOversizedPrimitives(frame);
  terrain::drawFarSectors(frame);
  // 80029118: the exit. Retail reloads the borrowed registers from the save area, which the native
  // passes never changed, and returns with whatever its last pass left in v0 and v1.
  core->mem_w32(render_globals::kPrimitiveCursor, frame.primitive);
  core->r[2] = frame.v0;
  core->r[3] = frame.v1;
}

} // namespace

void registerTerrainDrawerOverride(Core &core) {
  spyro::installNativeOverride(core, kTerrainDrawerEntry, kOwner, terrainDrawer);
}

} // namespace spyro2
