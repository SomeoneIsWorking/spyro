// terrain_world_pass.h — Spyro's in-between field: the guest's own terrain drawer, run again at a
// camera between two of the guest's own.
//
// WHAT THIS IS NOT. It is not a second renderer and it is not a transform of the captured frame.
// The real field is the game's untouched GPU output and this never touches it: no guest code runs,
// no guest byte is written, and the captured frame's non-terrain items are left exactly where they
// were. What it does is run the SAME measured terrain routine (guest_terrain::Drawer — one
// implementation for SCUS_944.25 and SCUS_944.67) a second time, over host memory, with the camera
// lerped between the two real fields' cameras, and submit the packets it produces.
//
// WHY RE-RUNNING BEATS INTERPOLATING THE CAPTURED PACKETS. The captured terrain is a set of
// already- projected triangles with no depth of their own — its depth lives entirely in the
// ordering table the drawer linked it into, which the capture does not keep. Interpolating screen
// positions of triangles that were selected by a different camera's visibility and split decisions
// produces a picture that was never on screen at any instant: pop as sectors enter and leave, and
// cracks as splits move. Running the drawer produces a field the guest's own routine would have
// produced for that camera.
//
// THE THREE INPUTS, AND WHY EACH IS WHAT IT IS:
//
//   the camera — the guest's own STATE, not its output: the three 16-bit angles its camera builder
//     reads (0x80067EC8 / 0x8006E03C), interpolated each the short way round its 4096-step turn and
//     turned back into the two matrices by that same builder over host memory
//     (guest_camera_builder.h). The world position is interpolated as the single signed 32-bit
//     fixed-point values it is. A real field reads the guest's own words and never comes near any
//     of this.
//
//   the sector visibility — the guest decided it, by asking its own code, once per real field. The
//     in-between cannot ask: there is no guest to ask, and running the guest's visibility walk
//     would be guest code. So the real field's answer is CAPTURED and handed to the in-between,
//     which draws the union of the two fields' visible sectors. A sector visible in either field is
//     drawn, because dropping it would show the level's interior; a sector visible in neither is
//     not, because the guest never classified it. This is the one place the in-between is
//     deliberately a superset of both endpoints, and that is what makes a fast pan continuous.
//
//   the sector animation — read from the level's own records, which are one real field stale at
//   most.
//     Advancing them would be a guest write; un-advancing them is not possible either, because the
//     previous field already advanced them. So the in-between presents the animation state the
//     current real field has, which is the same choice every other animation on the field makes.
#pragma once

#include "in_between_strategy.h"

#include "guest_camera_builder.h"
#include "guest_render_globals.h"
#include "guest_terrain_facts.h"
#include "guest_terrain_frame.h"
#include "guest_terrain_memory.h"
#include "terrain_packet_sink.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

class Game;

namespace spyro {

// One captured camera: the guest's own words, verbatim, plus the sector set its visibility call
// left. Two of these are the endpoints; the in-between is a camera between them.
struct TerrainField {
  guest_terrain::InBetweenCamera camera{};
  // THE GUEST'S OWN CAMERA STATE: the three angles its builder reads. The `camera` above is what
  // the builder makes of these, so these are what an in-between interpolates and the builder is run
  // again over them (guest_camera_builder.h).
  guest_camera::Angles angles{};
  // The guest's visible-sector bytes, copied out of its scratchpad. The drawer's sector-visibility
  // publication is real-field-only (the guest reads it afterwards), so this is the in-between's
  // copy and it is restored before the in-between's classification reads it.
  std::vector<std::uint8_t> visibleSectors;
  // THE CLIP WINDOW, from the guest's own captured terrain. Every captured RqItem records the draw
  // area its own enqueue saw, so this is the rectangle the guest's identical packet stream was
  // clipped to — read from the guest's own capture, not re-derived and not taken from the host's
  // latched rectangle, which the widescreen owner has widened since and which would let the
  // in-between paint into the letterbox the guest never drew in.
  //
  // It matters because the in-between's camera is BETWEEN two of the guest's, so its ground reaches
  // nearer than either endpoint's did: on SCUS_944.25 at t=0.5 the reconstruction submitted
  // straddling primitives down to y=239 while the guest's own window ends at y=227, and those
  // twelve rows were painted with terrain the guest's frame leaves black.
  bool hasClip = false;
  int clipX0 = 0;
  int clipY0 = 0;
  int clipX1 = 0;
  int clipY1 = 0;
  bool captured = false;
};

// THE PACKET ARENA AN IN-BETWEEN GETS, as a function of where everything else it touches ends.
//
// The guest allocates its terrain packets out of one pool and grows it upward, between its previous
// buffer's scratch block and the scratch block of the buffer in use. An in-between has no such
// guarantee: by the time the presenter rebuilds the frame, the guest's cursor has already advanced
// past everything the field's own producers allocated, so a reconstruction started there runs out
// of room before the frame ends. MEASURED 2026-10-03 on SCUS_944.25: an in-between starting at
// 0x801D04B4 needed 35,468 bytes of packets with the scratch block beginning at 0x801D7444 — 6,844
// bytes short — and the packets it wrote over the scratch block's sector and split lists, which is
// what made walkSplitList read a split-list word of 0x0C000000 as a sector header and fault on
// 0x30000008 (SCUS_944.67's fault, on the same line, was this one).
//
// So the window is everything above `frameTop` — the end of the last range the traversal owns — up
// to `mainRamEnd`, page-aligned because the window above it ends the address space. The address
// must be a guest-shaped main-RAM one because a packet's chain link is a 24-bit main-RAM OFFSET
// (terrain_packet_sink.h), so a host-only address could not be linked; and nothing the traversal
// reads may live there, which the level's own records do not (they sit below every one of those
// ranges, and they are read through unmapped reads that fall through to the guest).
//
// `bytes == 0` is the refusal case: `frameTop` is at or above the end of the window, there is no
// arena, and the caller must not present a truncated picture. It is a refusal rather than a clamp
// because a clamped arena is a silently wrong picture, which is the one failure this path exists to
// avoid making.
struct PacketArena {
  std::uint32_t base = 0;
  std::uint32_t bytes = 0;
};

[[nodiscard]] inline PacketArena packetArenaWindow(std::uint32_t frameTop,
                                                   std::uint32_t mainRamEnd) {
  const std::uint32_t base = (frameTop + 0xFFFu) & ~0xFFFu;
  if (base >= mainRamEnd) {
    return PacketArena{};
  }
  return PacketArena{base, mainRamEnd - base};
}

// THE TITLE'S IN-BETWEEN. One instance per Game, owned by the title's Fps60.
//
// `facts` and `globals` are the title's measured image facts, not copies of guest state: they are
// the addresses this image's drawer reads, and they are constant for the process.
class TerrainWorldPass final : public InBetweenStrategy {
public:
  TerrainWorldPass(Game &game, guest_terrain::Facts facts, guest_render_globals::Globals globals);

  // Once per presented frame, before `eligible` is asked. This is where the real field's terrain is
  // identified and its camera and sector visibility captured: all three are facts about the frame
  // being presented, and the frame is only complete here.
  //
  // A presentation that carries no NEW scene tick is not a new frame of the game — Spyro's scene
  // runs at 30 Hz on a fraction of the 60 Hz display fields — so it leaves the endpoints where they
  // were instead of making both of them the same camera.
  void beginPresentation(Core &core, CapturedFrameView frame, bool interpolating) override;

  // The captured queue holds terrain, and there is a previous field to interpolate from. Nothing
  // else is eligible: a field with no terrain is a field whose picture this pass cannot improve.
  bool eligible(const Core &core) const override;

  // EXACTLY the packets this image's terrain drawer linked since the last delivered field.
  //
  // Measured 2026-10-02 on SCUS_944.25: of 1355 captured items in a field's queue, exactly 719
  // carry a packet address the drawer linked and none of the other 636 does — so this is the
  // terrain's identity in a captured queue, by address, with no vertex matching, no content
  // similarity and no position in the queue. The drawer runs twice per product step, so the set
  // holds both runs' links; only one belongs to the frame being captured, and that does not matter
  // here because the other run's addresses answer false for every one of its items.
  bool owns(const RqItem &item) const override;

  void reconstruct(Core &core, float t) override;

  // The captured frame IS the guest's finished geometry: the real field presents it verbatim, and
  // only the in-between slot reconstructs. Returning true here is what keeps the real frame's
  // pixels out of this strategy's reach entirely.
  bool capturedQueueIsComplete() const override {
    return true;
  }

  // THE CLAIM, and it is the reason this strategy may present on the Gte path.
  //
  // The in-between's geometry is the HOST's, rebuilt here — but it is rebuilt out of the guest's
  // OWN memory and the guest's OWN scene routine, running over host memory, with no guest call, no
  // guest write, and no host geometry of this strategy's own. That is the guest's picture at
  // another instant, the same claim GuestGeometrySceneSource makes for the captured primitives, so
  // it is permitted on the path that already ships guest geometry (Fps60::interpolationPermitted).
  //
  // The claim is falsifiable and this class is what makes it true: `reconstruct` reads the guest
  // only through TerrainMemory's unmapped reads and writes only through its host mappings, so a
  // guest write would be refused by name rather than performed.
  GuestPathClaim guestPathClaim() const override {
    return GuestPathClaim::HostRebuiltFromGuestMemory;
  }

  void rotate(Core &core) override;

  // How many packets the last reconstructed field submitted, and how many sectors its visibility
  // union held. Read by the product's own run log; the pair is what says whether the pass did
  // anything.
  struct Report {
    std::uint32_t packets = 0;
    std::uint32_t visibleSectors = 0;
    std::uint32_t factorPerMille = 0;
  };
  [[nodiscard]] const Report &report() const {
    return report_;
  }

private:
  // The camera the guest's globals currently describe, and its visible-sector bytes, as this
  // image's drawer would read them.
  void capture(Core &core, TerrainField &into) const;

  // The three angles at the guest's own camera object: the first two angles share the first word,
  // low half then high half, and the third is the low half of the second word. They are read as
  // three signed 16-bit values because that is what the guest's builder reads them as
  // (0x8001C2F8's `param_1[0..2]`, a `short *`).
  [[nodiscard]] guest_camera::Angles cameraAngles(Core &core) const;

  // Give the traversal a union of two endpoints' visible-sector sets, in the scratchpad address the
  // drawer's own classification reads. Restored afterwards, because the scratchpad is guest memory.
  class VisibilityUnion;

  Game &game_;
  const guest_terrain::Facts facts_;
  const guest_render_globals::Globals globals_;

  // The frame being presented, and the previous delivered frame. `present_` is what `owns` answers
  // from and what `reconstruct` builds on.
  TerrainField present_;
  TerrainField previous_;
  std::vector<spyro::Context::TerrainPacketArena> arenas_;
  bool ownsField_ = false;
  Report report_;

  // WHICH REAL SCENE FRAME THE ENDPOINTS ARE. `sceneTick_` is the guest's own scene-producer tick
  // at the last capture and `captured_` says one has happened at all, so a presentation with no new
  // scene tick leaves both endpoints alone; `sceneAdvanced_` is what `rotate` consumes, so the two
  // endpoints only ever become the two most recent REAL scene frames.
  std::uint32_t sceneTick_ = 0;
  bool captured_ = false;
  bool sceneAdvanced_ = false;
};

// The one way a title installs this. `facts` and `globals` are the title's MEASURED image facts —
// the addresses this image's drawer reads — not copies of guest state, so they are taken by value
// and are constant for the process.
std::unique_ptr<::InBetweenStrategy>
makeTerrainWorldPass(Game &game, guest_terrain::Facts facts, guest_render_globals::Globals globals);

} // namespace spyro
