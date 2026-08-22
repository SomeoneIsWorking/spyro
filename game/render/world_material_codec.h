#pragma once

#include "world_recipe.h"

#include <array>
#include <cstdint>

namespace spyro::world_material_codec {

struct Tile {
  uint32_t first = 0;
  uint32_t second = 0;
};

struct DecodedTile {
  std::array<uint8_t, 4> u{};
  std::array<uint8_t, 4> v{};
  uint16_t clut = 0;
  uint16_t tpage = 0;
};

struct MaterialKey {
  bool textured = false;
  bool semiTransparent = false;
  uint8_t index = 0;
  uint8_t orientation = 0;
};

struct DirectFade {
  uint32_t halfOffset = 0;
  uint32_t step = 0;
};

MaterialKey classify(int8_t materialId, uint8_t orientation);
DecodedTile decodeQuad(Tile tile, uint32_t fadeStep);
DecodedTile decodeTriangle(Tile tile, uint8_t orientation, uint32_t fadeStep);

// HQ direct faces reuse the depth value after the guest has quantized it for
// the OT (`depthSum >> 7`). The subsequent `<< 5` is therefore not equivalent
// to shifting the unquantized sum; preserving that operation order controls
// both the selected texture half and CLUT fade bits.
DirectFade directFade(uint32_t depthSum, uint32_t fogEnd);

// Exact 0x80026DF0 fog rule. farRgb is color plane 0, nearRgb is
// color plane 1, and depth is the face vertex's SZ value.
uint32_t fogColor(uint32_t farRgb, uint32_t nearRgb, uint32_t fogEnd, uint16_t depth);

world_recipe::Material painterMaterial(const MaterialKey &key, const DecodedTile &tile);

} // namespace spyro::world_material_codec
