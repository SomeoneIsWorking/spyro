#include "field_environment_scene.h"

#include "core.h"
#include "world_scene_builder.h"

namespace spyro::field_environment_scene {

Status prepare(Core *core, Frame &frame) {
  frame = {};
  if (core == nullptr) {
    return Status::InvalidCore;
  }
  const field_environment::State state{
      .cameraOcclusionGroup = (int32_t)core->mem_r32(field_environment::kCameraOcclusionGroup),
      .occlusionGroupCount = (int32_t)core->mem_r32(field_environment::kOcclusionGroupCount),
      .stage = core->mem_r32(field_environment::kStageSelector)};
  frame.invocation = field_environment::derive(state);
  // Phase 1's animation channels run before anything reads the sector arrays, exactly where the
  // guest renderer runs them.
  frame.animation = world_scene::animate(core, frame.invocation.worldSelection);
  if (!frame.animation.ok) {
    return Status::AnimationRefused;
  }
  frame.world = world_scene::build(
      core, frame.invocation.worldSelection, nullptr, frame.invocation.cullingDistance);
  if (frame.world.status == world_recipe::Status::Ready) {
    return Status::Ready;
  }
  if (frame.world.status == world_recipe::Status::ValidEmpty) {
    return Status::ValidEmpty;
  }
  return Status::WorldRefused;
}

const char *statusName(Status status) {
  switch (status) {
  case Status::Ready:
    return "ready";
  case Status::ValidEmpty:
    return "valid empty";
  case Status::InvalidCore:
    return "invalid core";
  case Status::AnimationRefused:
    return "animation refused";
  case Status::WorldRefused:
    return "world refused";
  }
  return "unknown";
}

} // namespace spyro::field_environment_scene
