#include "world_scene_builder.h"

#include "core.h"
#include "gpu_vk.h"
#include "wide_clip_plan.h"
#include "world_animation.h"
#include "world_chunk_codec.h"
#include "world_hq_recipe.h"
#include "world_lq_recipe.h"
#include "world_scene_prepare.h"
#include "world_source_animation.h"
#include "world_source_pair.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "wide_screen_space.h"

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

int renderWidth(Core *core) {
  return wide_screen_space::drawClipRight(core);
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

AnimationResult animate(Core *core, int32_t selection) {
  AnimationResult out{};
  if (core == nullptr || core->game == nullptr) {
    out.refusal = core ? "no_game" : "no_core";
    return out;
  }
  // Guest animation state follows retail's own 4:3 cull at every aspect. The walk and its
  // verification both run at the native width; a sector only the widened view admits is animated
  // for the draw alone, inside the captured Source (see `capture`), never here (issue 0152).
  const int clipRight = wide::kNativeClipWidth;
  const world_chunk_codec::RamView ram(std::span<const uint8_t>(core->ram));
  world_scene_prepare::Prepared prepared{};
  world_animation::Plan plan{};
  const char *why = "none";
  if (!world_scene_prepare::prepare(ram, selection, clipRight, prepared, why, &plan)) {
    out.refusal = why;
    return out;
  }
  for (const world_animation::Write &write : plan.writes) {
    if (write.width == 1u) {
      core->mem_w8(write.address, (uint8_t)write.value);
    } else {
      core->mem_w32(write.address, write.value);
    }
  }
  // The plan is only believed once the state it claims to have advanced actually reads back that
  // way: re-walk the same selection in the form that refuses on a live channel. A survivor here is
  // a real defect in the decode, and saying so beats a silent partial frame.
  world_scene_prepare::Prepared verified{};
  const char *residual = "none";
  if (!world_scene_prepare::prepare(ram, selection, clipRight, verified, residual)) {
    out.refusal = residual;
    return out;
  }
  out.ok = true;
  out.channels = plan.channels;
  out.direct = plan.direct;
  out.blended = plan.blended;
  out.writes = (uint32_t)plan.writes.size();
  if (!plan.writes.empty()) {
    out.lastAddress = plan.writes.back().address;
  }
  return out;
}

world_source::Source
capture(Core *core, int32_t selection, std::optional<uint32_t> cullingDistance) {
  if (!core || !core->game || !core->rsub.projParams.geomValid()) {
    world_source::Source out{};
    out.selection.refusal = "projection_unset";
    return out;
  }
  const int clipRight = renderWidth(core);
  const world_chunk_codec::RamView ram(std::span<const uint8_t>(core->ram));
  auto out = world_source::capture(
      ram, selection, projection(core, clipRight), clipRight, cullingDistance);
  const char *why = "none";
  if (!world_source_animation::animateDrawnOnly(ram, out, why)) {
    out.selection.valid = false;
    out.selection.refusal = why;
  }
  return out;
}

Recipe build(Core *core,
             int32_t selection,
             world_hq_recipe::Audit *audit,
             std::optional<uint32_t> cullingDistance) {
  return build(capture(core, selection, cullingDistance), audit);
}

namespace {
Recipe reconstruct(const world_source::Source &previous,
                   const world_source::Source &source,
                   std::optional<double> t,
                   world_hq_recipe::Audit *audit) {
  using spyro::ProjectionStream;
  const ProjectionStream culling = t ? ProjectionStream(previous.selection.camera.cullingMatrix,
                                                        source.selection.camera.cullingMatrix,
                                                        {},
                                                        *t)
                                     : ProjectionStream(source.selection.camera.cullingMatrix, {});
  const ProjectionStream projection =
      t ? ProjectionStream(previous.selection.camera.projectionMatrix,
                           source.selection.camera.projectionMatrix,
                           source.projection,
                           *t)
        : ProjectionStream(source.selection.camera.projectionMatrix, source.projection);
  Recipe out{};
  const int clipRight = source.clipRight;
  world_scene_prepare::Prepared prepared{};
  const char *why = "none";
  if (!world_scene_prepare::prepare(
          previous.selection, source.selection, culling, clipRight, prepared, why)) {
    const Status status = why == std::string_view("active_animation") ? Status::ActiveAnimation
                                                                      : Status::InvalidSelection;
    return refuse(std::move(out), status, why);
  }
  out.visibility = prepared.visibility;
  out.selectedSectors = prepared.selectedSectors;
  out.lowSectors = (uint32_t)prepared.low.size();
  out.highSectors = (uint32_t)prepared.high.size();

  const uint32_t farLimit = source.selection.cullingDistance >> 7;
  if (!world_lq_recipe::append(
          previous, source, prepared, projection, clipRight, farLimit, out, why) ||
      !world_hq_recipe::append(
          previous, source, prepared, projection, clipRight, out, why, audit)) {
    return refuse(std::move(out), refusalStatus(why), why);
  }
  out.status = out.faces.empty() ? Status::ValidEmpty : Status::Ready;
  return out;
}

} // namespace

Recipe build(const world_source::Source &source, world_hq_recipe::Audit *audit) {
  return reconstruct(source, source, std::nullopt, audit);
}

Recipe sample(const world_source::Source &previous, const world_source::Source &current, double t) {
  if (!std::isfinite(t) || t < 0.0 || t > 1.0) {
    return refuse({}, Status::InvalidSelection, "sample_t");
  }
  if (t == 0.0) {
    return build(previous);
  }
  if (t == 1.0) {
    return build(current);
  }
  const char *why = "none";
  if (!world_source_pair::compatible(previous, current, why)) {
    return refuse({}, Status::InvalidSelection, why);
  }
  return reconstruct(previous, current, t, nullptr);
}

} // namespace spyro::world_scene
