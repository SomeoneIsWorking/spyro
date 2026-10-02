// guest_terrain_drawer.h — the native terrain drawer of this engine family, as one routine.
//
// ONE ROUTINE, TWO IMAGES. The drawer is measured instruction-for-instruction identical between
// SCUS_944.25 (0x80023BB4) and SCUS_944.67 (0x80022378): both decompile to the same 5,487
// instructions, and the two decompilations differ in exactly one line (a `gp` spill the two builds
// place differently). Everything that differs between the images -- the entry address, the one
// `jal` the drawer executes, and the guest words it reads -- is a per-title fact, so the routine
// here is one implementation and the two titles differ only in the facts they hand it.
//
// An override is a plain `void (*)(Core *)`, so a title's facts cannot be captured by it. The
// title therefore binds them in a one-line thunk of its own (see the title's facts header) and this
// header declares only the routine that thunk runs.
//
// The passes it runs are declared in guest_terrain_passes.h and the state they hand on is
// guest_terrain_frame.h.
#pragma once

#include "core.h"
#include "guest_render_globals.h"
#include "guest_terrain_facts.h"
#include "guest_terrain_frame.h"
#include "guest_terrain_memory.h"

namespace spyro::guest_terrain {

// One call of the drawer over one field's working memory.
//
// The SAME seven passes run either way. What differs is the working memory they read and write
// (`GuestMemory` for the guest's own bytes, `HostMemory` for an in-between's), and the five things
// only the guest's own field may do: spill retail's borrowed registers, ask the guest which sectors
// are visible, animate the level's sector data, publish the visibility groups the moby walk reads,
// and store the primitive cursor and v0/v1 the guest's caller will read. Those are the guards, and
// they are the only difference between the two calls.
class Drawer {
public:
  Drawer(Core &core,
         const Facts &facts,
         const guest_render_globals::Globals &globals,
         TerrainMemory &memory,
         FrameMode mode);

  void run();

private:
  Core &core_;
  const Facts &facts_;
  const guest_render_globals::Globals &globals_;
  TerrainMemory &memory_;
  const FrameMode mode_;
};

// Run one call of the drawer against this image: spill retail's borrowed registers, ask the guest
// for its visible sectors through the one `jal` the retail body executes, then run the passes in
// retail order. No guest hand-off: the retail body never leaves itself.
//
// This is the real field, and it is the override entry: it owns a `GuestMemory` for its own length
// and runs in `FrameMode::RealField`, so every guard inside the passes is open.
void draw(Core &core, const Facts &facts, const guest_render_globals::Globals &globals);

} // namespace spyro::guest_terrain