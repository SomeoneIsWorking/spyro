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

} // namespace

int main() {
  retained_selection_contract();
  horizontal_projection_contract();
  unchanged_vertical_and_near_contract();
  widened_animation_and_build_contract();
  lucent::info("selftest",
               "world preparation: PASS {} checks; native, wide, vertical/near edges and animation",
               checks);
  return 0;
}
