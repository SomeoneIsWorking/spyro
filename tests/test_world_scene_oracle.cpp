#include "world_scene_oracle.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

spyro::world_recipe::Face
face(uint16_t bin, uint32_t paintGroup, uint32_t paintSuborder, uint32_t source, int16_t sx) {
  spyro::world_recipe::Face out{};
  out.family = spyro::world_recipe::Family::GT3;
  out.vertexCount = 3;
  out.otBin = bin;
  out.paintGroup = paintGroup;
  out.paintSuborder = paintSuborder;
  out.source = source;
  out.material = {.textured = true, .semiTransparent = false, .clut = 0x31, .tpage = 0x82};
  out.vertices[0] = {.sx = sx, .sy = 2, .sz = 3, .rgb = 0x102030, .u = 4, .v = 5};
  out.vertices[1] = {.sx = 6, .sy = 7, .sz = 8, .rgb = 0x405060, .u = 9, .v = 10};
  out.vertices[2] = {.sx = 11, .sy = 12, .sz = 13, .rgb = 0x708090, .u = 14, .v = 15};
  return out;
}

} // namespace

int main() {
  const std::array faces = {
      face(3, 0, 0, 1, 10), face(8, 1, 0, 2, 20), face(8, 4, 1, 3, 30), face(8, 4, 0, 4, 40)};
  std::vector<spyro::world_scene_oracle::Record> records;
  check(spyro::world_scene_oracle::expected(faces, records), "valid paint order accepted");
  check(records.size() == 4, "expected record count");
  check(records[0].otBin == 8 && records[0].vertices[0].sx == 40,
        "highest group child zero is first");
  check(records[1].otBin == 8 && records[1].vertices[0].sx == 30,
        "adaptive child suborder is ascending");
  check(records[2].otBin == 8 && records[2].vertices[0].sx == 20,
        "same-bin paint groups are descending");
  check(records[3].otBin == 3 && records[3].vertices[0].sx == 10, "lowest bin is last");
  check(spyro::world_scene_oracle::compare(records, records).equal, "positive equality");
  check(spyro::world_scene_oracle::corruptionSelftest(), "corruption negative control");

  auto corrupted = records;
  corrupted[1].vertices[2].sy++;
  const auto difference = spyro::world_scene_oracle::compare(records, corrupted);
  check(!difference.equal && difference.record == 1 && difference.field == std::string_view("sy"),
        "exact field and record reported");

  auto invalid = faces;
  invalid[0].paintGroup = UINT32_MAX;
  check(!spyro::world_scene_oracle::expected(invalid, records),
        "missing paint identity is refused");
  check(records.empty(), "refused ordering returns no partial records");
  invalid = faces;
  invalid[0].vertexCount = 4;
  check(!spyro::world_scene_oracle::expected(invalid, records), "family/count mismatch is refused");

  std::cout << "PASS: world scene oracle ordering, equality, and corruption negative\n";
  return 0;
}
