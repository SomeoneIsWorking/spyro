#include "actor_draw_recipe.h"

#include <array>
#include <cstdio>
#include <cstdlib>

namespace {
using namespace spyro::actor_draw_recipe;
namespace actor_prefix = spyro::actor_prefix;

unsigned checks = 0;

void require(bool condition, const char *message) {
  ++checks;
  if (!condition) {
    std::fprintf(stderr, "actor_draw_recipe: %s (check %u)\n", message, checks);
    std::abort();
  }
}

PrimitiveInput triangle(uint32_t control = 0) {
  PrimitiveInput input{};
  input.words[0] = control;
  input.xy = {0x00000000u, 0x0000000au, 0x000a0000u, 0};
  input.depth = {1000, 1000, 1000, 1000};
  input.color = {0x00112233u, 0x00445566u, 0x00778899u, 0x00aabbccu};
  input.shift = 4;
  return input;
}

void testEvaluatorFamiliesAndNegatives() {
  auto g3 = evaluate(triangle());
  require(g3.supported && g3.emitted && g3.family == Family::G3 && g3.localBin == 250 &&
              g3.payload.size() == 7,
          "G3 evaluation mismatch");
  auto gt3Input = triangle(2u);
  gt3Input.words[2] = 0x00110022u;
  gt3Input.words[3] = 0x00330044u;
  gt3Input.words[4] = 0x00550066u;
  const auto gt3 = evaluate(gt3Input);
  require(gt3.emitted && gt3.family == Family::GT3 && gt3.payload.size() == 10,
          "GT3 evaluation mismatch");

  PrimitiveInput quad = triangle(0x80000000u);
  quad.xy = {0x00000000u, 0x000a0000u, 0x0000000au, 0x000a000au};
  quad.words[2] = 0;
  const auto g4 = evaluate(quad);
  require(g4.emitted && g4.family == Family::G4 && g4.origin == Origin::FullQuad &&
              g4.payload.size() == 9,
          "G4 evaluation mismatch");
  quad.words[0] |= 2u;
  quad.words[3] = 1;
  quad.words[4] = 2;
  quad.words[5] = 3;
  const auto gt4 = evaluate(quad);
  require(gt4.emitted && gt4.family == Family::GT4 && gt4.payload.size() == 13,
          "GT4 evaluation mismatch");

  auto outcode = triangle();
  outcode.shift = 0x80000004u;
  outcode.status = {4, 4, 4, 4};
  require(!evaluate(outcode).emitted && evaluate(outcode).reason == Reason::Outcode,
          "outcode rejection mismatch");
  auto semi = triangle();
  semi.words[1] = 1;
  const auto semiResult = evaluate(semi);
  require(semiResult.supported && semiResult.emitted &&
              semiResult.payload[1] == g3.payload[1] + 0x02000000u,
          "guest semitransparent command bit was not preserved");
  auto corrupt = g3;
  corrupt.payload[2] ^= 1u;
  require(corrupt.payload != g3.payload, "payload corruption discriminator failed");
}

actor_prefix::Output record(uint32_t control) {
  actor_prefix::Output output{};
  output.status = actor_prefix::Status::Ok;
  output.depthOrigin = 0;
  output.otShift = 4;
  output.primitiveWords = {control | 0x00002020u, 0};
  output.colors = {0x00112233u};
  output.vertices.resize(3);
  const std::array<std::array<int16_t, 2>, 3> xy{{{{0, 0}}, {{10, 0}}, {{0, 10}}}};
  for (unsigned i = 0; i < 3; ++i) {
    output.vertices[i].projected.sx = xy[i][0];
    output.vertices[i].projected.sy = xy[i][1];
    output.vertices[i].projected.sz = 1000;
    output.vertices[i].scratchWord = (uint16_t)xy[i][0] | ((uint32_t)(uint16_t)xy[i][1] << 16);
  }
  return output;
}

void testAtomicComposition() {
  const auto visible = record(0);
  const Recipe ready = compose(std::span(&visible, 1));
  require(ready.status == Status::Ready && ready.records == 1 && ready.candidates == 1 &&
              ready.faces.size() == 1 && ready.faces[0].sourceOrdinal == 0,
          "visible recipe mismatch");

  auto empty = visible;
  empty.primitiveWords[0] |= 8u;
  const Recipe validEmpty = compose(std::span(&empty, 1));
  require(validEmpty.status == Status::ValidEmpty && validEmpty.candidates == 1 &&
              validEmpty.rejectedCandidates == 1 && validEmpty.faces.empty(),
          "valid-empty recipe mismatch");
  require(compose({}).status == Status::NoCorpus, "empty corpus was not distinguished");

  auto unsupported = visible;
  unsupported.status = actor_prefix::Status::NegativeBlend;
  const std::array mixed{visible, unsupported};
  const Recipe refused = compose(mixed);
  require(refused.status == Status::Unsupported && refused.firstReason == Reason::Prefix &&
              refused.faces.empty(),
          "unsupported record did not atomically clear prior faces");

  auto malformed = visible;
  malformed.primitiveWords[0] = 0x7fc00000u;
  const Recipe bad = compose(std::span(&malformed, 1));
  require(bad.status == Status::Unsupported && bad.firstReason == Reason::Malformed &&
              bad.faces.empty(),
          "malformed vertex offset was not atomically refused");

  actor_prefix::Output rejected{};
  rejected.status = actor_prefix::Status::VisibilityRejected;
  const Recipe rejectedOnly = compose(std::span(&rejected, 1));
  require(rejectedOnly.status == Status::ValidEmpty && rejectedOnly.rejectedRecords == 1,
          "visibility-rejected record was not valid empty");
}

} // namespace

int main() {
  testEvaluatorFamiliesAndNegatives();
  testAtomicComposition();
  std::printf("actor_draw_recipe: PASS (%u checks)\n", checks);
  return 0;
}
