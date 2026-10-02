#include "guest_terrain_memory.h"

#include "core.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include <lucent/log.h>

namespace spyro::guest_terrain {
namespace {

// Reading an address this accessor does not own is NORMAL and expected: the passes read the level's
// sector records, polygon records and static tables straight out of the guest's RAM. Only the FIRST
// such read is logged, so a frame that legitimately reads ten thousand guest words does not print
// ten thousand lines; the count is available for a diagnostic.
constexpr const char *kChannel = "terrmem";

} // namespace

void HostMemory::map(std::uint32_t base, std::uint32_t bytes) {
  if (bytes == 0) {
    return;
  }
  const std::uint32_t end = base + bytes;
  // The union of this range and every segment it covers or touches. Two of the frame's ranges DO
  // overlap -- the primitive arena's top is inside the scratch block -- so treating them as
  // separate objects would make one of the two lose the other's writes.
  std::uint32_t mergedBase = base;
  std::uint32_t mergedEnd = end;
  for (const Segment &segment : segments_) {
    const std::uint32_t segmentEnd =
        segment.base + static_cast<std::uint32_t>(segment.bytes.size());
    if (segmentEnd < mergedBase || mergedEnd < segment.base) {
      continue;
    }
    mergedBase = std::min(mergedBase, segment.base);
    mergedEnd = std::max(mergedEnd, segmentEnd);
  }
  std::vector<std::uint8_t> merged(mergedEnd - mergedBase, 0);

  std::vector<Segment> kept;
  std::vector<Range> absorbed;
  kept.reserve(segments_.size() + 1);
  for (Segment &segment : segments_) {
    const std::uint32_t segmentEnd =
        segment.base + static_cast<std::uint32_t>(segment.bytes.size());
    if (segmentEnd < mergedBase || mergedEnd < segment.base) {
      kept.push_back(std::move(segment));
      continue;
    }
    // An absorbed segment keeps the bytes it already held: growing a mapping must not lose a pass's
    // writes, and a fresh mapping must still hold the level data the passes read out of it.
    const std::uint32_t lo = std::max(segment.base, mergedBase);
    const std::uint32_t hi = std::min(segmentEnd, mergedEnd);
    std::memcpy(
        merged.data() + (lo - mergedBase), segment.bytes.data() + (lo - segment.base), hi - lo);
    absorbed.push_back(Range{segment.base, segmentEnd - segment.base});
  }
  // Whatever the new range reaches that no absorbed segment covered starts as the guest's own
  // bytes: the passes read the level's records out of these ranges as well as writing their own.
  for (std::uint32_t address = base; address < end; ++address) {
    const bool covered = std::any_of(absorbed.begin(), absorbed.end(), [address](Range range) {
      return address >= range.base && address < range.base + range.bytes;
    });
    if (!covered) {
      merged[address - mergedBase] = guestByte(address);
    }
  }
  kept.push_back(Segment{mergedBase, std::move(merged)});
  std::sort(kept.begin(), kept.end(), [](const Segment &a, const Segment &b) {
    return a.base < b.base;
  });
  segments_ = std::move(kept);
}

void HostMemory::clear() {
  segments_.clear();
  guestReads_ = 0;
  refusedWrites_ = 0;
}

const HostMemory::Segment *HostMemory::find(std::uint32_t address) const {
  for (const Segment &segment : segments_) {
    if (address >= segment.base &&
        address < segment.base + static_cast<std::uint32_t>(segment.bytes.size())) {
      return &segment;
    }
  }
  return nullptr;
}

HostMemory::Segment *HostMemory::find(std::uint32_t address) {
  return const_cast<Segment *>(static_cast<const HostMemory *>(this)->find(address));
}

std::uint8_t HostMemory::guestByte(std::uint32_t address) const {
  return core_.mem_r8(address);
}

void HostMemory::refuseWrite(std::uint32_t address, std::uint32_t bytes) const {
  ++refusedWrites_;
  lucent::error(kChannel,
                "refusing a {}-byte write at 0x{:08X}: no host mapping owns it, and a write the "
                "guest did not make would change the real field",
                bytes,
                address);
  std::abort();
}

std::uint32_t HostMemory::r32(std::uint32_t address) const {
  const Segment *segment = find(address);
  if (segment == nullptr) {
    if (guestReads_ == 0) {
      lucent::info(kChannel,
                   "first unmapped read at 0x{:08X}: the level's records are the guest's own and "
                   "are read, never written",
                   address);
    }
    ++guestReads_;
    return core_.mem_r32(address);
  }
  const std::size_t at = address - segment->base;
  std::uint32_t value = 0;
  std::memcpy(&value, segment->bytes.data() + at, sizeof value);
  return value;
}

std::uint16_t HostMemory::r16(std::uint32_t address) const {
  const Segment *segment = find(address);
  if (segment == nullptr) {
    ++guestReads_;
    return core_.mem_r16(address);
  }
  const std::size_t at = address - segment->base;
  std::uint16_t value = 0;
  std::memcpy(&value, segment->bytes.data() + at, sizeof value);
  return value;
}

std::uint8_t HostMemory::r8(std::uint32_t address) const {
  const Segment *segment = find(address);
  if (segment == nullptr) {
    ++guestReads_;
    return core_.mem_r8(address);
  }
  return segment->bytes[address - segment->base];
}

void HostMemory::w32(std::uint32_t address, std::uint32_t value) {
  Segment *segment = find(address);
  if (segment == nullptr) {
    refuseWrite(address, 4);
    return;
  }
  std::memcpy(segment->bytes.data() + (address - segment->base), &value, sizeof value);
}

void HostMemory::w16(std::uint32_t address, std::uint16_t value) {
  Segment *segment = find(address);
  if (segment == nullptr) {
    refuseWrite(address, 2);
    return;
  }
  std::memcpy(segment->bytes.data() + (address - segment->base), &value, sizeof value);
}

void HostMemory::w8(std::uint32_t address, std::uint8_t value) {
  Segment *segment = find(address);
  if (segment == nullptr) {
    refuseWrite(address, 1);
    return;
  }
  segment->bytes[address - segment->base] = value;
}

bool HostMemory::mapped(std::uint32_t address) const {
  return find(address) != nullptr;
}

std::uint32_t HostMemory::mappedBytes() const {
  std::uint32_t total = 0;
  for (const Segment &segment : segments_) {
    total += static_cast<std::uint32_t>(segment.bytes.size());
  }
  return total;
}

std::vector<HostMemory::Range> HostMemory::ranges() const {
  std::vector<Range> out;
  out.reserve(segments_.size());
  for (const Segment &segment : segments_) {
    out.push_back(Range{segment.base, static_cast<std::uint32_t>(segment.bytes.size())});
  }
  return out;
}

} // namespace spyro::guest_terrain