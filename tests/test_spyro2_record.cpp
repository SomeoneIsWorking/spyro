// Spyro 2 on the Record path: the render path it selects, the cuts it declares, the depth bin each
// packet is assigned, and the terrain producer's render against the real draw over a synthetic
// level.
#include "core.h"
#include "game.h"
#include "gp0_primitive_decode.h"
#include "guest_call.h"
#include "guest_camera_builder.h"
#include "guest_render_globals.h"
#include "guest_terrain_facts.h"
#include "guest_terrain_frame.h"
#include "hw_bind.h"
#include "image_identity.h"
#include "native_dispatch.h"
#include "spyro2_depth_bins.h"
#include "spyro2_frame_cut.h"
#include "spyro2_render_facts.h"
#include "spyro2_runtime.h"
#include "spyro3_runtime.h"
#include "spyro_context.h"
#include "terrain_packet_sink.h"
#include "terrain_state_producer.h"
#include "testutil.h"

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace {

namespace terrain = spyro::guest_terrain;
using psx::present::DrawPrimitive;
using psx::present::OtSlot;

constexpr std::uint32_t kTerrainEntry = spyro2::kTerrainFacts.entry;
constexpr std::uint32_t kVisibilityLeaf = spyro2::kTerrainFacts.sectorVisibility;
constexpr std::uint32_t kFlatten = spyro2::depth_bins::kFlatten;
constexpr std::uint32_t kObject = 0x801D9044u; // the drawer's A0 object
constexpr std::uint32_t kTable = 0x80100000u;
constexpr std::uint32_t kPacketCursor = 0x80120000u;
constexpr std::uint32_t kScratchEnd = 0x80190000u;
constexpr std::uint32_t kSector = 0x80010000u;
constexpr std::uint32_t kSectorTable = 0x80010400u;
constexpr std::uint32_t kTextures = 0x80011000u;
// One image holding the three guest functions the fixture reaches, so the dispatcher resolves them.
constexpr std::uint32_t kImageStart = 0x1B000u;
constexpr std::uint32_t kImageEnd = 0x49000u;

void test_spyro2_replays_its_record_and_interpolates_it() {
  const spyro2::Spyro2Runtime spyro2;
  const RenderCapabilities caps = spyro2.renderCapabilities();
  CHECK(caps.defaultPath == RenderPath::Record);
  CHECK(caps.temporalInterpolation);
  CHECK(!caps.nativeRenderPath);
}

void test_spyro3_keeps_its_render_path() {
  const spyro3::Spyro3Runtime spyro3;
  CHECK(spyro3.renderCapabilities().defaultPath != RenderPath::Record);
}

// One step of the driver: a draw returns (walking its table), then the step's tail.
void drawThenTail(spyro2::FrameCut &cut, Core &core) {
  cut.onFrameDrawn(core);
  cut.onFrameTail(core);
}

void setScene(Core &core, std::uint32_t state, std::uint32_t level) {
  core.mem_w32(spyro2::FrameCut::kGameState, state);
  core.mem_w32(spyro2::FrameCut::kLevelId, level);
}

void test_a_cut_is_a_new_game_state_or_level() {
  auto game = std::make_unique<Game>();
  Core &core = game->core;
  spyro2::FrameCut cut;
  CHECK(cut.isCut()); // nothing sealed yet
  setScene(core, 0, 11);
  drawThenTail(cut, core);
  CHECK(cut.isCut()); // the first record has no predecessor
  drawThenTail(cut, core);
  CHECK(!cut.isCut());
  setScene(core, 1, 11);
  drawThenTail(cut, core);
  CHECK(cut.isCut());
  drawThenTail(cut, core);
  CHECK(!cut.isCut());
  setScene(core, 1, 12);
  drawThenTail(cut, core);
  CHECK(cut.isCut());
  // Anything else the guest changes, the camera included, is not a scene change.
  core.mem_w32(0x80067EACu, 0x1234u);
  drawThenTail(cut, core);
  CHECK(!cut.isCut());
}

// The scene is the one the walked table was drawn in, not the state at the tail: the update that
// runs after the draw returns may already have moved on.
void test_the_scene_is_sampled_when_the_table_is_walked() {
  auto game = std::make_unique<Game>();
  Core &core = game->core;
  spyro2::FrameCut cut;
  setScene(core, 0, 11);
  drawThenTail(cut, core);
  cut.onFrameDrawn(core);
  setScene(core, 5, 11); // the next update enters a load before the tail
  cut.onFrameTail(core);
  CHECK(!cut.isCut());
  drawThenTail(cut, core);
  CHECK(cut.isCut());
  // A step whose draw did not return walked no table: not a scene change, and the walk after it
  // compares with the last walk.
  cut.onFrameTail(core);
  CHECK(!cut.isCut());
  drawThenTail(cut, core);
  CHECK(!cut.isCut());
  setScene(core, 0, 12);
  cut.onFrameTail(core);
  CHECK(!cut.isCut());
  drawThenTail(cut, core);
  CHECK(cut.isCut());
}

void test_a_fresh_runtime_seals_a_cut_first() {
  auto game = std::make_unique<Game>();
  const spyro2::Spyro2Runtime spyro2;
  CHECK(spyro2.sealedFrameIsCut(game->core));
}

// The GTE constants retail's display bootstrap leaves: the screen centre of a 512x240 window and H.
void setProjection() {
  gte_write_ctrl(24, 256u << 16); // OFX
  gte_write_ctrl(25, 120u << 16); // OFY
  gte_write_ctrl(26, 341u);       // H
}

// The guest's visibility leaf, which marks the one sector of the synthetic level visible.
void visibilityLeaf(Core *core) {
  core->mem_w8(terrain::kScratchpad, 1);
  setProjection();
  core->r[2] = 1;
}

std::uint32_t vertexWord(std::uint32_t z, std::uint32_t x, std::uint32_t y) {
  return ((z / 4) << 21) | ((x / 4) << 10) | (y / 8);
}

std::uint32_t packedIndices(std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d) {
  return (a << 24) | (b << 16) | (c << 8) | d;
}

constexpr terrain::CameraState kRestCamera{{0, 0, 0}, {0x0u, 0x800u, 0x400u}};

// Spyro 2's drawer, flatten and visibility leaf over a one-sector level that links one GT4 and one
// GT3: guest memory only, no disc. The camera is the guest's own words, built by the shipping
// camera builder over sine and cosine tables filled here.
class Scene {
public:
  Scene() {
    gte_init();
    game_ = std::make_unique<Game>();
    core().gameCtx = &context_;
    gte_bind(&core());
    core().imageCatalog().activate("spyro2 synthetic terrain", {kImageStart, kImageEnd}, 2);
    core().mem_w32(kFlatten, 0x03E00008u); // jr ra, the flatten's original body
    spyro2::registerRenderOverrides(core());
    spyro2::depth_bins::registerOverrides(core());
    psx::cpu::installNativeOverride(
        core(), kVisibilityLeaf, "synthetic-visibility", visibilityLeaf);
    plantTables();
    plantLevel();
    plantFrame();
    setCamera(kRestCamera);
  }

  Core &core() {
    return game_->core;
  }

  // The guest's camera words for `camera`, as its builder writes them.
  void setCamera(const terrain::CameraState &camera) {
    const auto &globals = spyro2::kRenderGlobals;
    const spyro::guest_camera::Matrices matrices =
        spyro::guest_camera::Builder(core(), globals.cameraSineTable, globals.cameraCosineTable)
            .build({camera.angles[0], camera.angles[1], camera.angles[2]});
    for (std::uint32_t i = 0; i < 5; ++i) {
      core().mem_w32(globals.cameraRotation + 4 * i, matrices.projection[i]);
      core().mem_w32(spyro2::kTerrainFacts.classify.viewRotation + 4 * i, matrices.view[i]);
    }
    core().mem_w32(globals.cameraAngles,
                   (static_cast<std::uint32_t>(camera.angles[0]) & 0xFFFFu) |
                       (static_cast<std::uint32_t>(camera.angles[1]) << 16));
    core().mem_w32(globals.cameraAngles + 4,
                   static_cast<std::uint32_t>(camera.angles[2]) & 0xFFFFu);
    for (std::uint32_t i = 0; i < 3; ++i) {
      core().mem_w32(globals.cameraPosition + 4 * i, camera.position[i]);
    }
  }

  // The real draw, reached as the guest reaches it: through the dispatcher's producer scope.
  void drawTerrain() {
    setProjection();
    psx::cpu::callGuestNow(core(), "test", kTerrainEntry, kObject);
  }

  void flatten() {
    psx::cpu::callGuestNow(core(), "test", kFlatten);
  }

private:
  void plantTables() {
    const auto &globals = spyro2::kRenderGlobals;
    for (std::uint32_t i = 0; i < 257; ++i) {
      const double turn = 2.0 * 3.14159265358979323846 * static_cast<double>(i % 256) / 256.0;
      core().mem_w32(globals.cameraSineTable + 2 * i,
                     static_cast<std::uint32_t>(std::lround(std::sin(turn) * 4096.0)) & 0xFFFFu);
      core().mem_w32(globals.cameraCosineTable + 2 * i,
                     static_cast<std::uint32_t>(std::lround(std::cos(turn) * 4096.0)) & 0xFFFFu);
    }
  }

  // One detail-pass sector 0xC00 deep: six vertices, four colours, a quad and a triangle.
  void plantLevel() {
    const auto &facts = spyro2::kTerrainFacts;
    core().mem_w32(facts.classify.sectorCount, 1);
    core().mem_w32(facts.classify.sectorTable, kSectorTable);
    core().mem_w32(kSectorTable, kSector);
    core().mem_w32(facts.detail.textureTable, kTextures);
    core().mem_w32(facts.far.farDepth, 0x4650u);
    core().mem_w32(kSector + 0x00, (0x300u << 16) | 0x80u); // centre x, y in camera units / 16
    core().mem_w32(kSector + 0x04, (0x40u << 16) | 0x80u);  // centre z, radius, no flags
    core().mem_w32(kSector + 0x08, 0xC00u << 14);
    core().mem_w32(kSector + 0x0C, 0x1800u); // the polygon flag bytes, inside the scratch block
    core().mem_w32(kSector + 0x14, (2u << 16) | (4u << 8) | 6u);
    core().mem_w32(kSector + 0x18, 0xFFFF0000u); // animation marks already claimed
    const std::uint32_t vertices[6] = {vertexWord(0x00, 0x080, 0x040),
                                       vertexWord(0x00, 0x200, 0x040),
                                       vertexWord(0x40, 0x200, 0x100),
                                       vertexWord(0x40, 0x080, 0x100),
                                       vertexWord(0x80, 0x300, 0x0C0),
                                       vertexWord(0x80, 0x300, 0x180)};
    std::uint32_t at = kSector + 0x1C;
    for (const std::uint32_t word : vertices) {
      core().mem_w32(at, word);
      at += 4;
    }
    for (std::uint32_t i = 0; i < 4; ++i) { // near colours, then the far set
      core().mem_w32(at + 4 * i, 0x00A08060u + i * 0x000A0A0Au);
      core().mem_w32(at + 16 + 4 * i, 0x00402010u + i * 0x00050505u);
    }
    at += 32;
    core().mem_w32(at + 0x0, packedIndices(0, 1, 2, 3));
    core().mem_w32(at + 0x4, packedIndices(0, 1, 2, 3));
    core().mem_w32(at + 0xC, 0x800u);
    core().mem_w32(at + 0x10, packedIndices(1, 4, 2, 2));
    core().mem_w32(at + 0x14, packedIndices(0, 1, 2, 2));
    core().mem_w32(at + 0x1C, 0x801u);
    core().mem_w32(kTextures + 0, 0x7A200810u);
    core().mem_w32(kTextures + 4, 0x00092010u);
    core().mem_w32(kTextures + 48, 0x7A200C0Cu);
    core().mem_w32(kTextures + 52, 0x80090C0Cu); // sign bit: semi-transparent
  }

  void plantFrame() {
    const auto &globals = spyro2::kRenderGlobals;
    core().mem_w32(globals.orderingTable, kTable);
    core().mem_w32(globals.orderingTableMark, kTable + terrain::kOrderingTableBytes);
    core().mem_w32(globals.primitiveCursor, kPacketCursor);
    core().mem_w32(globals.scratchBaseWord, kScratchEnd);
  }

  spyro::Context context_{};
  std::unique_ptr<Game> game_;
};

struct Emitted {
  OtSlot slot;
  DrawPrimitive primitive;

  bool operator==(const Emitted &) const = default;
};

class Collect final : public psx::present::PrimitiveSink {
public:
  void emit(OtSlot slot, const DrawPrimitive &primitive) override {
    emitted.push_back({slot, primitive});
  }

  std::vector<Emitted> emitted;
};

// The packets the real draw linked, as the flatten's walk reaches them: the deepest bin first, each
// bin from the first packet linked on, with the bucket the title assigned each.
struct Linked {
  std::vector<Emitted> packets;
  std::vector<std::optional<OtSlot>> assigned;
  unsigned undecoded = 0;
};

Linked linkedPackets(Core &core) {
  namespace sink = spyro::terrain_packet_sink;
  Linked linked;
  const std::uint32_t table = core.mem_r32(spyro2::kRenderGlobals.orderingTable);
  for (std::int32_t bin = static_cast<std::int32_t>(sink::kBins) - 1; bin >= 0; --bin) {
    std::uint32_t packet = core.mem_r32(table + static_cast<std::uint32_t>(bin) * sink::kBinStride +
                                        sink::kBinFirstOffset);
    while (packet != 0) {
      const std::uint32_t tag = core.mem_r32(packet);
      std::vector<std::uint32_t> words(tag >> 24);
      for (std::uint32_t i = 0; i < words.size(); ++i) {
        words[i] = core.mem_r32(packet + 4 + 4 * i);
      }
      const auto primitive = psx::gpu::decodePacketPrimitive(words);
      if (primitive) {
        linked.packets.push_back(
            {OtSlot{spyro2::depth_bins::kTable, static_cast<std::uint32_t>(bin)}, *primitive});
        linked.assigned.push_back(core.otTables.assigned(packet));
      } else {
        ++linked.undecoded;
      }
      packet = (tag & 0xFFFFFFu) == 0 ? 0 : psx::gpu::guestAddressOf(tag & 0xFFFFFFu);
    }
  }
  return linked;
}

// The state the drawer saved for its object, collected by a record holding one of its packets.
std::optional<std::vector<std::byte>>
savedState(Core &core, const DrawPrimitive &packet, std::uint32_t address) {
  psx::present::FrameRecord record(1, true);
  DrawPrimitive keyed = packet;
  keyed.key = core.emission.keyFor(address);
  record.append(keyed);
  const psx::present::FrameState states = core.frameStates.collect(record);
  const auto found = states.find({kTerrainEntry, kObject});
  if (!found) {
    return std::nullopt;
  }
  return std::vector<std::byte>(found->begin(), found->end());
}

std::uint32_t firstLinkedPacket(Core &core) {
  namespace sink = spyro::terrain_packet_sink;
  const std::uint32_t table = core.mem_r32(spyro2::kRenderGlobals.orderingTable);
  for (std::int32_t bin = static_cast<std::int32_t>(sink::kBins) - 1; bin >= 0; --bin) {
    const std::uint32_t packet = core.mem_r32(
        table + static_cast<std::uint32_t>(bin) * sink::kBinStride + sink::kBinFirstOffset);
    if (packet != 0) {
      return packet;
    }
  }
  return 0;
}

// The flatten override hands every packet of every bin its bin, chains and singletons alike, and
// still runs the original body.
void test_the_flatten_override_assigns_each_packet_its_bin() {
  Scene scene;
  Core &core = scene.core();
  constexpr std::uint32_t kBinPair = 8;
  constexpr std::uint32_t kPackets[4] = {0x80130000u, 0x80130040u, 0x80130080u, 0x801300C0u};
  const auto link = [&core](std::uint32_t from, std::uint32_t to) {
    core.mem_w32(from, 0x01000000u | (to == 0 ? 0u : psx::gpu::mainRamOffsetOf(to)));
  };
  link(kPackets[0], kPackets[1]); // bin 5: first, middle, head
  link(kPackets[1], kPackets[2]);
  link(kPackets[2], 0);
  link(kPackets[3], 0); // bin 200: one packet
  core.mem_w32(kTable + 5 * kBinPair, kPackets[2]);
  core.mem_w32(kTable + 5 * kBinPair + 4, kPackets[0]);
  core.mem_w32(kTable + 200 * kBinPair, kPackets[3]);
  core.mem_w32(kTable + 200 * kBinPair + 4, kPackets[3]);
  core.mem_w32(spyro2::kRenderGlobals.orderingTable, kTable);
  core.mem_w32(spyro2::kRenderGlobals.orderingTableMark, kTable + terrain::kOrderingTableBytes);

  scene.flatten();

  const OtSlot five{spyro2::depth_bins::kTable, 5};
  CHECK(core.otTables.assigned(kPackets[0]) == five);
  CHECK(core.otTables.assigned(kPackets[1]) == five);
  CHECK(core.otTables.assigned(kPackets[2]) == five);
  CHECK(core.otTables.assigned(kPackets[3]) == (OtSlot{spyro2::depth_bins::kTable, 200}));
  CHECK(core.otTables.slotOf(kTable + 200 * kBinPair) == (OtSlot{spyro2::depth_bins::kTable, 200}));
}

// At t = 1 the render emits the packets the real draw linked: the same decoded primitives into the
// same buckets, deepest bin first; and the real draw's packets all belong to one object whose saved
// state the producer reads.
void test_the_render_at_one_emits_what_the_real_draw_linked() {
  Scene scene;
  Core &core = scene.core();
  scene.drawTerrain();
  scene.flatten();

  const Linked real = linkedPackets(core);
  CHECK(real.packets.size() >= 2); // the quad and the triangle
  CHECK_EQ(real.undecoded, 0u);
  for (std::size_t i = 0; i < real.packets.size(); ++i) {
    CHECK(real.assigned[i] == real.packets[i].slot);
  }
  const std::uint32_t first = firstLinkedPacket(core);
  const std::optional<psx::present::RecordKey> key = core.emission.keyFor(first);
  CHECK(key.has_value());
  CHECK(key && key->producer == kTerrainEntry && key->object == kObject && key->element == 0);

  const auto state = savedState(core, real.packets.front().primitive, first);
  CHECK(state.has_value());
  const psx::present::StateProducer *producer = core.stateProducers.find(kTerrainEntry);
  CHECK(producer != nullptr);
  if (!state || producer == nullptr) {
    return;
  }
  const auto field = psx::present::stateAs<terrain::FieldState>(*state);
  CHECK_EQ(field.sectorCount, 1u);
  CHECK_EQ(static_cast<unsigned>(field.visible[0]), 1u);
  CHECK_EQ(field.camera.position[1], kRestCamera.position[1]);

  Collect sink;
  producer->render(*state, *state, 1.0f, sink);
  CHECK(sink.emitted == real.packets);
}

// Halfway between two cameras the render differs from t = 1, still draws, and holds only finite,
// on-grid values in bins the table has.
void test_the_render_between_two_cameras_differs_and_is_finite() {
  Scene scene;
  Core &core = scene.core();
  scene.drawTerrain();
  scene.flatten();
  const Linked real = linkedPackets(core);
  CHECK(!real.packets.empty());
  const std::uint32_t first = firstLinkedPacket(core);
  const auto after = savedState(core, real.packets.front().primitive, first);
  const psx::present::StateProducer *producer = core.stateProducers.find(kTerrainEntry);
  CHECK(after.has_value());
  CHECK(producer != nullptr);
  if (!after || producer == nullptr) {
    return;
  }

  auto before = psx::present::stateAs<terrain::FieldState>(*after);
  before.camera.angles[1] = 0x20;
  before.camera.position[0] += 0x40;
  const auto beforeBytes = std::as_bytes(std::span<const terrain::FieldState, 1>(&before, 1));
  Collect half;
  Collect one;
  producer->render(beforeBytes, *after, 0.5f, half);
  producer->render(beforeBytes, *after, 1.0f, one);

  CHECK(!half.emitted.empty());
  CHECK(half.emitted != one.emitted);
  CHECK(one.emitted == real.packets);
  for (const Emitted &emitted : half.emitted) {
    CHECK(emitted.slot.table == spyro2::depth_bins::kTable);
    CHECK(emitted.slot.index < spyro::terrain_packet_sink::kBins);
    for (int i = 0; i < emitted.primitive.vertexCount; ++i) {
      const psx::present::RecordVertex &vertex =
          emitted.primitive.vertices[static_cast<std::size_t>(i)];
      CHECK(std::isfinite(vertex.subX) && std::isfinite(vertex.subY));
      CHECK(std::abs(vertex.x) < 4096 && std::abs(vertex.y) < 4096);
    }
  }
}

} // namespace

int main() {
  RUN(spyro2_replays_its_record_and_interpolates_it);
  RUN(spyro3_keeps_its_render_path);
  RUN(a_cut_is_a_new_game_state_or_level);
  RUN(the_scene_is_sampled_when_the_table_is_walked);
  RUN(a_fresh_runtime_seals_a_cut_first);
  RUN(the_flatten_override_assigns_each_packet_its_bin);
  RUN(the_render_at_one_emits_what_the_real_draw_linked);
  RUN(the_render_between_two_cameras_differs_and_is_finite);
  return pt_summary();
}
