// spyro2_terrain_passes.h — the passes of Spyro 2's terrain drawer (SCUS_944.25 0x80023BB4), in the
// order the drawer runs them. Each pass's file describes it; spyro2_terrain_frame.h is the state
// they hand on.
#pragma once

namespace spyro2::terrain {

struct TerrainFrame;

void classifySectors(TerrainFrame &frame);            // 80023C0C: spyro2_terrain_classify.cpp
void drawDetailSectors(TerrainFrame &frame);          // 80024534: spyro2_terrain_detail.cpp
void drawTranslucentSectors(TerrainFrame &frame);     // 80025434: spyro2_terrain_translucent.cpp
void splitCoarsePolygons(TerrainFrame &frame);        // 80025CC8: spyro2_terrain_coarse.cpp
void splitFinePolygons(TerrainFrame &frame);          // 80026C74: spyro2_terrain_fine.cpp
void resplitOversizedPrimitives(TerrainFrame &frame); // 80028504: spyro2_terrain_resplit.cpp
void drawFarSectors(TerrainFrame &frame);             // 80028B14: spyro2_terrain_far.cpp

} // namespace spyro2::terrain
