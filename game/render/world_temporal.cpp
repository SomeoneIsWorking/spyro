#include "world_temporal.h"

#include "core.h"
#include "fx_paired_actor.h"
#include "producer_scope.h"
#include "world_scene_builder.h"
#include "world_source_pair.h"

#include <lucent/log.h>
#include <tuple>
#include <utility>

namespace spyro::world_temporal {
namespace {

bool resident(const Core &core, const Frame &frame) {
  for (const auto &resource : frame.resources) {
    if (core.currentImageIdentity(resource.range) != resource.image) {
      return false;
    }
  }
  return true;
}

bool sameResources(const Frame &a, const Frame &b) {
  if (a.resources.size() != b.resources.size()) {
    return false;
  }
  for (size_t i = 0; i < a.resources.size(); ++i) {
    const auto &left = a.resources[i];
    const auto &right = b.resources[i];
    if (left.range.begin != right.range.begin || left.range.end != right.range.end ||
        left.image != right.image) {
      return false;
    }
  }
  return true;
}

bool sameDrawPolicy(const world_scene_submitter::DrawState &a,
                    const world_scene_submitter::DrawState &b) {
  // Physical destinations may flip; viewport shape and material sampling policy may not.
  const auto policy = [](const world_scene_submitter::DrawState &draw) {
    return std::tuple{int64_t{draw.areaLeft} - draw.offsetX,
                      int64_t{draw.areaTop} - draw.offsetY,
                      int64_t{draw.areaRight} - draw.offsetX,
                      int64_t{draw.areaBottom} - draw.offsetY,
                      draw.windowMaskX,
                      draw.windowMaskY,
                      draw.windowOffsetX,
                      draw.windowOffsetY,
                      draw.dither,
                      draw.projectionH};
  };
  return policy(a) == policy(b);
}

bool cameraMatches(const Frame &world, const SpyroPairedFrame &paired) {
  const auto &camera = world.source.selection.camera;
  const SceneCameraInputs inputs{true, camera.projectionMatrix.m, camera.position};
  return world.serial != 0 && world.serial == paired.frameSerial &&
         inputs == paired.transform.sceneCamera;
}

} // namespace

void History::begin(uint64_t scene, bool reference, bool active) {
  ++serial_;
  eligible = false;
  current_.reset();
  seen_ = false;
  refused_ = false;
  const bool enabled = active && !reference;
  if (!enabled || enabled != active_ || scene != scene_) {
    previous_.reset();
  }
  scene_ = scene;
  active_ = enabled;
}

bool History::retain(const Core &core,
                     world_source::Source source,
                     world_scene_submitter::DrawState draw) {
  if (!active_) {
    return false;
  }
  if (seen_ || refused_ || !source.selection.valid || draw.areaLeft > draw.areaRight ||
      draw.areaTop > draw.areaBottom) {
    refuse();
    return false;
  }
  seen_ = true;
  std::vector<Resource> resources;
  for (const auto range : source.resourceRanges()) {
    const auto image = core.currentImageIdentity(range);
    if (!image) {
      lucent::debug("worldtemporal",
                    "REFUSED frame={} resource=[{:08X},{:08X}) has no complete residency",
                    serial_,
                    range.begin,
                    range.end);
      refuse();
      return false;
    }
    resources.push_back({range, *image});
  }
  current_ = Frame{std::move(source), draw, std::move(resources), serial_};
  return true;
}

void History::refuse() {
  current_.reset();
  eligible = false;
  refused_ = true;
}

void History::rotate() {
  previous_ = std::move(current_);
  current_.reset();
  eligible = false;
}

bool History::compatible(const Core &core, const char *&why) const {
  if (!active_ || !previous_ || !current_ || refused_) {
    why = "missing consecutive world source";
    return false;
  }
  if (previous_->serial + 1 != current_->serial) {
    why = "nonconsecutive world frames";
    return false;
  }
  if (!sameResources(*previous_, *current_) || !resident(core, *previous_) ||
      !resident(core, *current_)) {
    why = "world resource residency changed";
    return false;
  }
  if (!sameDrawPolicy(previous_->draw, current_->draw)) {
    why = "world draw policy changed";
    return false;
  }
  return world_source_pair::compatible(previous_->source, current_->source, why);
}

bool History::camerasMatch(const SpyroPairedFrame &previous,
                           const SpyroPairedFrame &current) const {
  return previous_ && current_ && cameraMatches(*previous_, previous) &&
         cameraMatches(*current_, current);
}

bool History::emit(Core &core, RenderQueue &target, double t) const {
  const char *why = nullptr;
  if (!compatible(core, why)) {
    return false;
  }
  const auto recipe = world_scene::sample(previous_->source, current_->source, t);
  // Both endpoints and every interior sample draw into the CURRENT owned destination.
  const auto plan = world_scene_submitter::prepare(current_->draw, target, kProducerKey, recipe);
  ProducerScope producer(&core.rsub.producerScope, kProducerKey, "world:static");
  return world_scene_submitter::emit(&core, target, kProducerKey, recipe, plan);
}

} // namespace spyro::world_temporal
