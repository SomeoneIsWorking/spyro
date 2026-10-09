// The terrain drawer's classification pass (SCUS_944.25 80023C0C..800245CC): which sectors are
// drawn, and by which pass.

#include "guest_terrain_passes.h"

#include "core.h"
#include "gte_registers.h"
#include "guest_render_globals.h"
#include "guest_terrain_frame.h"

#include <array>

namespace spyro::guest_terrain {
namespace {

namespace gte = psx::gte;

constexpr std::uint32_t kGroupCopyEnd = 0xFu; // SCUS_944.25 800244FC: whole 16-byte rows

namespace sector {
constexpr std::uint32_t kCentreXY = 0x00; // x in the high halfword, y in the low, camera units / 16
constexpr std::uint32_t kCentreZ = 0x04;  // z in the high halfword; flags 0xF000; radius 0x0FFF
constexpr std::uint32_t kFlagsHalf = 0x04;   // the low halfword of kCentreZ, rewritten by animation
constexpr std::uint32_t kColourSkip = 0x10;  // u8: first colour word slot 1 animates
constexpr std::uint32_t kLayout = 0x14;      // packed offsets of the sector's data arrays
constexpr std::uint32_t kOverlaySkip = 0x17; // u8: first word slot 2 animates
constexpr std::uint32_t kAnimationMarks = 0x18; // four bytes, 0xFF once animated this frame
constexpr std::uint32_t kData = 0x1C;
} // namespace sector

constexpr std::uint32_t kRadiusMask = 0x0FFFu;
constexpr std::uint32_t kFlagMask = 0xF000u;
constexpr std::uint32_t kNoFarList = 0x2000u;   // 80023DDC
constexpr std::uint32_t kNearFar = 0x8000u;     // 80023DE8: listed far from 0x160, not 0x760
constexpr std::uint32_t kTranslucent = 0x5000u; // 80023E2C
constexpr std::uint32_t kNoNearList = 0x4000u;  // 80023E40

constexpr std::int32_t kFarListDepth = 0x760;
constexpr std::int32_t kNearFarListDepth = 0x160;
constexpr std::int32_t kDetailListDepth = 0x7C0;
constexpr std::int32_t kTranslucentListDepth = 0x600;
constexpr std::int32_t kCloseDepth = 0x100;

// List-entry bits, ORed into the 4-aligned sector pointer.
constexpr std::uint32_t kEdgeTouching = 1;
constexpr std::uint32_t kClose = 2;

// Which lists took the sector: the byte that replaces its scratchpad visibility byte.
constexpr std::uint32_t kTakenNear = 1;
constexpr std::uint32_t kTakenFar = 2;

constexpr std::int32_t asSigned(std::uint32_t value) {
  return static_cast<std::int32_t>(value);
}

constexpr std::uint32_t asWord(std::int32_t value) {
  return static_cast<std::uint32_t>(value);
}

namespace animation {
constexpr std::uint32_t kKeyIndex = 0x02; // u8: the current key
constexpr std::uint32_t kTarget = 0x05;   // u8: first word slot 0/2 animates
constexpr std::uint32_t kStride = 0x06;   // u16: bytes per frame
constexpr std::uint32_t kDataOffset = 0x08;
constexpr std::uint32_t kKeys = 0x0C; // 8 bytes per key; the frame word is at +4
} // namespace animation

// An INTPL packed vector: x in bits 21..31, y in 10..20, z in 0..9.
struct PackedVector {
  std::uint32_t x;
  std::uint32_t y;
  std::uint32_t z;
};

PackedVector unpack(std::uint32_t word) {
  return PackedVector{word >> 21, (word >> 10) & 0x7FFu, word & 0x3FFu};
}

class SectorClassifier {
public:
  explicit SectorClassifier(TerrainFrame &frame)
      : frame_(frame), core_(frame.core), memory_(frame.memory) {}

  void run();

private:
  void classify(std::uint32_t sector, std::uint32_t visibilityByte);
  void animateVectorSlot(std::uint32_t sector,
                         std::uint32_t slot,
                         std::uint32_t index,
                         std::uint32_t flagBit,
                         std::uint32_t target);
  void animateColourSlot(std::uint32_t sector, std::uint32_t slot, std::uint32_t index);
  void animatePairSlot(std::uint32_t sector, std::uint32_t slot, std::uint32_t index);
  std::uint32_t definition(std::uint32_t sector, std::uint32_t slot, std::uint32_t index);

  TerrainFrame &frame_;
  Core &core_;
  TerrainMemory &memory_;
  std::int32_t cameraX_ = 0;
  std::int32_t cameraY_ = 0;
  std::int32_t cameraZ_ = 0;
  std::uint32_t detailCursor_ = 0;
  std::uint32_t translucentCursor_ = 0;
  std::uint32_t farCursor_ = 0;
};

void SectorClassifier::run() {
  for (std::uint32_t i = 0; i < gte::kRotationWords; ++i) {
    gte_write_ctrl(gte::kRotation + i, frame_.classificationRotationWord(i));
  }
  gte_write_ctrl(gte::kTranslationX, 0);
  gte_write_ctrl(gte::kTranslationY, 0);
  gte_write_ctrl(gte::kTranslationZ, 0);
  cameraX_ = asSigned(frame_.positionWord(0)) >> 4;
  cameraY_ = asSigned(frame_.positionWord(1)) >> 4;
  cameraZ_ = asSigned(frame_.positionWord(2)) >> 4;
  const std::uint32_t count = core_.mem_r32(frame_.facts.classify.sectorCount);
  std::uint32_t slot = core_.mem_r32(frame_.facts.classify.sectorTable);
  const std::uint32_t end = kScratchpad + count;
  detailCursor_ = frame_.scratch + kDetailList;
  farCursor_ = frame_.scratch + kFarList;
  translucentCursor_ = frame_.scratch + kTranslucentList;

  for (std::uint32_t byte = kScratchpad; byte != end; ++byte) {
    slot += 4;
    if (memory_.r8(byte) != 0) {
      classify(memory_.r32(slot - 4), byte);
    }
  }
  memory_.w32(translucentCursor_, 0);
  memory_.w32(farCursor_, 0);
  memory_.w32(detailCursor_, 0);

  // 800244F0: the moby visibility walk (each moby's group byte at +0x52) is the guest's own, so a
  // real field must publish these bytes; an in-between field classifies from what it was handed.
  if (frame_.realField()) {
    const std::uint32_t copyEnd = (end + kGroupCopyEnd) & ~kGroupCopyEnd;
    std::uint32_t from = kScratchpad;
    std::uint32_t to = frame_.globals.visibilityGroups;
    do {
      for (std::uint32_t word = 0; word < 16; word += 4) {
        core_.mem_w32(to + word, memory_.r32(from + word));
      }
      from += 16;
      to += 16;
    } while (from != copyEnd);
  }
}

// 80023CB0..80023E88.
void SectorClassifier::classify(std::uint32_t sector, std::uint32_t visibilityByte) {
  memory_.w8(visibilityByte, 0);
  const std::uint32_t centreXY = core_.mem_r32(sector + sector::kCentreXY);
  const std::uint32_t centreZ = core_.mem_r32(sector + sector::kCentreZ);
  gte_write_data(gte::kIr3, asWord(asSigned(centreXY >> 16) - cameraX_));
  gte_write_data(gte::kIr1, asWord(cameraY_ - asSigned(centreXY & 0xFFFFu)));
  gte_write_data(gte::kIr2, asWord(cameraZ_ - asSigned(centreZ >> 16)));
  const std::uint32_t flags = centreZ & kFlagMask;
  const std::uint32_t r = centreZ & kRadiusMask;
  gte_op(&core_, gte::kMvmvaRtIr);
  // The margins are r/cos and r*tan of the 4:3 half-angle horizontally, the 17:32 slope vertically;
  // every product wraps as the image's 32-bit `add`/`sll` do.
  const std::uint32_t horizontalLateral = (r >> 1) + (r >> 2) + (r >> 5);
  const std::uint32_t horizontalDepth = (r >> 1) + (r >> 4) + (r >> 5);
  const std::uint32_t z = gte_read_data(gte::kMac3);
  std::uint32_t x = gte_read_data(gte::kMac1);
  if (asSigned(z + r) <= 0) {
    return;
  }
  if (asSigned(x) < 0) {
    x = 0u - x;
  }
  if (asSigned(((x - horizontalLateral) << 2) - (z + horizontalDepth) * 3) >= 0) {
    return;
  }
  const std::uint32_t verticalLateral = r - (r >> 3);
  const std::uint32_t verticalDepth = (r >> 1) - (r >> 4);
  std::uint32_t y = gte_read_data(gte::kMac2);
  if (asSigned(y) < 0) {
    y = 0u - y;
  }
  if (asSigned(((y - verticalLateral) << 5) - (z + verticalDepth) * 17) >= 0) {
    return;
  }
  std::uint32_t marks = core_.mem_r32(sector + sector::kAnimationMarks);
  std::uint32_t entry = sector | kEdgeTouching;
  if (asSigned(z - r) > 0 &&
      asSigned(((x + horizontalLateral) << 2) - (z - horizontalDepth) * 3) < 0 &&
      asSigned(((y + verticalLateral) << 5) - (z - verticalDepth) * 17) < 0) {
    entry ^= kEdgeTouching;
  }

  // 80023DD4: which lists take it, and which halves of the animation marks it may claim.
  std::uint32_t claim = 0xFFFFFFFFu;
  std::uint32_t taken = 0;
  if ((flags & kNoFarList) != 0) {
    claim = 0xFFFF0000u;
  } else {
    const std::int32_t farDepth = (flags & kNearFar) != 0 ? kNearFarListDepth : kFarListDepth;
    if (asSigned(z + r - asWord(farDepth)) > 0) {
      const std::uint32_t farEntry =
          asSigned(z - r - asWord(kFarListDepth)) > 0 ? entry : entry | kClose;
      memory_.w32(farCursor_, farEntry);
      farCursor_ += 4;
      taken |= kTakenFar;
      claim = 0xFFFF0000u;
    }
  }
  const std::uint32_t nearSide = z - r;
  if (asSigned(nearSide - asWord(kDetailListDepth)) < 0) {
    if ((flags & kTranslucent) == 0) {
      if (asSigned(nearSide - asWord(kCloseDepth)) < 0) {
        entry += kClose;
      }
      memory_.w32(detailCursor_, entry);
      detailCursor_ += 4;
      taken |= kTakenNear;
      claim &= 0xFFFFu;
    } else if (asSigned(nearSide - asWord(kTranslucentListDepth)) < 0) {
      if ((flags & kNoNearList) != 0) {
        claim &= 0xFFFFu;
      } else {
        if (asSigned(nearSide - asWord(kCloseDepth)) < 0) {
          entry += kClose;
        }
        translucentCursor_ += 4;
        memory_.w32(translucentCursor_ - 4, entry);
        taken |= kTakenNear;
      }
    }
  }
  marks |= claim;
  memory_.w8(visibilityByte, static_cast<std::uint8_t>(taken));
  // 80023E8C..80024314: each slot whose mark byte is still clear animates once, and each of the
  // four writes a word of the level's own sector data, so an in-between field must not advance the
  // animation a second time inside one field.
  if (!frame_.realField() || marks == 0xFFFFFFFFu) {
    return;
  }
  const auto index = [marks](std::uint32_t slot) {
    return (marks >> (8 * slot)) & 0xFFu;
  };
  if (index(0) < 0x80) {
    animateVectorSlot(sector, 0, index(0), 0x2000u, sector + sector::kData);
  }
  if (index(1) < 0x80) {
    animateColourSlot(sector, 1, index(1));
  }
  if (index(2) < 0x80) {
    animateVectorSlot(sector,
                      2,
                      index(2),
                      0x4000u,
                      sector + sector::kData + core_.mem_r8(sector + sector::kOverlaySkip) * 4);
  }
  if (index(3) < 0x80) {
    animatePairSlot(sector, 3, index(3));
  }
}

// Claims the slot's mark byte, then returns the slot's definition.
std::uint32_t
SectorClassifier::definition(std::uint32_t sector, std::uint32_t slot, std::uint32_t index) {
  core_.mem_w8(sector + sector::kAnimationMarks + slot, 0xFF);
  return core_.mem_r32(core_.mem_r32(frame_.facts.classify.animationTables[slot]) + index * 4);
}

// Slots 0 and 2 (80023E98, 80024174). The key's top byte also sets or clears the slot's sector
// flag.
void SectorClassifier::animateVectorSlot(std::uint32_t sector,
                                         std::uint32_t slot,
                                         std::uint32_t index,
                                         std::uint32_t flagBit,
                                         std::uint32_t target) {
  const std::uint32_t def = definition(sector, slot, index);
  const std::uint32_t key =
      core_.mem_r32(def + animation::kKeys + core_.mem_r8(def + animation::kKeyIndex) * 8 + 4);
  std::uint32_t flagsHalf = core_.mem_r16(sector + sector::kFlagsHalf) & ~flagBit & 0xFFFFu;
  if ((key >> 24) != 0) {
    flagsHalf |= flagBit;
  }
  core_.mem_w16(sector + sector::kFlagsHalf, static_cast<std::uint16_t>(flagsHalf));
  const std::uint32_t fraction = key & 0xFFu;
  const std::uint32_t stride = core_.mem_r16(def + animation::kStride);
  std::uint32_t to = target + core_.mem_r8(def + animation::kTarget) * 4;
  const std::uint32_t data = def + core_.mem_r32(def + animation::kDataOffset);
  std::uint32_t from = data + ((key >> 8) & 0xFFu) * stride;
  if (fraction == 0) {
    const std::uint32_t end = from + stride;
    do {
      core_.mem_w32(to, core_.mem_r32(from));
      from += 4;
      to += 4;
    } while (from != end);
    return;
  }
  gte_write_data(gte::kIr0, fraction << 4);
  std::uint32_t toward = data + ((key >> 16) & 0xFFu) * stride;
  const std::uint32_t end = from + stride + 4;
  std::uint32_t near = core_.mem_r32(from);
  from += 4;
  std::uint32_t far = core_.mem_r32(toward);
  toward += 4;
  PackedVector v = unpack(near);
  gte_write_data(gte::kIr1, v.x);
  gte_write_data(gte::kIr2, v.y);
  gte_write_data(gte::kIr3, v.z);
  do {
    const PackedVector f = unpack(far);
    gte_write_ctrl(gte::kFarRed, f.x);
    gte_write_ctrl(gte::kFarGreen, f.y);
    gte_write_ctrl(gte::kFarBlue, f.z);
    near = core_.mem_r32(from);
    far = core_.mem_r32(toward);
    gte_op(&core_, gte::kIntpl);
    to += 4;
    from += 4;
    toward += 4;
    v = unpack(near);
    gte_write_data(gte::kIr1, v.x);
    gte_write_data(gte::kIr2, v.y);
    gte_write_data(gte::kIr3, v.z);
    const std::uint32_t packed = (gte_read_data(gte::kMac1) << 21) +
                                 (gte_read_data(gte::kMac2) << 10) + gte_read_data(gte::kMac3);
    core_.mem_w32(to - 4, packed);
  } while (end != from);
}

void loadFarColour(std::uint32_t colour) {
  gte_write_ctrl(gte::kFarRed, (colour << 4) & 0xFF0u);
  gte_write_ctrl(gte::kFarGreen, (colour >> 4) & 0xFF0u);
  gte_write_ctrl(gte::kFarBlue, (colour >> 12) & 0xFF0u);
}

// Slot 1 (8002402C): colour words, each placed by the 8-bit skip in its own top byte.
void SectorClassifier::animateColourSlot(std::uint32_t sector,
                                         std::uint32_t slot,
                                         std::uint32_t index) {
  const std::uint32_t def = definition(sector, slot, index);
  const std::uint32_t key = def + animation::kKeys + core_.mem_r8(def + animation::kKeyIndex) * 8;
  const std::uint32_t fraction = core_.mem_r8(key + 4);
  const std::uint32_t stride = core_.mem_r16(def + animation::kStride);
  const std::uint32_t data = def + core_.mem_r32(def + animation::kDataOffset);
  std::uint32_t from = data + core_.mem_r8(key + 5) * stride;
  const std::uint32_t end = from + stride;
  std::uint32_t to = sector + sector::kData + core_.mem_r8(sector + sector::kColourSkip) * 4;
  if (fraction == 0) {
    do {
      const std::uint32_t word = core_.mem_r32(from);
      from += 4;
      to += (word >> 22) & 0x3FCu;
      core_.mem_w32(to, word & 0xFFFFFFu);
    } while (from != end);
    return;
  }
  std::uint32_t toward = data + core_.mem_r8(key + 6) * stride;
  gte_write_data(gte::kIr0, fraction << 4);
  std::uint32_t near = core_.mem_r32(from);
  std::uint32_t far = core_.mem_r32(toward);
  do {
    gte_write_data(gte::kRgbc, near & 0xFFFFFFu);
    loadFarColour(far);
    const std::uint32_t skip = (near >> 22) & 0x3FCu;
    gte_op(&core_, gte::kDpcs);
    near = core_.mem_r32(from + 4);
    far = core_.mem_r32(toward + 4);
    from += 4;
    toward += 4;
    to += skip;
    core_.mem_w32(to, gte_read_data(gte::kRgb2));
  } while (end != from);
}

// Slot 3 (8002431C): colour pairs placed into two arrays the sector's layout word locates.
void SectorClassifier::animatePairSlot(std::uint32_t sector,
                                       std::uint32_t slot,
                                       std::uint32_t index) {
  const std::uint32_t def = definition(sector, slot, index);
  const std::uint32_t key = def + animation::kKeys + core_.mem_r8(def + animation::kKeyIndex) * 8;
  const std::uint32_t fraction = core_.mem_r8(key + 4);
  const std::uint32_t stride = core_.mem_r16(def + animation::kStride);
  const std::uint32_t data = def + core_.mem_r32(def + animation::kDataOffset);
  std::uint32_t from = data + core_.mem_r8(key + 5) * stride;
  const std::uint32_t end = from + stride;
  const std::uint32_t layout = core_.mem_r32(sector + sector::kLayout);
  std::uint32_t first =
      sector + sector::kData + ((layout >> 22) & 0x3FCu) + ((layout << 2) & 0x3FCu);
  std::uint32_t second = first + ((layout >> 6) & 0x3FCu);
  if (fraction == 0) {
    do {
      const std::uint32_t a = core_.mem_r32(from);
      const std::uint32_t b = core_.mem_r32(from + 4);
      from += 8;
      const std::uint32_t skip = (a >> 22) & 0x3FCu;
      first += skip;
      second += skip;
      core_.mem_w32(first, a & 0xFFFFFFu);
      core_.mem_w32(second, b);
    } while (from != end);
    return;
  }
  std::uint32_t toward = data + core_.mem_r8(key + 6) * stride;
  gte_write_data(gte::kIr0, fraction << 4);
  std::uint32_t near = core_.mem_r32(from);
  std::uint32_t far = core_.mem_r32(toward);
  do {
    gte_write_data(gte::kRgbc, near & 0xFFFFFFu);
    loadFarColour(far);
    const std::uint32_t skip = (near >> 22) & 0x3FCu;
    gte_op(&core_, gte::kDpcs);
    near = core_.mem_r32(from + 4);
    far = core_.mem_r32(toward + 4);
    first += skip;
    core_.mem_w32(first, gte_read_data(gte::kRgb2));
    gte_write_data(gte::kRgbc, near);
    loadFarColour(far);
    from += 8;
    toward += 8;
    gte_op(&core_, gte::kDpcs);
    near = core_.mem_r32(from);
    far = core_.mem_r32(toward);
    second += skip;
    core_.mem_w32(second, gte_read_data(gte::kRgb2));
  } while (from != end);
}

} // namespace

void classifySectors(TerrainFrame &frame) {
  SectorClassifier(frame).run();
}

} // namespace spyro::guest_terrain
