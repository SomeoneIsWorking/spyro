#pragma once

#include "cyclorama_portal_mesh_recipe.h"

#include <array>
#include <cstdint>
#include <vector>

class Core;

namespace spyro::cyclorama_mask_recipe {

constexpr uint32_t kProducerKey = 0x8004fea0u;

enum class Status : uint8_t {
  Ready,
  ValidEmpty,
  InvalidCore,
  InvalidFrame,
  InvalidClipRegion,
  CapacityExceeded,
};

struct Vertex {
  int16_t sx = 0;
  int16_t sy = 0;
  float screenX = 0.0f;
  float screenY = 0.0f;
};

struct Face {
  std::array<Vertex, 3> vertices{};
  uint32_t rgb = 0;
};

struct Recipe {
  Status status = Status::InvalidCore;
  const char *refusal = "invalid_core";
  uint32_t portalOrdinal = 0;
  uint16_t otBin = 0;
  std::vector<Face> faces;
};

// 0x8004FEA0 starts with the two source-defined full-screen POLY_F3 packets at
// D_8006CB8C, then clips each against D_80077EA0. This is the native, pure
// transcription of that mask family; publication belongs to the submitter.
Recipe build(Core *core, const cyclorama_portal_mesh::PortalFrame &frame);

const char *statusName(Status status);

} // namespace spyro::cyclorama_mask_recipe
