#include "testutil.h"
#include "world_source_pair.h"

#include <string_view>
#include <vector>

namespace {
using spyro::world_chunk_codec::RamView;
using spyro::world_chunk_codec::Status;
using spyro::world_source::Source;
using spyro::world_source_pair::compatible;

constexpr uint32_t kEnvironment = 0x785a8u;
constexpr uint32_t kSector = 0x91000u;
constexpr uint32_t kLowMaterial = 0x93000u;
constexpr uint32_t kHighMaterial = 0x94000u;

void w32(std::vector<uint8_t> &ram, uint32_t address, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) {
    ram[address + i] = (uint8_t)(value >> (i * 8u));
  }
}

std::vector<uint8_t> fixtureBytes() {
  std::vector<uint8_t> ram(0x200000u);
  w32(ram, kEnvironment, 0x90000u);
  w32(ram, kEnvironment + 4u, 2u);
  w32(ram, kEnvironment + 8u, 0x92000u);
  w32(ram, 0x92000u, 0x92100u);
  ram[0x92100u] = 0;
  ram[0x92101u] = 1;
  ram[0x92102u] = 0;
  ram[0x92103u] = 0xff;
  w32(ram, kEnvironment + 0x18u, kLowMaterial);
  w32(ram, kEnvironment + 0x1cu, kHighMaterial);
  w32(ram, kEnvironment + 0x20u, 1u);
  w32(ram, kEnvironment + 0x24u, 16000u);
  for (uint32_t i = 0; i < 2; ++i) {
    const uint32_t sector = kSector + i * 0x100u;
    w32(ram, 0x90000u + i * 4u, sector);
    w32(ram, sector + 4u, 100u);
    w32(ram, sector + 0x10u, 0x00010303u); // Three LQ vertices/colors, one face.
    w32(ram, sector + 0x14u, 0x08010303u); // HQ starts after the 32-byte LQ payload.
    w32(ram, sector + 0x18u, 0xffffffffu);
    for (uint32_t color = 0; color < 3; ++color) {
      w32(ram, sector + 0x28u + color * 4u, 0x00102030u + color);
      w32(ram, sector + 0x48u + color * 4u, 0x00405060u + color);
      w32(ram, sector + 0x54u + color * 4u, 0x00708090u + color);
    }
    w32(ram, sector + 0x34u, 0x00108280u);
    w32(ram, sector + 0x38u, 0x00108200u);
    w32(ram, sector + 0x60u, 0x00040808u);
    w32(ram, sector + 0x64u, 0x00040808u);
  }
  return ram;
}

Source capture(const std::vector<uint8_t> &ram) {
  return spyro::world_source::capture(
      RamView(ram), 0, {.ofx = 342 << 16, .ofy = 120 << 16, .h = 341}, 684);
}

void checkMismatch(const Source &before, const Source &after, const char *expected) {
  const char *why = "unset";
  CHECK(!compatible(before, after, why));
  CHECK(std::string_view(why) == expected);
  CHECK(!compatible(after, before, why));
  CHECK(std::string_view(why) == expected);
}

void test_authored_movement_and_duplicates_are_compatible() {
  auto ram = fixtureBytes();
  const Source previous = capture(ram);
  CHECK(previous.selection.valid);
  CHECK(previous.selection.occurrences == std::vector<uint8_t>({0, 1, 0}));
  CHECK(previous.sectors[0]->lowStatus == Status::Ok);
  CHECK(previous.sectors[0]->highStatus == Status::Ok);
  Source current = previous;
  current.selection.camera.position = {13, -19, 27};
  current.selection.camera.projectionMatrix.m[0][0] = 4095;
  current.selection.camera.projectionMatrix.t[2] = 8;
  current.selection.camera.cullingMatrix.m[1][2] = -300;
  current.selection.sectors[0]->center = 0x12345678u;
  current.selection.sectors[0]->extent |= 0x87650000u;
  current.selection.sectors[0]->animation = 0xffffff00u;
  current.sectors[0]->low.originWord = 0x12345678u;
  current.sectors[0]->low.originZ = 234u;
  current.sectors[0]->low.vertices[1] = 0x12345678u;
  current.sectors[0]->high.originWord = 0x87654321u;
  current.sectors[0]->high.originAndOffset |= 0x34560000u;
  current.sectors[0]->high.vertices[2] = 0x87654321u;
  const auto lowVertices = current.sectors[0]->low.vertices;
  const auto highVertices = current.sectors[0]->high.vertices;
  const auto camera = current.selection.camera;
  const char *why = "unset";
  CHECK(compatible(previous, current, why));
  CHECK(std::string_view(why) == "none");
  CHECK(compatible(current, previous, why));
  // Captured bytes and both source streams stay owned and unchanged by the gate.
  ram[kLowMaterial] = 99;
  CHECK(compatible(previous, current, why));
  CHECK(current.materials.r8(kLowMaterial) == 0);
  CHECK(current.sectors[0]->low.vertices == lowVertices);
  CHECK(current.sectors[0]->high.vertices == highVertices);
  CHECK(current.selection.camera.position == camera.position);
  CHECK(current.selection.camera.projectionMatrix.m == camera.projectionMatrix.m);
  CHECK(current.selection.sectors[0]->animation == 0xffffff00u);
  CHECK(previous.sectors[0]->low.vertices[1] == 0);
}

void test_projection_selection_and_sector_discriminators() {
  const Source source = capture(fixtureBytes());
  struct Change {
    const char *why;
    void (*apply)(Source &);
  };
  const Change changes[] = {
      {"selection_invalid",
       [](Source &s) {
         s.selection.valid = false;
       }},
      {"projection_changed",
       [](Source &s) {
         ++s.projection.ofx;
       }},
      {"projection_changed",
       [](Source &s) {
         ++s.projection.ofy;
       }},
      {"projection_changed",
       [](Source &s) {
         ++s.projection.h;
       }},
      {"projection_changed",
       [](Source &s) {
         ++s.projection.dqa;
       }},
      {"projection_changed",
       [](Source &s) {
         ++s.projection.dqb;
       }},
      {"projection_changed",
       [](Source &s) {
         ++s.clipRight;
       }},
      {"selection_policy",
       [](Source &s) {
         ++s.selection.lodDistance;
       }},
      {"selection_policy",
       [](Source &s) {
         ++s.selection.cullingDistance;
       }},
      {"selection_policy",
       [](Source &s) {
         s.selection.skipLow = true;
       }},
      {"selection_occurrences",
       [](Source &s) {
         ++s.selection.group;
       }},
      {"selection_occurrences",
       [](Source &s) {
         s.selection.occurrences.pop_back();
       }},
      {"selection_occurrences",
       [](Source &s) {
         s.selection.occurrences = {1, 0, 0};
       }},
      {"sector_presence",
       [](Source &s) {
         s.selection.sectors[0].reset();
       }},
      {"sector_presence",
       [](Source &s) {
         s.sectors[0].reset();
       }},
      {"sector_control",
       [](Source &s) {
         ++s.selection.sectors[0]->address;
       }},
      {"sector_control",
       [](Source &s) {
         s.selection.sectors[0]->extent ^= 1u;
       }},
      {"sector_control",
       [](Source &s) {
         s.selection.sectors[0]->extent ^= 0x2000u;
       }},
      {"sector_control",
       [](Source &s) {
         s.selection.sectors[0]->extent ^= 0x4000u;
       }},
      {"sector_control",
       [](Source &s) {
         s.selection.sectors[0]->extent ^= 0x8000u;
       }},
      {"lod_availability",
       [](Source &s) {
         s.sectors[0]->lowStatus = Status::InvalidCount;
       }},
      {"lod_availability",
       [](Source &s) {
         s.sectors[0]->highStatus = Status::InvalidCount;
       }},
  };
  for (const auto &change : changes) {
    Source changed = source;
    change.apply(changed);
    checkMismatch(source, changed, change.why);
  }
}

void test_lod_layout_topology_and_color_discriminators() {
  const Source source = capture(fixtureBytes());
  struct Change {
    const char *why;
    void (*apply)(Source &);
  };
  const Change changes[] = {
      {"low_source_range",
       [](Source &s) {
         ++s.sectors[0]->low.address;
       }},
      {"low_source_range",
       [](Source &s) {
         ++s.sectors[0]->low.payloadRange->end;
       }},
      {"low_source_range",
       [](Source &s) {
         s.sectors[0]->low.payloadRange.reset();
       }},
      {"low_layout",
       [](Source &s) {
         ++s.sectors[0]->low.descriptor;
       }},
      {"low_layout",
       [](Source &s) {
         s.sectors[0]->low.vertices.pop_back();
       }},
      {"low_layout",
       [](Source &s) {
         s.sectors[0]->low.colors.pop_back();
       }},
      {"low_layout",
       [](Source &s) {
         s.sectors[0]->low.faces.pop_back();
       }},
      {"low_colors",
       [](Source &s) {
         ++s.sectors[0]->low.colors[0];
       }},
      {"low_face",
       [](Source &s) {
         ++s.sectors[0]->low.faces[0].address;
       }},
      {"low_face",
       [](Source &s) {
         s.sectors[0]->low.faces[0].vertexWord ^= 0x100u;
       }},
      {"low_face",
       [](Source &s) {
         s.sectors[0]->low.faces[0].vertexWord ^= 0x80u;
       }},
      {"low_face",
       [](Source &s) {
         ++s.sectors[0]->low.faces[0].materialWord;
       }},
      {"high_source_range",
       [](Source &s) {
         ++s.sectors[0]->high.address;
       }},
      {"high_source_range",
       [](Source &s) {
         ++s.sectors[0]->high.payloadRange->begin;
       }},
      {"high_source_range",
       [](Source &s) {
         s.sectors[0]->high.payloadRange.reset();
       }},
      {"high_layout",
       [](Source &s) {
         ++s.sectors[0]->high.layout;
       }},
      {"high_layout",
       [](Source &s) {
         s.sectors[0]->high.vertices.pop_back();
       }},
      {"high_layout",
       [](Source &s) {
         s.sectors[0]->high.farColors.pop_back();
       }},
      {"high_layout",
       [](Source &s) {
         s.sectors[0]->high.nearColors.pop_back();
       }},
      {"high_layout",
       [](Source &s) {
         s.sectors[0]->high.faces.pop_back();
       }},
      {"high_status_offset",
       [](Source &s) {
         s.sectors[0]->high.originAndOffset ^= 0x8000u;
       }},
      {"high_colors",
       [](Source &s) {
         ++s.sectors[0]->high.farColors[0];
       }},
      {"high_colors",
       [](Source &s) {
         ++s.sectors[0]->high.nearColors[0];
       }},
      {"high_face",
       [](Source &s) {
         ++s.sectors[0]->high.faces[0].address;
       }},
      {"high_face",
       [](Source &s) {
         ++s.sectors[0]->high.faces[0].vertexWord;
       }},
      {"high_face",
       [](Source &s) {
         ++s.sectors[0]->high.faces[0].colorWord;
       }},
      {"high_face",
       [](Source &s) {
         ++s.sectors[0]->high.faces[0].materialWord;
       }},
      {"high_face",
       [](Source &s) {
         ++s.sectors[0]->high.faces[0].flags;
       }},
  };
  for (const auto &change : changes) {
    Source changed = source;
    change.apply(changed);
    checkMismatch(source, changed, change.why);
  }
}

void test_material_capture_and_dormant_lod_contract() {
  auto ram = fixtureBytes();
  const Source source = capture(ram);
  for (uint32_t address : {kLowMaterial + 2u, kHighMaterial + 0xa7u, 0x6cf98u, 0x6d5c7u}) {
    ram[address] ^= 1u;
    checkMismatch(source, capture(ram), "captured_materials");
    ram[address] ^= 1u;
  }
  for (uint32_t offset : {0x18u, 0x1cu, 0x20u}) {
    ram[kEnvironment + offset] ^= 1u;
    checkMismatch(source, capture(ram), "captured_materials");
    ram[kEnvironment + offset] ^= 1u;
  }
  Source previous = source;
  previous.sectors[0]->lowStatus = Status::PayloadBounds;
  previous.sectors[0]->highStatus = Status::InvalidCount;
  Source current = previous;
  current.sectors[0]->low.vertices.clear();
  current.sectors[0]->high.layout = 0;
  const char *why = "unset";
  CHECK(compatible(previous, current, why));
  CHECK(std::string_view(why) == "none");
  current.sectors[0]->lowStatus = Status::HeaderBounds;
  checkMismatch(previous, current, "lod_availability");

  Source empty{};
  empty.selection.valid = true;
  CHECK(compatible(empty, empty, why));
  CHECK(std::string_view(why) == "none");
}

void test_tile_uv_state_preserves_identity() {
  auto bytes = fixtureBytes();
  // A signed selector also reaches a tile outside the nominal high record.
  bytes[0x6d378u] = 0xf0u;
  const Source previous = capture(bytes);
  for (uint32_t tile : {kLowMaterial,
                        kLowMaterial + 8u,
                        kHighMaterial,
                        kHighMaterial + 0xa0u,
                        kHighMaterial - 8u}) {
    for (uint32_t offset = 0; offset < 8u; ++offset) {
      bytes[tile + offset] ^= 1u;
      const Source current = capture(bytes);
      const char *why = "unset";
      const bool uv = offset == 0u || offset == 1u || offset == 4u || offset == 5u;
      CHECK_EQ(compatible(previous, current, why), uv);
      CHECK_EQ(compatible(current, previous, why), uv);
      CHECK(!(previous.materials == current.materials));
      bytes[tile + offset] ^= 1u;
    }
  }
  // An unaligned signed tile's CLUT bytes overlap the preceding record's UV bytes.
  bytes[0x6d378u] = 0xfeu;
  const Source overlap = capture(bytes);
  bytes[kHighMaterial + 8u] ^= 1u;
  checkMismatch(overlap, capture(bytes), "captured_materials");
  bytes = fixtureBytes();
  w32(bytes, kEnvironment + 0x18u, 0x1ffffdu);
  const Source partial = capture(bytes);
  bytes[0x1ffffeu] ^= 1u;
  const char *why = "unset";
  CHECK(compatible(partial, capture(bytes), why));
  bytes[0x1fffffu] ^= 1u;
  checkMismatch(partial, capture(bytes), "captured_materials");
  // A material pointer that aliases a refinement table cannot reclassify its bytes as UV.
  bytes = fixtureBytes();
  w32(bytes, kEnvironment + 0x18u, 0x6cf98u);
  const Source alias = capture(bytes);
  bytes[0x6cf98u] ^= 1u;
  checkMismatch(alias, capture(bytes), "captured_materials");
}

void test_material_difference_diagnostic_has_denominators() {
  auto bytes = fixtureBytes();
  const Source previous = capture(bytes);
  const auto equal = previous.materials.difference(previous.materials);
  CHECK(!equal.layoutMismatch);
  CHECK_EQ(equal.scannedBlocks, 3u);
  CHECK_EQ(equal.scannedBytes, 0x630u + 16u + 0xa8u);
  CHECK_EQ(equal.changedBytes, 0u);
  bytes[kLowMaterial + 3u] = 0x6au;
  bytes[kHighMaterial + 0xa7u] = 0x91u;
  const Source current = capture(bytes);
  const auto changed = previous.materials.difference(current.materials);
  CHECK(!changed.layoutMismatch);
  CHECK_EQ(changed.scannedBytes, equal.scannedBytes);
  CHECK_EQ(changed.changedBytes, 2u);
  CHECK_EQ(changed.blockAddress, kLowMaterial);
  CHECK_EQ(changed.blockSize, 16u);
  CHECK_EQ(changed.firstAddress, kLowMaterial + 3u);
  CHECK_EQ(changed.before, 0u);
  CHECK_EQ(changed.after, 0x6au);
  checkMismatch(previous, current, "captured_materials");
  w32(bytes, kEnvironment + 0x20u, 2u);
  CHECK(previous.materials.difference(capture(bytes).materials).layoutMismatch);
}

} // namespace

int main() {
  RUN(authored_movement_and_duplicates_are_compatible);
  RUN(projection_selection_and_sector_discriminators);
  RUN(lod_layout_topology_and_color_discriminators);
  RUN(material_capture_and_dormant_lod_contract);
  RUN(tile_uv_state_preserves_identity);
  RUN(material_difference_diagnostic_has_denominators);
  return pt_summary();
}
