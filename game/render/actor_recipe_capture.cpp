#include "actor_recipe_capture.h"

#include "core.h"

#include <algorithm>
#include <lucent/log.h>

namespace spyro::actor_recipe_capture {
namespace {

bool copy_model_stream(Core *c, uint32_t model, uint32_t count, actor_prefix::OwnedStream &out) {
  if (!physical_span(model, 8u)) {
    return false;
  }
  const uint32_t model0 = c->mem_r32(kseg(model));
  const uint32_t model1 = c->mem_r32(kseg(model + 4u));
  uint32_t full = model0 & 0x1fffffu;
  uint32_t delta = full + ((model1 >> 24) << 2);
  if (!physical_span(full, 4u) || delta > 0x1fffffu) {
    return false;
  }
  out = {};
  out.firstFull = c->mem_r32(kseg(full));
  full += 4;
  uint32_t selector = out.firstFull;
  for (uint32_t i = 1; i < count; ++i) {
    if (selector & 1u) {
      if (delta > 0x1ffffeu) {
        return false;
      }
      const int16_t word = (int16_t)c->mem_r16(kseg(delta));
      out.deltaWords.push_back(word);
      selector = (uint16_t)word;
      delta += 2;
    } else {
      if (!physical_span(full, 4u)) {
        return false;
      }
      selector = c->mem_r32(kseg(full));
      out.fullWords.push_back(selector);
      full += 4;
    }
  }
  return true;
}

bool copy_primitive_patches(Core *c,
                            uint32_t model,
                            uint32_t modelMeta,
                            uint32_t primitiveBytes,
                            std::vector<actor_prefix::PrimitivePatch> &patches) {
  patches.clear();
  const uint32_t tableOffset = (modelMeta & 0xffffu) << 2;
  if (tableOffset == 0u) {
    return true;
  }
  uint32_t cursor = model + tableOffset;
  constexpr uint32_t kMaxPatchGroups = 1024u;
  for (uint32_t group = 0; group < kMaxPatchGroups; ++group) {
    if (!physical_span(cursor, 4u)) {
      lucent::debug("actordirect",
                    "REFUSED expansion table header group={} cursor=0x{:08X}: outside guest RAM",
                    group,
                    cursor);
      return false;
    }
    const uint32_t header = c->mem_r32(kseg(cursor));
    cursor += 4u;
    const uint32_t byteOffset = header & 0xffffu;
    const uint32_t wordCount = (header >> 24) & 0x7fu;
    const bool aligned = (byteOffset & 3u) == 0u;
    const bool afterLengthWord = byteOffset >= 4u;
    const bool beginsInCommand = byteOffset <= primitiveBytes + 4u;
    const bool fitsCommand =
        beginsInCommand && wordCount <= (primitiveBytes + 4u - byteOffset) / 4u;
    const bool tableReadable = physical_span(cursor, std::max(1u, wordCount) * 4u);
    lucent::debug("actordirect",
                  "expansion group={} header={:08X} offset={} count={} primitive_bytes={} "
                  "continue={} predicates(aligned={},after_length={},begins={},fits={},table={})",
                  group,
                  header,
                  byteOffset,
                  wordCount,
                  primitiveBytes,
                  (int32_t)header > 0,
                  aligned,
                  afterLengthWord,
                  beginsInCommand,
                  fitsCommand,
                  tableReadable);
    if (!aligned || !afterLengthWord || !fitsCommand || !tableReadable) {
      patches.clear();
      return false;
    }
    actor_prefix::PrimitivePatch patch{};
    patch.wordOffset = byteOffset / 4u - 1u;
    patch.words.reserve(wordCount);
    for (uint32_t word = 0; word < wordCount; ++word) {
      patch.words.push_back(c->mem_r32(kseg(cursor)));
      cursor += 4u;
    }
    patches.push_back(std::move(patch));
    if ((int32_t)header <= 0) {
      return true;
    }
  }
  patches.clear();
  return false;
}

} // namespace

bool capture_source(Core *c, const SourceRecord &source, Record &capture) {
  const uint32_t descriptor = source.descriptor;
  const uint32_t model = source.model;
  const uint32_t alternate = source.alternate;
  if (!physical_span(descriptor, 36u) || !physical_span(model, 8u) ||
      !c->rsub.projParams.geomValid()) {
    return false;
  }
  auto &input = capture.input;
  input = {};
  input.header = source.header;
  input.tx = source.tx;
  input.ty = source.ty;
  input.tz = source.tz;
  input.matrixWords = source.matrixWords;
  // These values come from the game's published projection state, not ambient GTE output. The
  // live oracle independently compares the resulting recipe with 0x8001F798's reached path.
  input.cr29 = (int32_t)c->mem_r32(0x8007591Cu);
  input.cr30 = (int16_t)(input.matrixWords[4] >> 16);
  input.transformShift = c->mem_r8(kseg(descriptor + 5u));
  input.streamShift = (uint8_t)(c->mem_r8(kseg(descriptor + 6u)) + 1u);
  input.vertexCount = c->mem_r8(kseg(descriptor + 8u));
  const uint32_t modelMeta = c->mem_r32(kseg(model + 4u));
  const uint16_t blend = (uint16_t)((input.header & 0xff00u) >> 2);
  if (input.vertexCount != 0 && !copy_model_stream(c, model, input.vertexCount, input.primary)) {
    return false;
  }
  if (blend != 0 && (!physical_span(alternate, 8u) ||
                     !copy_model_stream(c, alternate, input.vertexCount, input.alternate))) {
    return false;
  }
  input.projection = {.ofx = (int32_t)(c->rsub.projParams.geomOfx() * 65536.0f),
                      .ofy = (int32_t)(c->rsub.projParams.geomOfy() * 65536.0f),
                      .h = (uint16_t)c->rsub.projParams.geomH()};
  if (input.cr30 >= 1024) {
    input.colorArm = actor_prefix::ColorArm::High;
  } else if (input.cr30 > 0) {
    input.colorArm = actor_prefix::ColorArm::PositiveBlend;
  } else if (input.cr29 <= 0 || input.cr30 >= -2048) {
    input.colorArm = actor_prefix::ColorArm::Plain;
  } else {
    input.colorArm = actor_prefix::ColorArm::NegativeBlend;
  }
  const uint32_t colorCount = c->mem_r16(kseg(descriptor + 2u));
  const DescriptorMaterial material = descriptorMaterial(input.colorArm);
  const uint32_t directColors = c->mem_r32(kseg(descriptor + material.colorOffset));
  const uint32_t secondaryColors = c->mem_r32(kseg(descriptor + 32u));
  if (!physical_span(directColors, std::max(1u, colorCount) * 4u)) {
    return false;
  }
  input.primaryColors.reserve(colorCount);
  for (uint32_t i = 0; i < colorCount; ++i) {
    input.primaryColors.push_back(c->mem_r32(kseg(directColors + i * 4u)));
  }
  if (input.colorArm == actor_prefix::ColorArm::PositiveBlend) {
    if (!physical_span(secondaryColors, std::max(1u, colorCount) * 4u)) {
      return false;
    }
    input.secondaryColors.reserve(colorCount);
    for (uint32_t i = 0; i < colorCount; ++i) {
      input.secondaryColors.push_back(c->mem_r32(kseg(secondaryColors + i * 4u)));
    }
  }
  const uint32_t command = c->mem_r32(kseg(descriptor + material.commandOffset));
  uint32_t primitiveBytes = 0;
  if (!copy_primitive_words(
          [&](uint32_t address) {
            return c->mem_r32(address);
          },
          command,
          input.primitiveWords,
          primitiveBytes)) {
    return false;
  }
  if (input.cr30 > 0 &&
      !copy_primitive_patches(c, model, modelMeta, primitiveBytes, input.primitivePatches)) {
    return false;
  }
  capture.descriptor = kseg(descriptor);
  capture.command = kseg(command);
  capture.colorBase = material.writesScratchColors ? 0x80070DF4u : kseg(directColors);
  capture.expected = actor_prefix::build(input);
  capture.fog = capture.expected.fog;
  return true;
}

bool capture_secondary_source(Core *c, const SourceRecord &source, Record &capture) {
  if (!capture_source(c, source, capture)) {
    return false;
  }
  auto &input = capture.input;
  if (input.cr30 > 0) {
    return true;
  }
  // Both regular 0x8001F798 and secondary 0x80020F34 select descriptor
  // offsets +28/+32 when CR30 is non-positive. capture_source already owns
  // that direct material arm. Only the deeper global-far-colour program
  // remains distinct and unsupported here.
  if (input.colorArm == actor_prefix::ColorArm::NegativeBlend) {
    return false;
  }
  return true;
}

bool capture_record(Core *c, uint32_t record, Record &capture) {
  if (c->mem_r32(record) == 0) {
    return false;
  }
  SourceRecord source{};
  source.header = c->mem_r32(record);
  source.descriptor = c->mem_r32(record + 4u);
  source.model = c->mem_r32(record + 8u);
  source.alternate = c->mem_r32(record + 12u);
  source.tx = (int32_t)c->mem_r32(record + 16u);
  source.ty = (int32_t)c->mem_r32(record + 20u);
  source.tz = (int32_t)c->mem_r32(record + 24u);
  for (uint32_t i = 0; i < source.matrixWords.size(); ++i) {
    source.matrixWords[i] = c->mem_r32(record + 28u + i * 4u);
  }
  return capture_source(c, source, capture);
}

bool capture_records(Core *c, std::vector<Record> &records) {
  records.clear();
  records.reserve(kDurableRecords);
  for (uint32_t i = 0; i <= kTerminatorIndex; ++i) {
    const uint32_t address = kRecordBase + i * kRecordSize;
    if (c->mem_r32(address) == 0) {
      return !records.empty();
    }
    Record record{};
    if (i == kTerminatorIndex || !capture_record(c, address, record)) {
      records.clear();
      return false;
    }
    records.push_back(std::move(record));
  }
  records.clear();
  return false;
}

actor_draw_recipe::Recipe compose_records(std::span<const Record> records,
                                          std::vector<actor_prefix::Output> &outputs) {
  outputs.clear();
  outputs.reserve(records.size());
  for (const auto &record : records) {
    outputs.push_back(record.expected);
  }
  // The live census has already established ownership before this adapter is
  // called. An empty list therefore means a known-empty scene, while the
  // pure corpus API's empty span still means that no capture was supplied.
  auto recipe = actor_draw_recipe::compose(outputs);
  if (records.empty() && recipe.status == actor_draw_recipe::Status::NoCorpus) {
    recipe.status = actor_draw_recipe::Status::ValidEmpty;
  }
  return recipe;
}

} // namespace spyro::actor_recipe_capture
