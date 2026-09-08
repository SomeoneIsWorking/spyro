#include "core.h"
#include "game.h"
#include "gpu_vk.h"
#include "native_projection.h"
#include "wide_clip_plan.h"
#include "world_scene_builder.h"
#include "world_scene_prepare.h"

#include <algorithm>
#include <cstdlib>
#include <lucent/log.h>
#include <memory>
#include <string_view>
#include <vector>

using spyro::world_chunk_codec::RamView;
using spyro::world_scene_prepare::Prepared;

namespace {

constexpr uint32_t kEnvironment = 0x785a8u;
constexpr uint32_t kCamera = 0x76dd0u;
constexpr uint32_t kTable = 0x90000u;
constexpr uint32_t kSector = 0x91000u;
constexpr uint32_t kGroups = 0x92000u;
constexpr uint32_t kGroup = 0x92100u;
constexpr int kNativeWidth = spyro::wide::kNativeClipWidth;
constexpr int kWideWidth = 684;
unsigned checks = 0;

void require(bool value, const char *message) {
  ++checks;
  if (!value) {
    lucent::error("selftest", "world preparation: FAIL {} (check {})", message, checks);
    std::exit(1);
  }
}

void w8(std::vector<uint8_t> &ram, uint32_t address, uint8_t value) {
  ram[address] = value;
}

void w32(std::vector<uint8_t> &ram, uint32_t address, uint32_t value) {
  for (uint32_t i = 0; i < 4u; ++i) {
    w8(ram, address + i, (uint8_t)(value >> (i * 8u)));
  }
}

void identity(std::vector<uint8_t> &ram, uint32_t address) {
  w32(ram, address, 0x00001000u);
  w32(ram, address + 4u, 0u);
  w32(ram, address + 8u, 0x00001000u);
  w32(ram, address + 12u, 0u);
  w32(ram, address + 16u, 0x00001000u);
}

std::vector<uint8_t> fixture() {
  std::vector<uint8_t> bytes(0x200000u);
  identity(bytes, kCamera);
  identity(bytes, kCamera + 0x14u);
  w32(bytes, kEnvironment, kTable);
  w32(bytes, kEnvironment + 4u, 1u);
  w32(bytes, kEnvironment + 8u, kGroups);
  w32(bytes, kEnvironment + 0x24u, 16000u);
  w32(bytes, kTable, kSector);
  w32(bytes, kSector + 0x18u, 0xffffffffu);
  return bytes;
}

// Invert the production sector-center/camera convention under identity matrices.
void view(std::vector<uint8_t> &bytes, int x, int y, int z, unsigned radius) {
  w32(bytes, kSector, 0u);
  w32(bytes, kSector + 4u, radius);
  w32(bytes, kCamera + 0x28u, (uint32_t)(-z * 16));
  w32(bytes, kCamera + 0x2cu, (uint32_t)(x * 16));
  w32(bytes, kCamera + 0x30u, (uint32_t)(y * 16));
}

Prepared prepare(const std::vector<uint8_t> &bytes, int width, int selection = -1) {
  const char *why = "none";
  Prepared result{};
  require(spyro::world_scene_prepare::prepare(RamView(bytes), selection, width, result, why),
          "shipping preparation accepts fixture");
  return result;
}

void retained_selection_contract() {
  auto bytes = fixture();
  w32(bytes, kEnvironment + 4u, 0u);
  const auto empty = prepare(bytes, kNativeWidth);
  require(empty.selectedSectors == 0 && empty.low.empty() && empty.high.empty(),
          "empty selection stays empty");
  w32(bytes, kEnvironment + 4u, 1u);
  view(bytes, 0, 0, 1000, 100);
  const auto flat = prepare(bytes, kNativeWidth);
  require(flat.selectedSectors == 1 && flat.low.size() == 1 && flat.high.size() == 1 &&
              flat.broadVisible[0] == 0xffu,
          "native sector and both LODs preserved");
  w32(bytes, kGroups, kGroup);
  w8(bytes, kGroup, 0u);
  w8(bytes, kGroup + 1u, 0u);
  w8(bytes, kGroup + 2u, 0xffu);
  const auto grouped = prepare(bytes, kNativeWidth, 0);
  require(grouped.selectedSectors == 2 && grouped.low.size() == 2 && grouped.high.size() == 2,
          "duplicate authored selection occurrences preserved");
  w32(bytes, kSector + 0x18u, 0xffffff00u);
  Prepared animated{};
  const char *why = "none";
  require(!spyro::world_scene_prepare::prepare(RamView(bytes), -1, kNativeWidth, animated, why) &&
              why == std::string_view("active_animation"),
          "unresolved native animation still refuses");
}

void horizontal_projection_contract() {
  auto bytes = fixture();
  psxport::native_projection::FixedAffine matrix{};
  matrix.m = {{{4096, 0, 0}, {0, 4096, 0}, {0, 0, 4096}}};
  for (int side : {-1, 1}) {
    view(bytes, side * 900, 0, 1000, 0);
    require(prepare(bytes, kNativeWidth).broadVisible[0] == 0,
            "side sector is outside native frustum");
    const auto wide = prepare(bytes, kWideWidth);
    require(wide.broadVisible[0] == 0xffu && wide.low.size() == 1 && wide.low[0].tags == 0,
            "same side sector is wholly inside widened frustum");
    const psxport::native_projection::ModelVertex vertex{(int16_t)(side * 900), 0, 1000};
    const auto native = psxport::native_projection::project(
        matrix, {.ofx = (kNativeWidth / 2) << 16, .ofy = 120 << 16, .h = 341}, vertex);
    const auto projected = psxport::native_projection::project(
        matrix, {.ofx = (kWideWidth / 2) << 16, .ofy = 120 << 16, .h = 341}, vertex);
    require(native.px < 0 || native.px >= kNativeWidth, "native projection excludes discriminator");
    require(projected.px >= 0 && projected.px < kWideWidth,
            "wide projection includes culled-sector discriminator");

    view(bytes, side * 767, 0, 1024, 0);
    require(prepare(bytes, kNativeWidth).broadVisible[0] == 0xffu,
            "just inside native horizontal edge");
    view(bytes, side * 768, 0, 1024, 0);
    require(prepare(bytes, kNativeWidth).broadVisible[0] == 0,
            "native strict horizontal edge preserved");
    view(bytes, side * 1025, 0, 1024, 0);
    require(prepare(bytes, kWideWidth).broadVisible[0] == 0xffu,
            "just inside wide horizontal edge");
    view(bytes, side * 1026, 0, 1024, 0);
    require(prepare(bytes, kWideWidth).broadVisible[0] == 0,
            "wide strict horizontal edge excludes");
    view(bytes, side * 900, 0, 1000, 100);
    const auto margin = prepare(bytes, kWideWidth);
    require(margin.broadVisible[0] == 0xffu && (margin.low[0].tags & 1u),
            "intersecting bounds retain clipping tag");
  }
}

void unchanged_vertical_and_near_contract() {
  auto bytes = fixture();
  for (int width : {kNativeWidth, kWideWidth, 896}) {
    for (int side : {-1, 1}) {
      view(bytes, 0, side * 543, 1024, 0);
      require(prepare(bytes, width).broadVisible[0] == 0xffu,
              "inside vertical edge at every width");
      view(bytes, 0, side * 544, 1024, 0);
      require(prepare(bytes, width).broadVisible[0] == 0, "strict vertical edge unchanged");
      view(bytes, side * 35, 0, -99, 100);
      require(prepare(bytes, width).broadVisible[0] == 0xffu,
              "authored behind-eye bound margin preserved");
      view(bytes, side * 35, 0, -100, 100);
      require(prepare(bytes, width).broadVisible[0] == 0, "strict near-plane edge unchanged");
    }
  }
  for (int width : {0, kNativeWidth - 1, 32768}) {
    Prepared out{};
    const char *why = "none";
    require(!spyro::world_scene_prepare::prepare(RamView(bytes), -1, width, out, why) &&
                why == std::string_view("clip_width") && out.selectedSectors == 0,
            "invalid viewport width refuses before selection");
  }
}

void widened_animation_and_build_contract() {
  auto bytes = fixture();
  view(bytes, 900, 0, 1000, 0);
  // One direct low-detail vertex channel. The other three channels are inactive.
  constexpr uint32_t set = 0x93000u, animation = 0x94000u, vertex = 0x00100401u;
  w32(bytes, kSector + 16u, 1u); // One vertex, zero colors/faces: a complete ValidEmpty recipe.
  w32(bytes, kSector + 24u, 0xffffff00u);
  w32(bytes, 0x78560u + 20u, set);
  w32(bytes, set, animation);
  w8(bytes, animation + 6u, 4u);
  w32(bytes, animation + 8u, 0x100u);
  w32(bytes, animation + 0x100u, vertex);
  const auto untouched = bytes;
  spyro::world_animation::Plan plan{};
  Prepared planned{};
  const char *why = "none";
  require(
      spyro::world_scene_prepare::prepare(RamView(bytes), -1, kWideWidth, planned, why, &plan) &&
          plan.channels == 1 && plan.writes.size() == 2 && planned.broadVisible[0] == 0xffu,
      "wide preparation plans newly visible animation");
  require(bytes == untouched, "preparation remains read-only with animation plan");

  auto game = std::make_unique<Game>();
  Core &core = game->core;
  std::copy(bytes.begin(), bytes.end(), core.ram);
  core.rsub.mode.setPath(RenderPath::Native);
  core.rsub.projParams.setGeomOffset(256, 120);
  core.rsub.projParams.setGeomScreen(341);
  game->gpu.s_disp_w = kNativeWidth;
  game->mods.aspect = ASPECT_4_3;
  require(spyro::world_scene::animate(&core, -1).channels == 0 && core.mem_r8(kSector + 24u) == 0,
          "native animation leaves out-of-frustum source unchanged");
  require(spyro::world_scene::build(&core, -1).broadVisible[0] == 0,
          "native builder keeps original culling");

  game->mods.aspect = ASPECT_16_9;
  require(gpu_vk_wide_engine_w(&core) == kWideWidth,
          "builder uses the production 512-wide 16:9 viewport");
  require(spyro::world_scene::build(&core, -1).status ==
              spyro::world_recipe::Status::ActiveAnimation,
          "wide render refuses unresolved newly visible geometry");
  const auto result = spyro::world_scene::animate(&core, -1);
  require(result.ok && result.channels == 1 && result.direct == 1 && result.writes == 2 &&
              core.mem_r32(kSector + 28u) == vertex && core.mem_r8(kSector + 24u) == 0xffu,
          "production animation applies and verifies same wide selection");
  const auto rendered = spyro::world_scene::build(&core, -1);
  require(rendered.status == spyro::world_recipe::Status::ValidEmpty &&
              rendered.broadVisible[0] == 0xffu && rendered.lowSectors == 1,
          "production build accepts the same resolved wide sector");
  require(spyro::world_scene::animate(&core, -1).channels == 0,
          "resolved animation is not replayed");
}

void immutable_source_contract() {
  auto bytes = fixture();
  view(bytes, 0, 0, 1000, 0);
  w32(bytes, kSector + 4u, 0x4000u); // LQ only; the unused HQ layout is deliberately invalid.
  w32(bytes, kEnvironment + 0x28u, 65536u);
  w32(bytes, kSector + 0x10u, 0x00010404u);
  constexpr uint32_t vertices = kSector + 0x1cu;
  for (uint32_t i = 0; i < 4; ++i) {
    w32(bytes, vertices + i * 4u, ((i & 1u) ? 64u << 10 : 0u) | ((i & 2u) ? 64u : 0u));
    w32(bytes, vertices + 16u + i * 4u, 0x00123456u + i);
  }
  constexpr uint32_t face = vertices + 32u;
  const uint32_t indices = (1u << 20) | (2u << 14) | (3u << 8);
  w32(bytes, face, indices | 0x80u);
  w32(bytes, face + 4u, indices | 7u);
  w32(bytes, kGroups, kGroup);
  w8(bytes, kGroup, 0u);
  w8(bytes, kGroup + 1u, 0u);
  w8(bytes, kGroup + 2u, 0xffu);
  constexpr uint32_t material = 0x95000u;
  w32(bytes, kEnvironment + 0x20u, 1u);
  w32(bytes, kEnvironment + 0x18u, material);
  w32(bytes, kEnvironment + 0x1cu, material + 16u);
  w32(bytes, material, 0xaabbccddu);

  auto game = std::make_unique<Game>();
  auto &core = game->core;
  std::copy(bytes.begin(), bytes.end(), core.ram);
  core.rsub.mode.setPath(RenderPath::Native);
  core.rsub.projParams.setGeomOffset(256, 120);
  core.rsub.projParams.setGeomScreen(341);
  game->gpu.s_disp_w = kNativeWidth;
  game->mods.aspect = ASPECT_4_3;
  const auto captured = spyro::world_scene::capture(&core, 0);
  const auto endpoint = spyro::world_scene::build(&core, 0);
  require(endpoint.status == spyro::world_recipe::Status::Ready && endpoint.faces.size() == 2 &&
              endpoint.candidates == 2 && endpoint.highSectors == 0,
          "authored duplicate LQ faces render while unused invalid HQ is retained");
  require(endpoint.faces[0].vertices[0].rgb == 0x00123456u &&
              endpoint.faces[0].material.semiTransparent &&
              endpoint.faces[0].material.tpage == 96u &&
              endpoint.faces[0].vertices[0].sz == 1000u && endpoint.faces[0].paintGroup == 0u &&
              endpoint.faces[1].paintGroup == 1u,
          "source endpoint retains colors, material, exact depth and duplicate OT link order");
  std::fill(std::begin(core.ram), std::end(core.ram), 0u);
  core.rsub.projParams.setGeomOffset(0, 0);
  const auto rebuilt = spyro::world_scene::build(captured);
  require(
      rebuilt.status == endpoint.status && rebuilt.broadVisible == endpoint.broadVisible &&
          rebuilt.candidates == endpoint.candidates &&
          spyro::world_recipe::compare(endpoint.faces, rebuilt.faces).equal &&
          rebuilt.faces[0].vertices[0].viewZ == endpoint.faces[0].vertices[0].viewZ &&
          rebuilt.faces[0].vertices[0].screenX == endpoint.faces[0].vertices[0].screenX &&
          captured.materials.r32(material) == 0xaabbccddu,
      "captured endpoint survives destruction of live geometry, camera, projection and materials");
  require(spyro::world_scene::build(&core, 0).status != spyro::world_recipe::Status::Ready,
          "live endpoint changes after source destruction discriminator");

  view(bytes, 900, 0, 1000, 0);
  w32(bytes, kSector + 4u, 0x4000u);
  auto hidden = spyro::world_source::capture(RamView(bytes), 0, captured.projection, kNativeWidth);
  require(hidden.selection.occurrences.size() == 2u && hidden.sectors[0] &&
              hidden.sectors[0]->low.faces.size() == 1u &&
              spyro::world_scene::build(hidden).faces.empty(),
          "capture retains authored duplicate candidates outside endpoint frustum");
  std::fill(bytes.begin(), bytes.end(), 0u);
  hidden.selection.camera.position[1] = 0;
  const auto revealed = spyro::world_scene::build(hidden);
  require(revealed.faces.size() == 2u &&
              spyro::world_recipe::compare(endpoint.faces, revealed.faces).equal,
          "new camera reclassifies and projects captured candidates without live RAM");
  hidden.selection.sectors[0]->extent = 100;
  require(spyro::world_scene::build(hidden).status == spyro::world_recipe::Status::InvalidChunk,
          "retained malformed HQ refuses when camera/LOD selection activates it");
  hidden.selection.sectors[0]->animation = 0xffffff00u;
  require(spyro::world_scene::build(hidden).status == spyro::world_recipe::Status::ActiveAnimation,
          "capture cannot bless unresolved authored animation");
  require(!captured.materials.contains(material + 0xb8u, 4u),
          "material source refuses bytes beyond captured records");
  auto partialBytes = fixture();
  w32(partialBytes, kEnvironment + 0x20u, 1u);
  w32(partialBytes, kEnvironment + 0x18u, 0x801ffff8u);
  w32(partialBytes, 0x1ffff8u, 0xa1b2c3d4u);
  const auto partial = spyro::world_source::Materials::capture(RamView(partialBytes));
  require(partial.contains(0x801ffff8u, 8u) && !partial.contains(0x801ffff8u, 16u) &&
              partial.r32(0x801ffff8u) == 0xa1b2c3d4u,
          "capture preserves valid partial material reads at RAM boundary");
  w32(partialBytes, kEnvironment + 0x1cu, material);
  w8(partialBytes, 0x6d378u, 0xf0u); // Medium base +8, signed -16 -> previous eight bytes.
  w32(partialBytes, material - 8u, 0x11223344u);
  const auto signedPair = spyro::world_source::Materials::capture(RamView(partialBytes));
  require(signedPair.contains(material - 8u, 8u) && signedPair.r32(material - 8u) == 0x11223344u,
          "authored signed pair selector retains referenced bytes outside nominal HQ record");
  require(!spyro::world_source::capture(RamView(bytes), INT32_MAX, captured.projection, 512)
               .selection.valid,
          "selection slot overflow refuses instead of wrapping into RAM");
}

void immutable_refined_source_contract() {
  // The same authored attribute-0x68 material and quad used by test_world_hq_refinement,
  // now encoded as a complete chunk so capture, classification and refinement all participate.
  for (uint32_t depth : {1024u, 256u}) {
    auto bytes = fixture();
    view(bytes, 0, 0, (int)(depth / 4u), 0);
    w32(bytes, kSector + 4u, 0x2000u); // HQ only.
    w32(bytes, kCamera + 0x2cu, depth / 2u);
    w32(bytes, kCamera + 0x30u, depth / 2u);
    w32(bytes, kSector + 0x14u, 0x00010404u); // Four vertices, two 16-byte color planes, one face.
    const uint32_t side = depth / 16u;
    constexpr uint32_t vertices = kSector + 0x1cu;
    w32(bytes, vertices, (side << 10) | side);
    w32(bytes, vertices + 4u, side);
    w32(bytes, vertices + 8u, 0u);
    w32(bytes, vertices + 12u, side << 10);
    for (uint32_t i = 0; i < 8; ++i) {
      w32(bytes, vertices + 16u + i * 4u, 0x00406080u);
    }
    constexpr uint32_t face = vertices + 48u;
    w32(bytes, face, 0x00010203u);
    w32(bytes, face + 4u, 0x00010203u);
    w32(bytes, face + 8u, 0u);
    w32(bytes, face + 12u, 4u);
    constexpr uint32_t material = 0x95000u;
    w32(bytes, kEnvironment + 0x1cu, material);
    w32(bytes, kEnvironment + 0x20u, 1u);
    w32(bytes, 0x6d0c0u, 0xffe1001fu);
    w32(bytes, 0x6d0c4u, 0x1f001f1fu);
    const uint32_t count = depth == 1024u ? 4u : 16u;
    const uint32_t firstPair = depth == 1024u ? 8u : 0x28u;
    for (uint32_t child = 0; child < count; ++child) {
      w32(bytes, material + firstPair + child * 8u, 0x2420e0e0u);
      w32(bytes, material + firstPair + child * 8u + 4u, 0xd088e0ffu);
    }
    auto game = std::make_unique<Game>();
    auto &core = game->core;
    std::copy(bytes.begin(), bytes.end(), core.ram);
    core.rsub.mode.setPath(RenderPath::Native);
    core.rsub.projParams.setGeomOffset(256, 120);
    core.rsub.projParams.setGeomScreen(341);
    game->gpu.s_disp_w = kNativeWidth;
    game->mods.aspect = ASPECT_4_3;
    const auto captured = spyro::world_scene::capture(&core, -1);
    const auto endpoint = spyro::world_scene::build(&core, -1);
    require(endpoint.status == spyro::world_recipe::Status::Ready && endpoint.faces.size() == count,
            "complete captured HQ chunk reaches expected medium/near subdivision");
    for (uint32_t child = 0; child < count; ++child) {
      const auto &output = endpoint.faces[child];
      require(output.material.clut == 0x2420u && output.material.tpage == 0xd088u &&
                  output.textureSource == material + firstPair + child * 8u &&
                  output.vertices[0].u == 0xffu && output.vertices[1].u == 0xe0u,
              "complete HQ endpoint preserves authored refinement material and UV adjustment");
    }
    std::fill(std::begin(core.ram), std::end(core.ram), 0u);
    core.rsub.projParams.setGeomScreen(1);
    const auto rebuilt = spyro::world_scene::build(captured);
    require(rebuilt.status == endpoint.status && rebuilt.broadVisible == endpoint.broadVisible &&
                spyro::world_recipe::compare(endpoint.faces, rebuilt.faces).equal,
            "HQ source rebuild retains geometry, camera, material and refinement tables after RAM "
            "destruction");
    for (size_t child = 0; child < endpoint.faces.size(); ++child) {
      for (uint32_t vertex = 0; vertex < 4; ++vertex) {
        const auto &before = endpoint.faces[child].vertices[vertex];
        const auto &after = rebuilt.faces[child].vertices[vertex];
        require(before.screenX == after.screenX && before.screenY == after.screenY &&
                    before.viewZ == after.viewZ && before.sz == after.sz,
                "HQ immutable rebuild retains precise projection and authored depth");
      }
    }
  }
}

void resource_span_contract() {
  auto bytes = fixture();
  view(bytes, 0, 0, 1000, 100);
  w32(bytes, kEnvironment, 0x80000000u | kTable);
  w32(bytes, kEnvironment + 4u, 2u);
  w32(bytes, kTable, 0x80000000u | kSector);
  w32(bytes, kGroups, 0x80000000u | kGroup);
  w8(bytes, kGroup, 0u);
  w8(bytes, kGroup + 1u, 0u);
  w8(bytes, kGroup + 2u, 0xffu);
  w32(bytes, kSector + 0x10u, 1u);
  w32(bytes, kSector + 0x14u, 1u | (16u << 22));
  constexpr uint32_t lowPayload = kSector + 0x1cu;
  constexpr uint32_t highPayload = kSector + 0x2cu;
  w32(bytes, lowPayload, 0x12345678u);
  w32(bytes, highPayload, 0xabcdef01u);
  const auto source = spyro::world_source::capture(
      RamView(bytes), 0, {.ofx = 256 << 16, .ofy = 120 << 16, .h = 341}, 512);
  const auto ranges = source.resourceRanges();
  const std::vector<GuestAddressRange> expected{{0x6cf98u, 0x6d5c8u},
                                                {kTable, kTable + 8u},
                                                {kSector, kSector + 0x1cu},
                                                {lowPayload, lowPayload + 4u},
                                                {highPayload, highPayload + 4u},
                                                {kGroups, kGroups + 4u},
                                                {kGroup, kGroup + 3u}};
  const auto sameRanges = [](const auto &left, const auto &right) {
    return left.size() == right.size() &&
           std::equal(left.begin(), left.end(), right.begin(), [](const auto &a, const auto &b) {
             return a.begin == b.begin && a.end == b.end;
           });
  };
  require(
      source.selection.valid && source.selection.occurrences.size() == 2u &&
          sameRanges(ranges, expected),
      "physical source spans include exact selection, terminated group, header and both payloads");
  require(source.sectors[0]->low.payloadRange->begin == lowPayload &&
              source.sectors[0]->low.payloadRange->end == lowPayload + 4u &&
              source.sectors[0]->high.payloadRange->begin == highPayload &&
              source.sectors[0]->high.payloadRange->end == highPayload + 4u,
          "codec reports validated payload spans without including unused HQ prefix gap");
  uint32_t total = 0;
  for (size_t i = 0; i < ranges.size(); ++i) {
    const auto &range = ranges[i];
    require(range.valid() && range.end <= 0x200000u && !range.containsPhysical(kCamera) &&
                !range.containsPhysical(kEnvironment) && !range.containsPhysical(lowPayload + 4u) &&
                (i == 0u || range.begin != ranges[i - 1].begin || range.end != ranges[i - 1].end),
            "resource spans exclude mutable globals and layout gaps, with no invalid or duplicate "
            "spans");
    total += range.end - range.begin;
  }
  require(total == (0x6d5c8u - 0x6cf98u) + 8u + 28u + 4u + 4u + 4u + 3u,
          "captured resource denominator covers bounded authored bytes rather than all RAM");
  const auto endpoint = spyro::world_scene::build(source);
  std::fill(bytes.begin(), bytes.end(), 0u);
  require(sameRanges(source.resourceRanges(), ranges) &&
              spyro::world_scene::build(source).status == endpoint.status &&
              source.sectors[0]->high.vertices[0] == 0xabcdef01u,
          "resource spans and captured payload survive live source destruction");
  require(!RamView(bytes).range(0x801fffffu, 2u) && !RamView(bytes).range(kSector, 0u) &&
              !RamView(bytes).range(0xa0091000u, 4u),
          "range owner refuses crossing RAM boundary, empty and unmapped spans");

  bytes = fixture();
  view(bytes, 0, 0, 1000, 0);
  w32(bytes, kSector + 4u, 0x4000u);
  w32(bytes, kSector + 0x10u, 1u);
  const auto dormant = spyro::world_source::capture(RamView(bytes), -1, source.projection, 512);
  require(dormant.sectors[0]->highStatus != spyro::world_chunk_codec::Status::Ok &&
              !dormant.sectors[0]->high.payloadRange &&
              spyro::world_scene::build(dormant).status == spyro::world_recipe::Status::ValidEmpty,
          "invalid inactive LOD adds no payload span and preserves endpoint admission");
  require(dormant.resourceRanges().size() == 4u,
          "flat selection excludes unused group records and failed LOD payloads");

  constexpr uint32_t material = 0x95000u;
  w32(bytes, kEnvironment + 0x20u, 1u);
  w32(bytes, kEnvironment + 0x18u, 0x801ffff8u);
  w32(bytes, kEnvironment + 0x1cu, material);
  w8(bytes, 0x6d378u, 0xf0u);
  const auto materials = spyro::world_source::Materials::capture(RamView(bytes));
  const std::vector<GuestAddressRange> materialExpected{
      {0x6cf98u, 0x6d5c8u}, {material - 8u, material + 0xa8u}, {0x1ffff8u, 0x200000u}};
  require(sameRanges(materials.resourceRanges(), materialExpected),
          "material spans retain signed-selector extras and exact partial RAM-end prefix");
  require(spyro::world_source::capture(RamView(bytes), INT32_MAX, source.projection, 512)
              .resourceRanges()
              .empty(),
          "invalid selection cannot publish partial resource provenance");
}

} // namespace

int main() {
  resource_span_contract();
  immutable_source_contract();
  immutable_refined_source_contract();
  retained_selection_contract();
  horizontal_projection_contract();
  unchanged_vertical_and_near_contract();
  widened_animation_and_build_contract();
  lucent::info("selftest",
               "world preparation: PASS {} checks; native, wide, vertical/near edges and animation",
               checks);
  return 0;
}
