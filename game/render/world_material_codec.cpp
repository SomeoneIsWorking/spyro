#include "world_material_codec.h"

#include "actor_model_codec.h"

namespace spyro::world_material_codec {
namespace {

uint8_t u(uint32_t packed) {
  return (uint8_t)packed;
}

uint8_t v(uint32_t packed) {
  return (uint8_t)(packed >> 8);
}

DecodedTile decode(std::array<uint32_t, 4> packed) {
  DecodedTile out{};
  for (uint32_t i = 0; i < packed.size(); ++i) {
    out.u[i] = u(packed[i]);
    out.v[i] = v(packed[i]);
  }
  out.clut = (uint16_t)(packed[0] >> 16);
  out.tpage = (uint16_t)(packed[1] >> 16);
  return out;
}

} // namespace

MaterialKey classify(int8_t materialId, uint8_t orientation) {
  MaterialKey out{};
  out.textured = materialId != -1;
  out.semiTransparent = materialId < -1;
  out.index = (uint8_t)materialId & 0x7fu;
  out.orientation = orientation & 3u;
  return out;
}

DecodedTile decodeQuad(Tile tile, uint32_t fadeStep) {
  tile.first += fadeStep << 22;
  return decode({tile.first, tile.second, tile.first + 0x1f00u, tile.first + 0x1f1fu});
}

DecodedTile decodeTriangle(Tile tile, uint8_t orientation, uint32_t fadeStep) {
  tile.first += fadeStep << 22;
  switch (orientation & 3u) {
  case 0:
    return decode({tile.first, tile.second, tile.first + 0x1f00u, 0});
  case 1:
    return decode({tile.first + 0x1fu, tile.second + 0x1f00u, tile.first, 0});
  case 2:
    return decode({tile.first + 0x1f1fu, tile.second + 0x1ee1u, tile.second, 0});
  default:
    return decode({tile.first + 0x1f00u, tile.second - 0x1fu, tile.first + 0x1f1fu, 0});
  }
}

DirectFade directFade(uint32_t depthSum, uint32_t fogEnd) {
  const int32_t numerator = (int32_t)((depthSum >> 7) << 5) - (int32_t)fogEnd + 0x1000;
  return {.halfOffset = numerator <= 0 ? 8u : 0u,
          .step = numerator > 0 ? (uint32_t)numerator >> 8 : 0u};
}

uint32_t fogColor(uint32_t farRgb, uint32_t nearRgb, uint32_t fogEnd, uint16_t depth) {
  const int32_t factor = (int32_t)fogEnd - (int32_t)depth;
  if (factor <= 0) {
    return nearRgb;
  }
  if (factor >= 0x1000) {
    return farRgb;
  }
  const std::array<int32_t, 3> target = {(int32_t)(farRgb & 0xffu) << 4,
                                         (int32_t)((farRgb >> 8) & 0xffu) << 4,
                                         // The guest writes FC3 with `farRgb >> 12`, without the
                                         // FF0 mask used for FC1/FC2. The upper nibble of green is
                                         // therefore intentionally retained below blue.
                                         (int32_t)(farRgb >> 12)};
  return actor_model_codec::depthCueRgb(nearRgb, target, (int16_t)factor).rgb;
}

world_recipe::Material painterMaterial(const MaterialKey &key, const DecodedTile &tile) {
  world_recipe::Material out{};
  out.textured = key.textured;
  out.semiTransparent = key.semiTransparent;
  out.clut = tile.clut;
  out.tpage = tile.tpage;
  return out;
}

} // namespace spyro::world_material_codec
