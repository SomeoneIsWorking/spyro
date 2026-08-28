#pragma once

#include "field_environment_recipe.h"
#include "world_recipe.h"
#include "world_scene_builder.h"

#include <cstdint>

class Core;

namespace spyro::field_environment_scene {

enum class Status : uint8_t { Ready, ValidEmpty, InvalidCore, AnimationRefused, WorldRefused };

struct Frame {
  field_environment::Invocation invocation{};
  world_scene::AnimationResult animation{};
  world_recipe::Recipe world{};
};

Status prepare(Core *core, Frame &frame);
const char *statusName(Status status);

} // namespace spyro::field_environment_scene
