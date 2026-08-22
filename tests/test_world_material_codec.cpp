#include "world_material_codec.h"

#include <cstdlib>

using namespace spyro::world_material_codec;

namespace {

void require(bool condition) {
  if (!condition) {
    std::abort();
  }
}

} // namespace

int main() {
  const MaterialKey opaque = classify(5, 0), semi = classify((int8_t)0x85, 2),
                    flat = classify(-1, 0);
  require(opaque.textured && !opaque.semiTransparent && opaque.index == 5);
  require(semi.textured && semi.semiTransparent && semi.index == 5 && semi.orientation == 2);
  require(!flat.textured && !flat.semiTransparent);

  const Tile tile{0x12341408u, 0x56782818u};
  const DecodedTile quad = decodeQuad(tile, 0);
  require(quad.u[0] == 8 && quad.v[0] == 20 && quad.u[1] == 24 && quad.v[1] == 40 &&
          quad.u[2] == 8 && quad.v[2] == 51 && quad.u[3] == 39 && quad.v[3] == 51 &&
          quad.clut == 0x1234 && quad.tpage == 0x5678);
  const DecodedTile triangle = decodeTriangle(tile, 2, 0);
  require(triangle.u[0] == 39 && triangle.v[0] == 51 && triangle.u[1] == 249 &&
          triangle.v[1] == 70 && triangle.u[2] == 24 && triangle.v[2] == 40);

  const DirectFade titleDirect = directFade(32677, 0x2000);
  require(titleDirect.halfOffset == 0 && titleDirect.step == 15);
  require(directFade(0, 0x2000).halfOffset == 8 && directFade(0, 0x2000).step == 0);

  require(fogColor(0x00000000u, 0x00ffffffu, 100, 100) == 0x00ffffffu);
  require(fogColor(0x00123456u, 0x00ffffffu, 0x1000, 0) == 0x00123456u);
  const uint32_t mixed = fogColor(0x00000000u, 0x00ffffffu, 0x800, 0);
  require((mixed & 0x00ffffffu) == 0x007f7f7fu);
  require(fogColor(0x00866861u, 0x00f7bbb6u, 0x2000, 7974) == 0x00f1b6b1u);
  return 0;
}
