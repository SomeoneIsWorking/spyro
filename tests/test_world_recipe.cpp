#include "world_recipe.h"

#include <cstdlib>
#include <string_view>
#include <vector>

using namespace spyro::world_recipe;

namespace {

Vertex vertex(int16_t x, int16_t y, uint32_t rgb) {
  Vertex out{};
  out.sx = x;
  out.sy = y;
  out.sz = (uint16_t)(x + y + 1000);
  out.screenX = x + 0.25f;
  out.screenY = y + 0.5f;
  out.viewZ = x + y + 1000.75f;
  out.rgb = rgb;
  out.u = (uint8_t)x;
  out.v = (uint8_t)y;
  return out;
}

void require(bool condition) {
  if (!condition) {
    std::abort();
  }
}

} // namespace

int main() {
  const Vertex a = vertex(0, 0, 0x00102030u), b = vertex(10, 20, 0x00304050u);
  const Vertex m = midpoint(a, b);
  require(m.sx == 5 && m.sy == 10 && m.sz == 1015 && m.rgb == 0x00203040u);
  Vertex clipLeft = vertex(-1, 10, 0), clipRight = vertex(1, 10, 0);
  clipLeft.clip = clipRight.clip = 0;
  require(midpoint(clipLeft, clipRight).clip == 4u);

  Face tri{};
  tri.family = Family::GT3;
  tri.vertexCount = 3;
  tri.material.textured = true;
  tri.paintGroup = 7;
  tri.vertices = {
      vertex(0, 0, 0x00101010u), vertex(100, 0, 0x00202020u), vertex(0, 100, 0x00303030u), {}};
  std::vector<Face> children;
  require(adaptiveSubdivide(tri, children, 64));
  require(children.size() == 7);
  require(children[0].origin == Origin::Adaptive);
  require(children[4].origin == Origin::EdgeFiller);
  for (uint32_t i = 0; i < children.size(); ++i) {
    require(children[i].paintGroup == 7 && children[i].paintSuborder == i);
  }
  require(compare(children, children).equal);

  auto corrupt = children;
  corrupt[2].vertices[1].sx ^= 1;
  Difference difference = compare(children, corrupt);
  require(!difference.equal && difference.face == 2 && difference.field == std::string_view("sx"));

  corrupt = children;
  ++corrupt[2].vertices[1].sz;
  difference = compare(children, corrupt);
  require(!difference.equal && difference.face == 2 && difference.field == std::string_view("sz"));

  corrupt = children;
  corrupt[2].vertices[1].viewZ += 0.25f;
  difference = compare(children, corrupt);
  require(!difference.equal && difference.face == 2 &&
          difference.field == std::string_view("viewZ"));

  std::vector<Face> capped;
  require(!adaptiveSubdivide(tri, capped, 3));

  Recipe recipe{};
  Face near = children[0], far = children[1], replacementA = children[2],
       replacementB = children[3];
  near.otBin = 3;
  far.otBin = 8;
  replacementA.otBin = replacementB.otBin = 3;
  near.paintGroup = 1;
  far.paintGroup = 0;
  replacementA.paintGroup = replacementB.paintGroup = 2;
  replacementA.paintSuborder = 0;
  replacementB.paintSuborder = 1;
  recipe.faces = {near, replacementB, far, replacementA};
  std::vector<size_t> order;
  require(paintOrder(recipe.faces, order));
  require((order == std::vector<size_t>{2, 3, 1, 0}));

  recipe.faces[1].paintSuborder = 0;
  require(!paintOrder(recipe.faces, order));
  return 0;
}
