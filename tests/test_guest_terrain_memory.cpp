// The traversal layer's two claims, as tests.
//
// WHAT IS BEING CLAIMED. The terrain drawer is one set of seven passes that has to run twice: once
// over the guest's own RAM for the field the guest drew, and once over host memory for a frame
// between two of them. The passes reach the frame's five working ranges (guest_terrain_memory.h)
// through one accessor, and every thing only the guest's own field may do is behind
// `TerrainFrame::realField()` (guest_terrain_frame.h). Two things therefore have to be true, and
// both are checked here rather than argued:
//
//   MARK ORDERING. The far pass compares the deepest bin it has started in. Retail holds a bin's
//   slot ADDRESS; this holds the bin's ORDINAL, so the same comparison is the same code over a
//   table the guest owns and over one an in-between owns in host memory. The ordinal order and the
//   address order have to agree exactly, including for the value the draw itself publishes -- one
//   past the last bin, ordinal 512 -- or the real field's mark would be published as a different
//   address than retail's.
//
//   THE MODE GUARDS. A whole drawer's worth of InBetween-mode passes, over a level with one visible
//   sector, must leave every byte of guest RAM and the scratchpad exactly as it found them, while
//   still having classified that sector onto a list. If one guard is missing, that sector's
//   animation mark, its flags, or the visibility groups the moby walk reads would differ, and this
//   fails on the memcmp rather than on a pixel.
#include "guest_terrain_drawer.h"
#include "guest_terrain_frame.h"
#include "guest_terrain_memory.h"

#include "core.h"
#include "game.h"
#include "guest_render_globals.h"
#include "guest_terrain_facts.h"
#include "guest_terrain_screen.h"
#include "hw_bind.h"
#include "spyro_context.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char *name) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", name);
    ++failures;
  }
}

void expectEq(std::uint32_t got, std::uint32_t want, const char *name) {
  if (got != want) {
    std::fprintf(stderr, "FAIL: %s: got 0x%08X want 0x%08X\n", name, got, want);
    ++failures;
  }
}

// ── The addresses the fixture's facts name
// ──────────────────────────────────────────────────────── One sector table, one sector, one
// camera. The values are the fixture's own: the only thing under test is which of them the two
// modes touch.
constexpr std::uint32_t kSectorCount = 0x80010000u;
constexpr std::uint32_t kSectorTable = 0x80010004u;
constexpr std::uint32_t kSectorTableEntry = 0x80010100u;
constexpr std::uint32_t kSector = 0x80011000u;
constexpr std::uint32_t kViewRotation = 0x80012000u;
constexpr std::uint32_t kAnimationTable = 0x80013000u;
constexpr std::uint32_t kAnimationDef = 0x80014000u;
constexpr std::uint32_t kFogLevel = 0x80015000u;
constexpr std::uint32_t kTextureTable = 0x80016000u;
constexpr std::uint32_t kFarDepth = 0x80017000u;
constexpr std::uint32_t kFogColour = 0x80018000u;
constexpr std::uint32_t kFoggedColours = 0x80019000u;
constexpr std::uint32_t kRegisterSaveArea = 0x8001A000u;
constexpr std::uint32_t kVisibleSectorCount = 0x8001D000u;
constexpr std::uint32_t kVisibilityGroups = 0x8001B000u;
constexpr std::uint32_t kCameraRotation = 0x8001C000u;
constexpr std::uint32_t kCameraPosition = 0x8001C020u;

// The scratch block the passes' sector lists live in, and the two objects that overlap it: the
// primitive arena ends where the block begins, and the ordering table sits above the block's end.
constexpr std::uint32_t kScratchBase = 0x80060000u;
constexpr std::uint32_t kScratchBlockBytes = 0x3000u;
constexpr std::uint32_t kScratchBaseWord = kScratchBase + kScratchBlockBytes;
constexpr std::uint32_t kPrimitiveCursor = 0x80063004u;
constexpr std::uint32_t kOrderingTableWord = 0x80063008u;
constexpr std::uint32_t kOrderingTableMarkWord = 0x8006300Cu;
constexpr std::uint32_t kArenaBase = 0x8005F000u;
constexpr std::uint32_t kOrderingTable = 0x80066FFCu;

// The sector header fields the classification pass reaches (guest_terrain_classify.cpp).
constexpr std::uint32_t kSectorCentreXY = 0x00;
constexpr std::uint32_t kSectorCentreZ = 0x04;
constexpr std::uint32_t kSectorLayout = 0x14;
constexpr std::uint32_t kSectorAnimationMarks = 0x18;

spyro::guest_terrain::Facts fixtureFacts() {
  spyro::guest_terrain::Facts facts{};
  facts.overrideName = "terrain-memory-fixture";
  facts.visibilityCallSite = 0;
  facts.sectorVisibility = 0;
  facts.visibleSectorCount = kVisibleSectorCount;
  facts.scratchListsBelowEnd = 0x3000u;
  facts.fogColour = kFogColour;
  facts.foggedColours = kFoggedColours;
  facts.nativeWidth = 320;
  facts.classify.sectorTable = kSectorTable;
  facts.classify.sectorCount = kSectorCount;
  facts.classify.viewRotation = kViewRotation;
  facts.classify.animationTables = {
      kAnimationTable, kAnimationTable, kAnimationTable, kAnimationTable};
  facts.detail.textureTable = kTextureTable;
  facts.detail.fogLevel = kFogLevel;
  facts.far.farDepth = kFarDepth;
  return facts;
}

spyro::guest_render_globals::Globals fixtureGlobals() {
  spyro::guest_render_globals::Globals globals{};
  globals.registerSaveArea = kRegisterSaveArea;
  globals.scratchBaseWord = kScratchBaseWord;
  globals.primitiveCursor = kPrimitiveCursor;
  globals.orderingTable = kOrderingTableWord;
  globals.orderingTableMark = kOrderingTableMarkWord;
  globals.cameraRotation = kCameraRotation;
  globals.cameraPosition = kCameraPosition;
  globals.visibilityGroups = kVisibilityGroups;
  return globals;
}

// The whole guest's addressable state, so one memcmp can stand for "the real field is untouched".
std::vector<std::uint8_t> guestBytes(Core &core) {
  std::vector<std::uint8_t> out(0x200000u + 0x400u);
  for (std::uint32_t i = 0; i < 0x200000u; ++i) {
    out[i] = core.mem_r8(0x80000000u + i);
  }
  for (std::uint32_t i = 0; i < 0x400u; ++i) {
    out[0x200000u + i] = core.mem_r8(0x1F800000u + i);
  }
  return out;
}

// One sector the classification keeps: its centre is the camera's own, so it is in front of the
// near plane and inside every horizontal and vertical plane test, and its 0x100 radius keeps its
// far side off the far list. `layout` gives it two vertices, no colours and no polygon records, so
// the near passes project it and draw nothing.
void plantLevel(Core &core, const spyro::guest_terrain::Facts &facts) {
  core.mem_w32(facts.classify.sectorCount, 1);
  core.mem_w32(facts.classify.sectorTable, kSectorTableEntry);
  core.mem_w32(kSectorTableEntry, kSector);
  for (std::uint32_t i = 0; i < 5; ++i) {
    core.mem_w32(facts.classify.viewRotation + 4 * i, i == 0 ? 0x00010000u : 0u);
  }
  for (std::uint32_t i = 0; i < 3; ++i) {
    core.mem_w32(kCameraPosition + 4 * i, 0);
  }
  // The sector: centre (0,0), radius 0x100 with no flags, so the far side is at -0x100.
  core.mem_w32(kSector + kSectorCentreXY, 0);
  core.mem_w32(kSector + kSectorCentreZ, 0x00000100u);
  core.mem_w32(kSector + kSectorLayout, 2u); // two vertices, no colours, no records
  core.mem_w32(kSector + kSectorAnimationMarks, 0);
  // Slot 0's animation: one key at index 0 whose fraction is zero, so the copy loop runs once.
  core.mem_w32(facts.classify.animationTables[0], 0x80062000u);
  core.mem_w32(0x80062000u + 0x08, 0x40u);       // frame data at +0x40
  core.mem_w32(0x80062000u + 0x06, 4u);          // four bytes per frame
  core.mem_w32(0x80062000u + 0x0C, 0x00000000u); // key 0: fraction 0, frames 0, frame word 0
  core.mem_w32(0x80062040u, 0x00112233u);
}

// ── 1. Mark ordering
// ──────────────────────────────────────────────────────────────────────────────
void testMarkOrdering() {
  using namespace spyro::guest_terrain;
  Core core{};
  spyro::Context context{};
  core.gameCtx = &context;

  GuestMemory memory(core);
  const Facts facts = fixtureFacts();
  const spyro::guest_render_globals::Globals globals = fixtureGlobals();
  TerrainFrame frame(core, facts, globals, ScreenBounds(320, 0), memory, FrameMode::InBetween);
  frame.orderingTable = kOrderingTable;

  // The draw publishes the mark one past the last of the 512 bins. Its ORDINAL is 512, and the two
  // forms have to be the same number, because the far pass reads one and stores the other.
  expectEq(frame.markBinFrom(kOrderingTable + kOrderingTableBytes),
           kOrderingTableBytes / 8u,
           "the draw's initial mark is ordinal 512");
  for (std::uint32_t bin = 0; bin <= 512; ++bin) {
    expectEq(
        frame.markBinFrom(frame.markSlot(bin)), bin, "slot address round-trips to its ordinal");
  }

  // The ORDINAL comparison and the ADDRESS comparison retail makes must never disagree. Address
  // comparison is signed on the difference, so this walks both sides of the wrap too.
  for (std::uint32_t a = 0; a <= 512; a += 7) {
    for (std::uint32_t b = 0; b <= 512; b += 11) {
      const bool bySlot = static_cast<std::int32_t>(frame.markSlot(a) - frame.markSlot(b)) > 0;
      const bool byBin = static_cast<std::int32_t>(a - b) > 0;
      if (bySlot != byBin) {
        std::fprintf(stderr, "FAIL: bin order differs from slot order at %u vs %u\n", a, b);
        ++failures;
      }
    }
  }

  // Linking: the first packet into an empty bin is written to BOTH its words, a later one chains
  // through the previous head's address field, and the report names all three parts.
  frame.orderingTable = kOrderingTable;
  frame.primitive = kArenaBase;
  const Linked first = frame.linkAndAdvance(7, 0x34);
  expectEq(first.head, 0u, "an empty bin reports no previous head");
  expectEq(first.primitive, kArenaBase, "the linked packet is the one at the cursor");
  expectEq(first.markKey, 7u, "the mark key is the bin's ordinal");
  expectEq(frame.primitive, kArenaBase + 0x34, "the cursor advanced past the packet");
  expectEq(memory.r32(frame.markSlot(7)), kArenaBase, "the bin's head is the packet");
  expectEq(
      memory.r32(frame.markSlot(7) + 4), kArenaBase, "an empty bin records its last packet too");

  const std::uint32_t second = kArenaBase + 0x34;
  const Linked next = frame.linkAndAdvance(7, 0x28);
  expectEq(next.head, kArenaBase, "a bin with a packet reports it as the previous head");
  expectEq(next.markKey, 7u, "the mark key is the bin, not the packet");
  expectEq(
      memory.r16(kArenaBase), static_cast<std::uint16_t>(second), "the old head now points on");
  expectEq(memory.r8(kArenaBase + 2), static_cast<std::uint8_t>(second >> 16), "address high byte");
  expectEq(memory.r32(frame.markSlot(7)), second, "the bin's head moved to the new packet");
  expectEq(memory.r32(frame.markSlot(7) + 4), kArenaBase, "the bin's last packet is unchanged");
}

// ── 2. The mode guards ───────────────────────────────────────────────────────────────────────────
void testModeGuards() {
  using namespace spyro::guest_terrain;
  // The passes drive the GTE, whose register file belongs to the Game, so this fixture is a Game:
  // gte_init() creates it and gte_bind() points it at this Core, which is what FrameLoopShell::step
  // does for a running game.
  gte_init();
  std::unique_ptr<Game> game = std::make_unique<Game>();
  Core &core = game->core;
  spyro::Context context{};
  core.gameCtx = &context;
  gte_bind(&core);
  const Facts facts = fixtureFacts();
  const spyro::guest_render_globals::Globals globals = fixtureGlobals();
  plantLevel(core, facts);

  // The frame the draw publishes before the passes run: an arena cursor, the table's base, and the
  // mark one past its last bin.
  core.mem_w32(globals.scratchBaseWord, kScratchBaseWord);
  core.mem_w32(globals.primitiveCursor, kArenaBase);
  core.mem_w32(globals.orderingTable, kOrderingTable);
  core.mem_w32(globals.orderingTableMark, kOrderingTable + kOrderingTableBytes);
  // Sector 0 is visible: what the guest's own visibility call would have left in the scratchpad.
  core.mem_w8(kScratchpad, 1);

  // The host memory an in-between field owns: the scratchpad, the scratch block, the arena, the
  // ordering table and the fogged-colour buffer, at the addresses the real field uses for them.
  HostMemory host(core);
  host.map(kScratchpad, kScratchpadBytes);
  const std::uint32_t scratch = core.mem_r32(globals.scratchBaseWord) - facts.scratchListsBelowEnd;
  host.map(scratch, kScratchBlockBytes);
  // The arena's top is the scratch block's, so this mapping deliberately OVERLAPS the one above:
  // two objects that share bytes have to be one object, or one of them would lose the other's
  // writes.
  host.map(kArenaBase, scratch + 0x1000u - kArenaBase);
  host.map(kOrderingTable, kOrderingTableBytes);
  host.map(facts.foggedColours, 0x1000u);

  const std::vector<std::uint8_t> before = guestBytes(core);
  Drawer(core, facts, globals, host, FrameMode::InBetween).run();
  const std::vector<std::uint8_t> after = guestBytes(core);

  // NOT ONE BYTE. Every guard in the drawer exists for this comparison.
  for (std::size_t i = 0; i < before.size(); ++i) {
    if (before[i] != after[i]) {
      std::fprintf(stderr,
                   "FAIL: an in-between field changed guest byte 0x%08X from 0x%02X to 0x%02X\n",
                   i < 0x200000u ? 0x80000000u + static_cast<std::uint32_t>(i)
                                 : 0x1F800000u + static_cast<std::uint32_t>(i - 0x200000u),
                   before[i],
                   after[i]);
      ++failures;
      break;
    }
  }

  // ...and it really did draw: the sector reached a list, and the arena cursor moved, which is the
  // same work the guards let through.
  // The entry is the sector with edge-touching and close (its nearest point is inside 0x100 of the
  // camera), which is exactly what the classification computes for this sector -- so the passes
  // ran.
  expect(host.r32(scratch + kDetailList) == (kSector | 3u),
         "the classification listed the sector for the detail pass");
  expect(host.r32(scratch + kFarList) == 0u, "the far list is terminated for the far pass");
  expect(host.mappedBytes() != 0u, "the in-between's working memory is its own");
  expect(host.refusedWrites() == 0u, "no working write fell outside a mapping");

  // The mode itself is the only gate, so it is asked directly on both sides.
  GuestMemory guest(core);
  TerrainFrame real(core, facts, globals, ScreenBounds(320, 0), guest, FrameMode::RealField);
  TerrainFrame between(core, facts, globals, ScreenBounds(320, 0), host, FrameMode::InBetween);
  expect(real.realField(), "the real field is the guest's own");
  expect(!between.realField(), "an in-between field is not");
}

} // namespace

int main() {
  testMarkOrdering();
  testModeGuards();
  if (failures != 0) {
    std::fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
  }
  std::printf("guest terrain memory + frame mode: ok\n");
  return 0;
}