// guest_terrain_passes.h — the passes of the terrain drawer of this engine family, in the
// order the drawer runs them. The addresses are SCUS_944.25's; SCUS_944.67's drawer is the same
// routine and runs the same passes in the same order. Each pass's file describes it, and
// guest_terrain_frame.h is the state they hand on.
#pragma once

namespace spyro::guest_terrain {

struct TerrainFrame;

void classifySectors(TerrainFrame &frame);            // 80023C0C: guest_terrain_classify.cpp
void drawDetailSectors(TerrainFrame &frame);          // 80024534: guest_terrain_detail.cpp
void drawTranslucentSectors(TerrainFrame &frame);     // 80025434: guest_terrain_translucent.cpp
void splitCoarsePolygons(TerrainFrame &frame);        // 80025CC8: guest_terrain_coarse.cpp
void splitFinePolygons(TerrainFrame &frame);          // 80026C74: guest_terrain_fine.cpp
void resplitOversizedPrimitives(TerrainFrame &frame); // 80028504: guest_terrain_resplit.cpp
void drawFarSectors(TerrainFrame &frame);             // 80028B14: guest_terrain_far.cpp

} // namespace spyro::guest_terrain
