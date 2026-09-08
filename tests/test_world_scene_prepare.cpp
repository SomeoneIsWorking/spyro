#include "core.h"
#include "game.h"
#include "gpu_vk.h"
#include "native_projection.h"
#include "wide_clip_plan.h"
#include "world_scene_builder.h"
#include "world_scene_prepare.h"
#include "world_source_fixture.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <lucent/log.h>
#include <memory>
#include <string_view>
#include <vector>

using spyro::world_chunk_codec::RamView;
using spyro::world_scene_prepare::Prepared;

namespace {

using namespace spyro::testing::world_source_fixture;
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
  auto bytes = lowGeometryFixture();
  constexpr uint32_t material = 0x95000u;
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

std::vector<uint8_t> refinedGeometryFixture(uint32_t depth) {
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
  return bytes;
}

void immutable_refined_source_contract() {
  // The same authored attribute-0x68 material and quad used by test_world_hq_refinement,
  // now encoded as a complete chunk so capture, classification and refinement all participate.
  for (uint32_t depth : {1024u, 256u}) {
    auto bytes = refinedGeometryFixture(depth);
    constexpr uint32_t material = 0x95000u;
    const uint32_t count = depth == 1024u ? 4u : 16u;
    const uint32_t firstPair = depth == 1024u ? 8u : 0x28u;
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

bool sameRecipe(const spyro::world_recipe::Recipe &a, const spyro::world_recipe::Recipe &b) {
  if (a.status != b.status || a.broadVisible != b.broadVisible || a.candidates != b.candidates ||
      a.rejected != b.rejected || !spyro::world_recipe::compare(a.faces, b.faces).equal) {
    return false;
  }
  for (size_t face = 0; face < a.faces.size(); ++face) {
    for (uint32_t vertex = 0; vertex < a.faces[face].vertexCount; ++vertex) {
      const auto &x = a.faces[face].vertices[vertex];
      const auto &y = b.faces[face].vertices[vertex];
      if (x.screenX != y.screenX || x.screenY != y.screenY || x.viewZ != y.viewZ || x.sz != y.sz) {
        return false;
      }
    }
  }
  return true;
}

void sampled_source_contract() {
  using spyro::world_recipe::Status;
  using spyro::world_scene::build;
  using spyro::world_scene::sample;
  const auto bytes = lowGeometryFixture();
  const auto base = spyro::world_source::capture(
      RamView(bytes), 0, {.ofx = 256 << 16, .ofy = 120 << 16, .h = 341}, 512);
  auto previous = base, current = base, expected = base;
  previous.selection.camera.position[1] = 128 * 16;
  current.selection.camera.position[1] = 192 * 16;
  expected.selection.camera.position[1] = 160 * 16;
  const auto mid = sample(previous, current, 0.5);
  require(
      mid.status == Status::Ready && mid.faces.size() == 2u && sameRecipe(mid, build(expected)),
      "LQ camera sampling matches independently constructed midpoint with duplicate occurrences");
  require(sameRecipe(sample(previous, current, 0.0), build(previous)) &&
              sameRecipe(sample(previous, current, 1.0), build(current)),
          "sampling exact endpoints preserves complete endpoint recipes");
  auto moving = current, middleGeometry = expected;
  for (auto &vertex : moving.sectors[0]->low.vertices) {
    vertex += 2u << 21;
  }
  for (auto &vertex : middleGeometry.sectors[0]->low.vertices) {
    vertex += 1u << 21;
  }
  require(sameRecipe(sample(previous, moving, 0.5), build(middleGeometry)),
          "authored geometry motion and camera translation sample before perspective division");
  current.selection.camera.position[1] = 129 * 16;
  const auto fractional = sample(previous, current, 0.5);
  require(fractional.status == Status::Ready &&
              std::abs(fractional.faces[0].vertices[0].screenX -
                       (256.0f + 128.5f * 341.0f / 1000.0f)) < 0.0001f,
          "interior LQ projection retains fractional raw view before projection");

  current.selection.camera.position[1] = 192 * 16;
  current.selection.camera.projectionMatrix.m[0][0] = 2048;
  const auto transformed = sample(previous, current, 0.5);
  require(transformed.status == Status::Ready &&
              std::abs(transformed.faces[0].vertices[0].screenX -
                       (256.0f + 112.0f * 341.0f / 1000.0f)) < 0.0001f,
          "moving camera and transform sample endpoint views without matrix-lerp cross terms");
  current = base;
  previous = base;
  previous.selection.camera.position[1] = -900 * 16;
  current.selection.camera.position[1] = 900 * 16;
  require(build(previous).faces.empty() && build(current).faces.empty() &&
              sample(previous, current, 0.5).faces.size() == 2u,
          "sampled culling reveals authored source absent from both endpoint recipes");
  previous.selection.sectors[0]->animation = 0xffffff00u;
  require(build(previous).status == Status::ValidEmpty &&
              sample(previous, current, 0.5).status == Status::ActiveAnimation,
          "newly visible midpoint refuses an unadvanced channel at either endpoint");
  for (uint32_t channel = 0; channel < 4u; ++channel) {
    for (bool dirtyPrevious : {false, true}) {
      auto before = previous.selection, after = current.selection;
      before.sectors[0]->animation = after.sectors[0]->animation = 0xffffffffu;
      before.sectors[0]->extent = after.sectors[0]->extent = channel < 2u ? 0x4001u : 0x2001u;
      auto &dirty = dirtyPrevious ? before : after;
      dirty.sectors[0]->animation &= ~(0xffu << (channel * 8u));
      const spyro::world_projection_math::ProjectionStream culling(
          before.camera.cullingMatrix, after.camera.cullingMatrix, {}, 0.5);
      Prepared prepared{};
      const char *why = "unset";
      require(!spyro::world_scene_prepare::prepare(before, after, culling, 512, prepared, why) &&
                  why == std::string_view("active_animation"),
              "each selected LQ/HQ vertex/color channel refuses when pending at either endpoint");
      require(dirty.sectors[0]->animation == (0xffffffffu & ~(0xffu << (channel * 8u))),
              "pending-channel diagnosis leaves captured readiness unchanged");
    }
  }
  previous = base;
  current = base;
  current.sectors[0]->low.colors[0] ^= 1u;
  require(sample(previous, current, 0.5).status == Status::InvalidSelection &&
              sameRecipe(sample(previous, current, 0.0), build(previous)),
          "incompatible source refuses interior while exact endpoint bypasses interval admission");
  for (double t : {-0.1,
                   1.1,
                   std::numeric_limits<double>::infinity(),
                   std::numeric_limits<double>::quiet_NaN()}) {
    require(sample(base, base, t).status == Status::InvalidSelection,
            "invalid temporal parameter refuses before source execution");
  }
  previous = base;
  current = base;
  previous.selection.camera.position[1] = current.selection.camera.position[1] = 128 * 16;
  current.selection.camera.projectionMatrix.t[0] = INT32_MAX;
  const auto refused = sample(previous, current, 0.5);
  require(refused.status == Status::InvalidChunk && refused.faces.empty() &&
              refused.refusal == std::string_view("low_projection_sample"),
          "LQ MAC overflow refuses rather than being misreported as a culled vertex");
  require(sameRecipe(sample(previous, current, 1.0), build(current)),
          "exact endpoint preserves its MAC overflow behavior before sampleability checks");
  current = previous;
  current.selection.camera.cullingMatrix.t[0] = INT32_MAX;
  require(sample(previous, current, 0.5).refusal == std::string_view("culling_projection_sample"),
          "culling MAC overflow propagates distinct sample refusal");

  for (uint32_t depth : {1024u, 256u}) {
    const auto hqBytes = refinedGeometryFixture(depth);
    const auto hq = spyro::world_source::capture(RamView(hqBytes), -1, base.projection, 512);
    auto left = hq, right = hq;
    // Translate endpoints in equal multiples of every authored lattice divisor. Their midpoint
    // is exactly the original input, including packed low-half borrowing at every lattice node.
    left.selection.camera.position[2] -= 64;
    right.selection.camera.position[2] += 64;
    const auto refined = sample(left, right, 0.5);
    const auto reference = build(hq);
    require(refined.status == Status::Ready && sameRecipe(refined, reference) &&
                refined.faces.size() == (depth == 1024u ? 4u : 16u),
            "HQ medium/near camera sample matches complete independently constructed source");
    require(sameRecipe(sample(left, right, 0.0), build(left)) &&
                sameRecipe(sample(left, right, 1.0), build(right)),
            "HQ refinement exact endpoints preserve material, corrections, depth and paint order");
  }
}

void sampled_material_timeline_contract() {
  using spyro::world_recipe::Status;
  using spyro::world_scene::build;
  using spyro::world_scene::sample;
  constexpr uint32_t high = 0x95000u;
  constexpr uint32_t low = 0x96000u;
  for (uint32_t mode = 0; mode < 3u; ++mode) {
    auto bytes = refinedGeometryFixture(mode == 1u ? 256u : 1024u);
    w32(bytes, kEnvironment + 0x18u, low);
    for (uint32_t tile = 0; tile < 2u; ++tile) {
      w32(bytes, low + tile * 8u, 0x24202010u);
      w32(bytes, low + tile * 8u + 4u, 0x0088202fu);
    }
    if (mode == 2u) {
      w32(bytes, kSector + 0x1cu + 60u, 0x84u); // Authored direct-face flag.
    }
    const auto capture = [&] {
      return spyro::world_source::capture(
          RamView(bytes), -1, {.ofx = 256 << 16, .ofy = 120 << 16, .h = 341}, 512);
    };
    auto previous = capture();
    const auto oldRecipe = build(previous);
    require(oldRecipe.status == Status::Ready && oldRecipe.faces.size() == (mode == 0u   ? 4u
                                                                            : mode == 1u ? 16u
                                                                                         : 1u),
            "material timeline fixture reaches medium, near and direct shipping owners");
    for (uint32_t tile = 0; tile < 21u; ++tile) {
      bytes[high + tile * 8u + 1u] ^= 0x10u;
      bytes[high + tile * 8u + 5u] ^= 0x10u;
    }
    for (uint32_t tile = 0; tile < 2u; ++tile) {
      bytes[low + tile * 8u + 1u] ^= 0x10u;
      bytes[low + tile * 8u + 5u] ^= 0x10u;
    }
    auto current = capture();
    const auto expected = build(current);
    require(!sameRecipe(oldRecipe, expected), "authored UV change visibly changes the recipe");
    previous.selection.camera.position[2] -= 64;
    current.selection.camera.position[2] += 64;
    require(
        sameRecipe(sample(previous, current, 0.5), expected),
        "interior samples moving geometry with current discrete UV, never previous or blended UV");
    require(sameRecipe(sample(previous, current, 0.0), build(previous)) &&
                sameRecipe(sample(previous, current, 1.0), build(current)),
            "UV animation retains exact t0 and t1 authored endpoint recipes");
    for (uint32_t address : {low + 2u, low + 6u, high + 2u, high + 7u, 0x6cf98u}) {
      bytes[address] ^= 1u;
      const auto changed = capture();
      const auto rejected = sample(previous, changed, 0.5);
      require(rejected.status == Status::InvalidSelection && rejected.faces.empty() &&
                  rejected.refusal == std::string_view("captured_materials"),
              "changed CLUT, TPAGE, refinement attributes or tables refuse the shipping sampler");
      bytes[address] ^= 1u;
    }
    w32(bytes, kEnvironment + 0x20u, 2u);
    require(sample(previous, capture(), 0.5).refusal == std::string_view("captured_materials"),
            "material layout change remains an interval refusal");
    std::fill(bytes.begin(), bytes.end(), 0u);
    require(sameRecipe(sample(previous, current, 0.5), expected),
            "sampled UV state remains immutable after original RAM destruction");
  }
}

} // namespace

int main() {
  sampled_source_contract();
  sampled_material_timeline_contract();
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
