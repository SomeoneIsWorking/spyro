#include "actor_prefix_builder.h"

#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace {
using namespace spyro::actor_prefix;

unsigned checks = 0;

void require(bool condition, const char *what) {
  ++checks;
  if (!condition) {
    std::fprintf(stderr, "actor_prefix_builder: %s (check %u)\n", what, checks);
    std::abort();
  }
}

Input baseInput() {
  Input input{};
  input.header = 0;
  input.tx = 0;
  input.ty = 0;
  input.tz = 4096;
  input.matrixWords = {0x00001000u, 0, 0x00001000u, 0, 0x00001000u};
  input.vertexCount = 1;
  input.primary.firstFull = 0;
  input.colorArm = ColorArm::High;
  input.primaryColors = {0xAA332211u};
  input.primitiveWords = {4, 0x12345678u};
  input.projection = {.ofx = 160 << 16, .ofy = 120 << 16, .h = 256};
  return input;
}

void testHighAndCorruptions() {
  const Input input = baseInput();
  const Output expected = build(input);
  require(expected.status == Status::Ok, "reached High input refused");
  require(expected.vertices.size() == 1 && expected.colors.size() == 1 &&
              expected.primitiveWords.size() == 2,
          "owned output denominator mismatch");
  require(expected.vertices[0].projected.sx == 160 && expected.vertices[0].projected.sy == 120,
          "identity endpoint mismatch");
  require(compareOutputs(expected, build(input)).mismatches == 0, "self comparison failed");

  Input transformCorrupt = input;
  transformCorrupt.tx = 4096;
  const CompareResult transformDiff = compareOutputs(expected, build(transformCorrupt));
  require(transformDiff.vertices == 1 && transformDiff.mismatches != 0 &&
              std::string_view(transformDiff.firstField) == "control",
          "transform corruption was not named by shared comparator");

  Input vertexCorrupt = input;
  vertexCorrupt.primary.firstFull = 1u << 21;
  const CompareResult vertexDiff = compareOutputs(expected, build(vertexCorrupt));
  require(vertexDiff.mismatches != 0 && std::string_view(vertexDiff.firstField) == "primary_vertex",
          "packed vertex corruption was not named by shared comparator");

  Output projectedCorrupt = expected;
  ++projectedCorrupt.vertices[0].projected.sx;
  const CompareResult projectedDiff = compareOutputs(expected, projectedCorrupt);
  require(projectedDiff.mismatches != 0 && std::string_view(projectedDiff.firstField) == "sxy",
          "projected output corruption was not named by comparator");

  Output colorCorrupt = expected;
  colorCorrupt.colors[0] ^= 1u;
  const CompareResult colorDiff = compareOutputs(expected, colorCorrupt);
  require(colorDiff.mismatches != 0 && std::string_view(colorDiff.firstField) == "color",
          "color output corruption was not named by comparator");

  Output scratchCorrupt = expected;
  scratchCorrupt.vertices[0].scratchWord ^= 1u;
  const CompareResult scratchDiff = compareOutputs(expected, scratchCorrupt);
  require(scratchDiff.mismatches != 0 && std::string_view(scratchDiff.firstField) == "scratch_word",
          "scratch output corruption was not named by comparator");
}

void testPositiveBlendAndRefusals() {
  Input input = baseInput();
  input.header = 0x00000100u;
  input.alternate.firstFull = 0;
  input.colorArm = ColorArm::PositiveBlend;
  input.cr30 = 512;
  input.secondaryColors = {0x00554433u};
  const Output blended = build(input);
  require(blended.status == Status::Ok && blended.vertices.size() == 1 &&
              blended.colors.size() == 1 && blended.colors[0] != input.primaryColors[0] &&
              blended.fog == 0x01000000u,
          "reached PositiveBlend input refused or not blended");

  input.optionalExpansion = true;
  require(build(input).status == Status::OptionalExpansion,
          "optional expansion was silently accepted");
  input.optionalExpansion = false;
  input.header |= 1;
  require(build(input).status == Status::TransformBlend,
          "uncovered transform blend was silently accepted");
  input.header &= ~1u;
  input.header |= 0x80000000u;
  const Output statusVisible = build(input);
  require(statusVisible.status == Status::Ok && statusVisible.commonStatus == 0 &&
              statusVisible.vertices[0].scratchWord == 0x0F001400u,
          "visible status output was not encoded exactly");
  Input rejectedInput = input;
  rejectedInput.projection.ofx = -2000 * 65536;
  const Output statusRejected = build(rejectedInput);
  require(statusRejected.status == Status::VisibilityRejected && statusRejected.commonStatus != 0 &&
              statusRejected.colors.empty(),
          "common status rejection was not preserved");
  Output rejectedCorrupt = statusRejected;
  rejectedCorrupt.commonStatus ^= 1u;
  require(std::string_view(compareOutputs(statusRejected, rejectedCorrupt).firstField) ==
              "common_status",
          "status corruption was not named by comparator");

  const std::array ownedRecords{blended, statusRejected};
  const CallBoundary owned = classifyCall(ownedRecords);
  require(owned.status == CallStatus::Owned && owned.records == 2 && owned.visibleRecords == 1 &&
              owned.rejectedRecords == 1 && owned.unsupportedRecords == 0,
          "mixed visible/rejected call was not atomically owned");
  require(classifyCall(std::span<const Output>{}).status == CallStatus::NoCorpus,
          "empty call did not refuse as no corpus");
  input.header &= ~0x80000000u;
  input.colorArm = ColorArm::Plain;
  const Output plain = build(input);
  require(plain.status == Status::PlainColor, "Plain arm was silently accepted");
  const std::array unsupportedRecords{blended, plain};
  const CallBoundary unsupported = classifyCall(unsupportedRecords);
  require(unsupported.status == CallStatus::Unsupported && unsupported.records == 2 &&
              unsupported.visibleRecords == 1 && unsupported.unsupportedRecords == 1,
          "unsupported record did not refuse the whole call");
  input.colorArm = ColorArm::NegativeBlend;
  require(build(input).status == Status::NegativeBlend, "NegativeBlend arm was silently accepted");
}

} // namespace

int main() {
  testHighAndCorruptions();
  testPositiveBlendAndRefusals();
  std::printf("actor_prefix_builder: PASS (%u checks)\n", checks);
  return 0;
}
