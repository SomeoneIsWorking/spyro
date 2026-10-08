// The native terrain drawer: one routine, shared by every image of this engine family.
#pragma once

#include "core.h"
#include "guest_render_globals.h"
#include "guest_terrain_facts.h"
#include "guest_terrain_frame.h"
#include "guest_terrain_memory.h"

#include <optional>

namespace spyro::guest_terrain {

// An override is a plain `void (*)(Core *)`, so a title cannot capture its facts in it: each title
// binds them in a one-line thunk of its own.
class Drawer {
public:
  Drawer(Core &core,
         const Facts &facts,
         const guest_render_globals::Globals &globals,
         TerrainMemory &memory,
         FrameMode mode,
         const InBetweenCamera *inBetweenCamera = nullptr,
         std::uint32_t inBetweenArenaBase = 0,
         std::int32_t inBetweenPacketBudget = 0);

  // Returns the frame the passes ran on so a caller that walks the ordering table afterwards walks
  // THIS field's table; a second `run()` would produce a second, empty record of the bins it
  // linked.
  const TerrainFrame &run();

private:
  Core &core_;
  const Facts &facts_;
  const guest_render_globals::Globals &globals_;
  TerrainMemory &memory_;
  const FrameMode mode_;
  // Held, not returned from a local: the frame is what a caller walks the ordering table with.
  std::optional<TerrainFrame> frame_;
  // The camera an in-between presents; always null for a real field, which the constructor refuses.
  const InBetweenCamera *inBetweenCamera_ = nullptr;
  // Where an in-between's packets go, when it is not the guest's own cursor (0 = the guest's).
  //
  // The guest allocates its terrain packets out of one pool and grows it upward, between its
  // previous buffer's scratch block and the scratch block of the buffer in use. An in-between has
  // no such guarantee: by the time the presenter rebuilds the frame the guest's cursor has already
  // advanced past everything the field's own producers allocated, so a reconstruction started there
  // can run out of room before the frame ends and overwrite the scratch block's sector and split
  // lists. The caller that owns the host windows gives the arena a window of its own.
  std::uint32_t inBetweenArenaBase_ = 0;
  // How many bytes of packets the far pass may link into that arena before it stops, as the real
  // field's pool allowed.
  std::int32_t inBetweenPacketBudget_ = 0;
};

// The override entry: it owns a `GuestMemory` for its own length and runs in
// `FrameMode::RealField`, so every guard inside the passes is open.
void draw(Core &core, const Facts &facts, const guest_render_globals::Globals &globals);

} // namespace spyro::guest_terrain