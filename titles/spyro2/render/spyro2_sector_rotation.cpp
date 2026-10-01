#include "spyro2_sector_rotation.h"

#include "core.h"
#include "spyro2_sector_gte.h"

namespace spyro2::sector_rotation {
namespace {

namespace gte = sector_gte;

constexpr std::uint32_t kLow = 0x0000FFFFu;
constexpr std::uint32_t kHigh = 0xFFFF0000u;

// The three axes, in the order retail applies them: rotation-word byte 2, byte 1, byte 0.
enum class Axis : std::uint8_t {
  Byte2,
  Byte1,
  Byte0,
};

// One `RT * V0` product, as the IR registers hold it (mfc2 sign-extends the 16-bit values).
struct Product {
  std::uint32_t ir1 = 0;
  std::uint32_t ir2 = 0;
  std::uint32_t ir3 = 0;
};

Product rotateV0(Core &core, std::uint32_t vxy0, std::uint32_t vz0) {
  gte_write_data(gte::kVxy0, vxy0);
  gte_write_data(gte::kVz0, vz0);
  gte_op(&core, gte::kRotateV0);
  return Product{gte_read_data(gte::kIr1), gte_read_data(gte::kIr2), gte_read_data(gte::kIr3)};
}

void load(const RotationWords &rotation) {
  for (std::uint32_t i = 0; i < gte::kRotationWords; ++i) {
    gte_write_ctrl(gte::kRotation0 + i, rotation[i]);
  }
}

// Each axis feeds two V0 vectors built from (sin, cos) and repacks the two products into the RT
// words that axis touches. The vectors and the repacking are retail's, word for word:
//   byte 2  80043BAC..80043C34 / 800441D8..800442B0
//   byte 1  80043C44..80043CD4 / 800442D0..800443B0
//   byte 0  80043CE4..80043D7C / 800443CC..800444B0
void rotate(Core &core, Axis axis, AngleWords angle, RotationWords &m) {
  const std::uint32_t s = angle.sine;
  const std::uint32_t c = angle.cosine;
  switch (axis) {
  case Axis::Byte2: {
    const Product a = rotateV0(core, c, s);
    const Product b = rotateV0(core, (0u - s) & kLow, c);
    m[0] = (m[0] & kHigh) + (a.ir1 & kLow);
    m[3] = (m[3] & kHigh) + (a.ir3 & kLow);
    m[1] = (b.ir1 & kLow) + (a.ir2 << 16);
    m[2] = (m[2] & kLow) + (b.ir2 << 16);
    m[4] = b.ir3 & kLow;
    break;
  }
  case Axis::Byte1: {
    const Product a = rotateV0(core, c << 16, s);
    const Product b = rotateV0(core, 0u - (s << 16), c);
    m[0] = (m[0] & kLow) + (a.ir1 << 16);
    m[3] = (m[3] & kLow) + (a.ir3 << 16);
    m[1] = (m[1] & kHigh) + (b.ir1 & kLow);
    m[2] = (b.ir2 << 16) + (a.ir2 & kLow);
    m[4] = b.ir3 & kLow;
    break;
  }
  case Axis::Byte0: {
    // VZ0 is cleared first in retail; the two writes touch different registers.
    const Product a = rotateV0(core, (s << 16) + c, 0);
    const Product b = rotateV0(core, ((0u - s) & kLow) + (c << 16), 0);
    m[1] = (m[1] & kLow) + (a.ir2 << 16);
    m[2] = (m[2] & kHigh) + (b.ir2 & kLow);
    m[0] = (b.ir1 << 16) + (a.ir1 & kLow);
    m[3] = (b.ir3 << 16) + (a.ir3 & kLow);
    break;
  }
  }
  load(m);
}

struct Channel {
  Axis axis;
  std::uint32_t angleShift;    // the angle byte is (angleWord >> angleShift) & 0xFF
  std::uint32_t fractionShift; // its fraction is (flags >> fractionShift) & 0xF
};

inline constexpr std::array<Channel, 3> kChannels = {{
    {Axis::Byte2, 16, 16},
    {Axis::Byte1, 8, 8},
    {Axis::Byte0, 0, 0},
}};

std::uint32_t tableEntry(std::uint32_t angle) {
  return kSineTable + angle * 2;
}

// `lh` the entry and, when the fraction is non-zero, step toward the next one by fraction/16.
std::int32_t interpolate(Core &core, std::uint32_t address, std::uint32_t fraction) {
  std::int32_t value = core.mem_r16s(address);
  if (fraction != 0) {
    const std::int32_t next = core.mem_r16s(address + 2);
    value += (static_cast<std::int32_t>(fraction) * (next - value)) >> 4;
  }
  return value;
}

} // namespace

void composeTabled(Core &core, std::uint32_t angleWord, RotationWords &rotation) {
  for (const Channel &channel : kChannels) {
    const std::uint32_t angle = (angleWord >> channel.angleShift) & 0xFFu;
    if (angle == 0) {
      continue;
    }
    const std::uint32_t entry = tableEntry(angle);
    rotate(core,
           channel.axis,
           AngleWords{core.mem_r16(entry), core.mem_r16(entry + kCosineOffset)},
           rotation);
  }
}

void composeInterpolated(Core &core,
                         std::uint32_t angleWord,
                         std::uint32_t flags,
                         RotationWords &rotation) {
  for (const Channel &channel : kChannels) {
    const std::uint32_t angle = (angleWord >> channel.angleShift) & 0xFFu;
    const std::uint32_t fraction = (flags >> channel.fractionShift) & 0xFu;
    if (angle == 0 && fraction == 0) {
      continue;
    }
    const std::uint32_t entry = tableEntry(angle);
    const std::int32_t sine = interpolate(core, entry, fraction);
    const std::int32_t cosine = interpolate(core, entry + kCosineOffset, fraction);
    rotate(core,
           channel.axis,
           AngleWords{static_cast<std::uint32_t>(sine), static_cast<std::uint32_t>(cosine)},
           rotation);
  }
}

void mirror(RotationWords &rotation) {
  rotation[0] = (0u - (rotation[0] & kHigh)) | (rotation[0] & kLow);
  rotation[2] = (rotation[2] & kHigh) | ((0u - (rotation[2] & kLow)) & kLow);
  rotation[3] = (0u - (rotation[3] & kHigh)) | (rotation[3] & kLow);
}

} // namespace spyro2::sector_rotation
