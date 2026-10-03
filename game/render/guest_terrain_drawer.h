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
//
// `inBetweenCamera` is the camera an IN-BETWEEN presents, and only an in-between may pass one: the
// real field passes null and reads the guest's own words. A real field handed a camera would read a
// copy of the guest's camera that the guest can also read and change, which is exactly the drift
// this seam exists to make impossible.
#pragma once

#include "core.h"
#include "guest_render_globals.h"
#include "guest_terrain_facts.h"
#include "guest_terrain_frame.h"
#include "guest_terrain_memory.h"

#include <optional>

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
         FrameMode mode,
         const InBetweenCamera *inBetweenCamera = nullptr,
         std::uint32_t inBetweenArenaBase = 0);

  // Runs the seven passes and returns the frame they ran on, so a caller that walks the resulting
  // ordering table afterwards walks THIS field's table: which bins it linked is frame state, and a
  // second `run()` would produce a second frame with a second, empty record of them. The frame is
  // the drawer's own member, so the reference outlives the call.
  const TerrainFrame &run();

private:
  Core &core_;
  const Facts &facts_;
  const guest_render_globals::Globals &globals_;
  TerrainMemory &memory_;
  const FrameMode mode_;
  // Held, not returned from a local: the frame is what a caller walks the ordering table with, and
  // a reference to a local would be the exact bug this member exists to prevent.
  std::optional<TerrainFrame> frame_;
  // The camera an in-between presents; always null for a real field, which the constructor refuses.
  const InBetweenCamera *inBetweenCamera_ = nullptr;
  // WHERE AN IN-BETWEEN'S PACKETS GO, when it is not the guest's own cursor (0 = the guest's).
  //
  // The guest allocates its terrain packets out of one pool and grows it upward, between its
  // previous buffer's scratch block and the scratch block of the buffer in use. An in-between has
  // no such guarantee: by the time the presenter rebuilds the frame, the guest's cursor has already
  // advanced past everything the field's own producers allocated, so a reconstruction started there
  // runs out of room before the frame ends. MEASURED 2026-10-03 on SCUS_944.25: an in-between
  // starting at 0x801D04B4 needed 35,468 bytes of packets and the scratch block begins at
  // 0x801D7444 — 6,844 bytes short — and the packets it wrote over the scratch block's sector and
  // split lists, which is what made walkSplitList read a split-list word of 0x0C000000 as a sector
  // header. So the caller that owns the host windows gives the arena a window of its own, above
  // everything else the traversal touches.
  std::uint32_t inBetweenArenaBase_ = 0;
};

// Run one call of the drawer against this image: spill retail's borrowed registers, ask the guest
// for its visible sectors through the one `jal` the retail body executes, then run the passes in
// retail order. No guest hand-off: the retail body never leaves itself.
//
// This is the real field, and it is the override entry: it owns a `GuestMemory` for its own length
// and runs in `FrameMode::RealField`, so every guard inside the passes is open.
void draw(Core &core, const Facts &facts, const guest_render_globals::Globals &globals);

} // namespace spyro::guest_terrain