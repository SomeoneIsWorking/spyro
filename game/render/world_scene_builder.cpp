#include "world_scene_builder.h"

#include "core.h"
#include "gpu_vk.h"
#include "world_chunk_codec.h"
#include "world_hq_recipe.h"
#include "world_lq_recipe.h"
#include "world_scene_prepare.h"

#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

namespace spyro::world_scene {
namespace {

using psxport::native_projection::ProjectionParams;
using spyro::world_recipe::Recipe;
using spyro::world_recipe::Status;

Recipe refuse(Recipe recipe, Status status, const char *why) {
  recipe.status = status;
  recipe.refusal = why;
  recipe.faces.clear();
  return recipe;
}

Status refusalStatus(std::string_view why) {
  if (why.find("capacity") != std::string_view::npos) {
    return Status::CapacityExceeded;
  }
  if (why.find("material") != std::string_view::npos ||
      why.find("texture") != std::string_view::npos) {
    return Status::InvalidMaterial;
  }
  return Status::InvalidChunk;
}

ProjectionParams projection(Core *core, int clipRight) {
  ProjectionParams out{};
  out.ofx = (int32_t)(core->rsub.projParams.geomOfx() * 65536.0f);
  out.ofy = (int32_t)(core->rsub.projParams.geomOfy() * 65536.0f);
  out.h = (uint16_t)core->rsub.projParams.geomH();
  if (gpu_vk_wide_engine(core)) {
    out.ofx = (clipRight / 2) << 16;
  }
  return out;
}

} // namespace

Recipe build(Core *core, int32_t selection, world_hq_recipe::Audit *audit) {
  Recipe out{};
  if (!core || !core->rsub.projParams.geomValid()) {
    return refuse(std::move(out), Status::InvalidSelection, "projection_unset");
  }
  const int clipRight = gpu_vk_wide_engine(core) ? gpu_vk_wide_engine_w(core) : 512;
  if (clipRight <= 0 || clipRight > INT16_MAX) {
    return refuse(std::move(out), Status::InvalidSelection, "clip_width");
  }

  const world_chunk_codec::RamView ram(std::span<const uint8_t>(core->ram));
  world_scene_prepare::Prepared prepared{};
  const char *why = "none";
  if (!world_scene_prepare::prepare(ram, selection, prepared, why)) {
    const Status status = why == std::string_view("active_animation") ? Status::ActiveAnimation
                                                                      : Status::InvalidSelection;
    return refuse(std::move(out), status, why);
  }
  out.broadVisible = prepared.broadVisible;
  out.selectedSectors = prepared.selectedSectors;
  out.lowSectors = (uint32_t)prepared.low.size();
  out.highSectors = (uint32_t)prepared.high.size();

  const ProjectionParams params = projection(core, clipRight);
  if (!world_lq_recipe::append(ram, prepared, params, clipRight, out, why) ||
      !world_hq_recipe::append(ram, prepared, params, clipRight, out, why, audit)) {
    return refuse(std::move(out), refusalStatus(why), why);
  }
  out.status = out.faces.empty() ? Status::ValidEmpty : Status::Ready;
  return out;
}

} // namespace spyro::world_scene
