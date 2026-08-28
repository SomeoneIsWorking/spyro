#pragma once

#include "actor_draw_recipe.h"

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

class Core;

namespace spyro::actor_recipe_capture {

constexpr uint32_t kRecordBase = 0x800712F4u;
constexpr uint32_t kRecordSize = 56u;
constexpr uint32_t kDurableRecords = 53u;
constexpr uint32_t kTerminatorIndex = 53u;

struct Record {
  actor_prefix::Input input;
  actor_prefix::Output expected;
  uint32_t descriptor = 0;
  uint32_t command = 0;
  uint32_t colorBase = 0;
  uint32_t fog = 0;
  bool colorSeen = false;
};

struct SourceRecord {
  uint32_t header = 0;
  uint32_t descriptor = 0;
  uint32_t model = 0;
  uint32_t alternate = 0;
  int32_t tx = 0;
  int32_t ty = 0;
  int32_t tz = 0;
  std::array<uint32_t, 5> matrixWords{};
};

struct DescriptorMaterial {
  uint32_t commandOffset = 20u;
  uint32_t colorOffset = 24u;
  bool writesScratchColors = false;
};

// Exact descriptor branch in regular renderer 0x8001F798 after vertex
// projection. Non-positive CR30 selects the secondary command/colour pair;
// only the two depth-cued arms materialize colours in scratch.
constexpr DescriptorMaterial descriptorMaterial(actor_prefix::ColorArm arm) {
  switch (arm) {
  case actor_prefix::ColorArm::High:
    return {};
  case actor_prefix::ColorArm::PositiveBlend:
    return {.commandOffset = 20u, .colorOffset = 24u, .writesScratchColors = true};
  case actor_prefix::ColorArm::Plain:
    return {.commandOffset = 28u, .colorOffset = 32u, .writesScratchColors = false};
  case actor_prefix::ColorArm::NegativeBlend:
    return {.commandOffset = 28u, .colorOffset = 32u, .writesScratchColors = true};
  }
  return {};
}

constexpr bool physical_span(uint32_t address, uint32_t bytes) {
  const uint32_t physical = address & 0x1fffffffu;
  return (address & 3u) == 0u && bytes >= 4u && bytes <= 0x200000u && physical <= 0x200000u - bytes;
}

constexpr uint32_t kseg(uint32_t address) {
  return 0x80000000u | (address & 0x1fffffu);
}

// Shared defensive stream copier used by both the live capture and its malformed-input tests.
template <class Read>
bool copy_primitive_words(Read read,
                          uint32_t command,
                          std::vector<uint32_t> &words,
                          uint32_t &bytes) {
  words.clear();
  bytes = 0;
  if (!physical_span(command, 4u)) {
    return false;
  }
  bytes = read(kseg(command));
  if ((bytes & 3u) != 0u || bytes > 0x1ffffcu || !physical_span(command, bytes + 4u)) {
    return false;
  }
  words.reserve(bytes / 4u);
  for (uint32_t offset = 4; offset < bytes + 4u; offset += 4u) {
    words.push_back(read(kseg(command + offset)));
  }
  return true;
}

bool capture_record(Core *c, uint32_t record, Record &capture);
bool capture_source(Core *c, const SourceRecord &source, Record &capture);

// 0x80020F34 switches to the descriptor's secondary command/material stream
// when CR30 is non-positive. This preserves the shared geometry capture while
// keeping that renderer-specific material ownership out of the regular actor
// path. Returns false for the distinct far-colour arm that is not yet owned.
bool capture_secondary_source(Core *c, const SourceRecord &source, Record &capture);
bool capture_records(Core *c, std::vector<Record> &records);

actor_draw_recipe::Recipe compose_records(std::span<const Record> records,
                                          std::vector<actor_prefix::Output> &outputs);

} // namespace spyro::actor_recipe_capture
