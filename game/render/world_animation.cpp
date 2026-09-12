#include "world_animation.h"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace spyro::world_animation {
namespace {

using world_chunk_codec::RamView;

constexpr uint32_t kEnvironmentAnimations = 0x80078560u; // g_EnvironmentAnimations (C206)
constexpr uint32_t kIdle = 0xffu;                        // the guest stamps a finished channel -1
constexpr uint32_t kColorMask = 0x00ffffffu;

// The channel's animation-set pointer lives at g_EnvironmentAnimations+0x14 and every 8 bytes
// after; the guest hard-codes one load per channel (0x80025BD0, 0x80025D24, 0x80025E6C,
// 0x80025FE0).
uint32_t channelSet(uint32_t channel) {
  return kEnvironmentAnimations + 20u + channel * 8u;
}

bool addAddress(uint32_t base, uint64_t offset, uint32_t &out) {
  if (offset > (uint64_t)std::numeric_limits<uint32_t>::max() - base) {
    return false;
  }
  out = base + (uint32_t)offset;
  return true;
}

bool retainResource(const RamView &ram,
                    uint32_t address,
                    uint32_t size,
                    Plan &plan,
                    const char *reason,
                    const char *&why) {
  const auto range = ram.range(address, size);
  if (!range) {
    why = reason;
    return false;
  }
  const auto duplicate =
      std::find_if(plan.resources.begin(), plan.resources.end(), [&](const auto &item) {
        return item.begin == range->begin && item.end == range->end;
      });
  if (duplicate == plan.resources.end()) {
    plan.resources.push_back(*range);
  }
  return true;
}

struct Header {
  uint32_t sourceA = 0;
  uint32_t sourceB = 0;
  uint32_t size = 0;
  int32_t factor = 0; // 0 selects the straight-copy form
};

bool readHeader(const RamView &ram,
                uint32_t channel,
                uint32_t index,
                Header &out,
                Plan &plan,
                const char *&why) {
  const uint32_t setSlot = channelSet(channel);
  if (!retainResource(ram, setSlot, 4u, plan, "animation_set_slot", why)) {
    return false;
  }
  const uint32_t set = ram.r32(setSlot);
  uint32_t tableEntry = 0;
  if (!addAddress(set, (uint64_t)index * 4u, tableEntry) ||
      !retainResource(ram, tableEntry, 4u, plan, "animation_table", why)) {
    why = "animation_table";
    return false;
  }
  const uint32_t animation = ram.r32(tableEntry);
  if (!retainResource(ram, animation, 12u, plan, "animation_header", why)) {
    return false;
  }
  uint32_t keyframe = 0;
  if (!addAddress(animation, 12u + (uint64_t)ram.r8(animation + 2u) * 8u, keyframe) ||
      !retainResource(ram, keyframe, 8u, plan, "animation_keyframe", why)) {
    return false;
  }
  out.factor = (int32_t)(uint32_t)ram.r8(keyframe + 4u);
  out.size = ram.r16(animation + 6u);
  uint32_t base = 0;
  if (!addAddress(animation, ram.r32(animation + 8u), base) ||
      !addAddress(base, (uint64_t)ram.r8(keyframe + 5u) * out.size, out.sourceA) ||
      !addAddress(base, (uint64_t)ram.r8(keyframe + 6u) * out.size, out.sourceB)) {
    why = "animation_payload";
    return false;
  }
  return true;
}

// Every channel walks its payload with a `bne` against a computed end pointer, so a size that is
// not a whole number of elements does not terminate in the guest either. That is a broken model of
// the data, not a case to tolerate quietly.
bool checkStride(const Header &header, uint32_t stride, const char *&why) {
  if (header.size == 0u || (header.size % stride) != 0u) {
    why = "animation_stride";
    return false;
  }
  return true;
}

bool readable(const RamView &ram, const Header &header, bool blended, const char *&why) {
  if (!ram.contains(header.sourceA, header.size) ||
      (blended && !ram.contains(header.sourceB, header.size))) {
    why = "animation_payload";
    return false;
  }
  return true;
}

Vector3 unpackVertex(uint32_t word) {
  return {(int32_t)(word >> 21), (int32_t)((word >> 10) & 0x7ffu), (int32_t)(word & 0x3ffu)};
}

uint32_t packVertex(const Vector3 &v) {
  return ((uint32_t)v.x << 21) + ((uint32_t)v.y << 10) + (uint32_t)v.z;
}

// The colour channels feed the far-colour registers from a source word's three bytes, each scaled
// into the GTE's 12.4 colour space exactly as the guest's shift/mask pairs do.
Vector3 unpackFarColor(uint32_t word) {
  return {(int32_t)((word << 4) & 0xff0u),
          (int32_t)((word >> 4) & 0xff0u),
          (int32_t)((word >> 12) & 0xff0u)};
}

void emit(Plan &plan, uint32_t address, uint32_t value) {
  plan.writes.push_back({address, value, 4u});
}

} // namespace

// i32_to_i16_saturate with lm=0, and the i44 truncation applied to the intermediate, implemented
// from the same GTE reference this port already vendors (beetle-psx mednafen/psx/gte.c).
namespace {

int64_t truncate44(int64_t value) {
  return (int64_t)((uint64_t)value << (64 - 44)) >> (64 - 44);
}

int32_t saturate16(int32_t value) {
  if (value < -32768) {
    return -32768;
  }
  if (value > 32767) {
    return 32767;
  }
  return value;
}

uint32_t clampByte(int32_t value) {
  if (value < 0) {
    return 0u;
  }
  if (value > 255) {
    return 255u;
  }
  return (uint32_t)value;
}

} // namespace

Vector3 intpl(Vector3 ir, Vector3 farColor, int32_t ir0) {
  const int32_t irv[3] = {ir.x, ir.y, ir.z};
  const int32_t fc[3] = {farColor.x, farColor.y, farColor.z};
  int32_t mac[3] = {0, 0, 0};
  for (int i = 0; i < 3; ++i) {
    mac[i] = (int32_t)(truncate44(((int64_t)fc[i] << 12) - ((int64_t)irv[i] << 12)) >> 12);
    mac[i] =
        (int32_t)(truncate44(((int64_t)irv[i] << 12) + (int64_t)ir0 * saturate16(mac[i])) >> 12);
  }
  return {mac[0], mac[1], mac[2]};
}

uint32_t dpcs(uint32_t rgb, Vector3 farColor, int32_t ir0) {
  const int32_t channel[3] = {(int32_t)((rgb >> 0) & 0xffu) << 4,
                              (int32_t)((rgb >> 8) & 0xffu) << 4,
                              (int32_t)((rgb >> 16) & 0xffu) << 4};
  const int32_t fc[3] = {farColor.x, farColor.y, farColor.z};
  int32_t mac[3] = {0, 0, 0};
  for (int i = 0; i < 3; ++i) {
    mac[i] = (int32_t)(truncate44(((int64_t)fc[i] << 12) - ((int64_t)channel[i] << 12)) >> 12);
    mac[i] =
        (int32_t)(truncate44(((int64_t)channel[i] << 12) + (int64_t)ir0 * saturate16(mac[i])) >>
                  12);
  }
  // MAC_to_RGB_FIFO: each accumulator drops its four fractional bits and clamps to a byte; the
  // source word's code byte rides through untouched, which is why channel 3's second stream feeds
  // its word unmasked.
  return clampByte(mac[0] >> 4) | (clampByte(mac[1] >> 4) << 8) | (clampByte(mac[2] >> 4) << 16) |
         (rgb & 0xff000000u);
}

namespace {

// Channel 0 / 2 — packed 11/11/10 vertices, copied straight or interpolated between two keyframes.
void appendVertices(const RamView &ram, const Header &header, uint32_t destination, Plan &plan) {
  const uint32_t count = header.size / 4u;
  if (header.factor == 0) {
    for (uint32_t i = 0; i < count; ++i) {
      emit(plan, destination + i * 4u, ram.r32(header.sourceA + i * 4u));
    }
    plan.direct++;
    return;
  }
  const int32_t ir0 = header.factor << 4;
  for (uint32_t i = 0; i < count; ++i) {
    const Vector3 from = unpackVertex(ram.r32(header.sourceA + i * 4u));
    const Vector3 to = unpackVertex(ram.r32(header.sourceB + i * 4u));
    emit(plan, destination + i * 4u, packVertex(intpl(from, to, ir0)));
  }
  plan.blended++;
}

// Channel 1 — colours addressed by a running delta carried in each source word's top BYTE; the
// guest's `>>22 & 0x3FC` shifts bits 24..31 down and scales them to a word offset in one step.
void appendColors(const RamView &ram, const Header &header, uint32_t destination, Plan &plan) {
  const uint32_t count = header.size / 4u;
  const int32_t ir0 = header.factor << 4;
  uint32_t cursor = destination;
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t word = ram.r32(header.sourceA + i * 4u);
    cursor += (word >> 22) & 0x3fcu;
    if (header.factor == 0) {
      emit(plan, cursor, word & kColorMask);
      continue;
    }
    emit(plan,
         cursor,
         dpcs(word & kColorMask, unpackFarColor(ram.r32(header.sourceB + i * 4u)), ir0));
  }
  (header.factor == 0 ? plan.direct : plan.blended)++;
}

// Channel 3 — the same delta walk, but each source pair feeds two destination streams whose start
// offsets come from the sector's own layout word. The second stream keeps its source word's code
// byte, so it is written unmasked.
void appendPairedColors(
    const RamView &ram, const Header &header, uint32_t first, uint32_t second, Plan &plan) {
  const uint32_t count = header.size / 8u;
  const int32_t ir0 = header.factor << 4;
  uint32_t cursorA = first;
  uint32_t cursorB = second;
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t wordA = ram.r32(header.sourceA + i * 8u);
    const uint32_t wordB = ram.r32(header.sourceA + i * 8u + 4u);
    const uint32_t delta = (wordA >> 22) & 0x3fcu;
    cursorA += delta;
    cursorB += delta;
    if (header.factor == 0) {
      emit(plan, cursorA, wordA & kColorMask);
      emit(plan, cursorB, wordB);
      continue;
    }
    const Vector3 farA = unpackFarColor(ram.r32(header.sourceB + i * 8u));
    const Vector3 farB = unpackFarColor(ram.r32(header.sourceB + i * 8u + 4u));
    emit(plan, cursorA, dpcs(wordA & kColorMask, farA, ir0));
    emit(plan, cursorB, dpcs(wordB, farB, ir0));
  }
  (header.factor == 0 ? plan.direct : plan.blended)++;
}

} // namespace

bool appendSector(
    const RamView &ram, uint32_t sector, uint32_t active, Plan &plan, const char *&why) {
  if (!ram.contains(sector, 28u)) {
    why = "animation_sector_bounds";
    return false;
  }
  for (uint32_t channel = 0; channel < 4u; ++channel) {
    const uint32_t index = (active >> (channel * 8u)) & 0xffu;
    if (index >= 0x80u) {
      continue;
    }
    Header header{};
    if (!readHeader(ram, channel, index, header, plan, why)) {
      return false;
    }
    const uint32_t stride = channel == 3u ? 8u : 4u;
    if (!checkStride(header, stride, why) || !readable(ram, header, header.factor != 0, why)) {
      return false;
    }
    if (!retainResource(ram, header.sourceA, header.size, plan, "animation_payload", why) ||
        (header.factor != 0 &&
         !retainResource(ram, header.sourceB, header.size, plan, "animation_payload", why))) {
      return false;
    }
    switch (channel) {
    case 0:
      appendVertices(ram, header, sector + 28u, plan);
      break;
    case 1:
      appendColors(ram, header, sector + 28u + (uint32_t)ram.r8(sector + 16u) * 4u, plan);
      break;
    case 2:
      appendVertices(ram, header, sector + 28u + (uint32_t)ram.r8(sector + 23u) * 4u, plan);
      break;
    default: {
      const uint32_t layout = ram.r32(sector + 20u);
      const uint32_t first = sector + 28u + ((layout >> 22) & 0x3fcu) + ((layout << 2) & 0x3fcu);
      appendPairedColors(ram, header, first, first + ((layout >> 6) & 0x3fcu), plan);
      break;
    }
    }
    // The guest retires the channel by stamping its byte back to -1, which is what stops the same
    // frame being re-applied on every later pass over this sector.
    plan.writes.push_back({sector + 24u + channel, kIdle, 1u});
    plan.channels++;
  }
  return true;
}

} // namespace spyro::world_animation
