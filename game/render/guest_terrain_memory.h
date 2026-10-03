// guest_terrain_memory.h — where one traversal of the terrain drawer reads and writes its WORKING
// memory, so the same traversal can run over the guest's own RAM and over host memory.
//
// WHAT "WORKING MEMORY" IS. The drawer's seven passes do not walk the level's geometry; they walk
// five ranges the frame owns for the length of one field:
//
//   the scratchpad        [0x1F800000, +0x400)  the vertex caches, the split grid cells, the
//                                                      projected (screen, depth) pairs
//   the scratch block     [scratch, +0x3000)    the five sector lists and their split entries
//   the primitive arena   [cursor, scratch+0x1000)  the GP0 packets the passes emit
//   the ordering table    [table, +0x1000)      the 512 8-byte depth bins packets link into
//   the fogged colours    [foggedColours, +0x1000)  the DPCS-faded copy a sector's polygons read
//
// Everything else the passes touch — sector records, polygon records, texture records, the crack
// and piece tables, the camera, the level's globals — is the LEVEL, and stays on `Core`.
//
// WHY TWO PLACES. The real field must read and write exactly the bytes retail did, so it runs over
// `GuestMemory`, which forwards to `Core` and is byte-identical by construction. An in-between
// field runs the identical passes with no guest code and no guest write, so it runs over
// `HostMemory`: guest-shaped ranges backed by host bytes, where a read of an unmapped address still
// falls through to the guest (level geometry is legitimately there) and a WRITE of an unmapped
// address is refused by name, because that is the one mistake that would change the real field.
#pragma once

#include "core.h"

#include <cstdint>
#include <vector>

namespace spyro::guest_terrain {

// The working-memory accessor every pass reads and writes through. Three widths, because the
// traversal packs GP0 tag address fields, 16-bit screen words and 8-bit flags into the same ranges.
class TerrainMemory {
public:
  virtual ~TerrainMemory() = default;

  [[nodiscard]] virtual std::uint32_t r32(std::uint32_t address) const = 0;
  [[nodiscard]] virtual std::uint16_t r16(std::uint32_t address) const = 0;
  [[nodiscard]] virtual std::uint8_t r8(std::uint32_t address) const = 0;
  virtual void w32(std::uint32_t address, std::uint32_t value) = 0;
  virtual void w16(std::uint32_t address, std::uint16_t value) = 0;
  virtual void w8(std::uint32_t address, std::uint8_t value) = 0;

  // Whether `address` is one this accessor owns, or whether the read would fall through / the
  // write would be refused. A pass never needs this today; the diagnostics and the tests do.
  [[nodiscard]] virtual bool mapped(std::uint32_t address) const = 0;
};

// The guest's own RAM, byte for byte. What the real field runs over.
class GuestMemory final : public TerrainMemory {
public:
  explicit GuestMemory(Core &core) : core_(core) {}

  [[nodiscard]] std::uint32_t r32(std::uint32_t address) const override {
    return core_.mem_r32(address);
  }
  [[nodiscard]] std::uint16_t r16(std::uint32_t address) const override {
    return core_.mem_r16(address);
  }
  [[nodiscard]] std::uint8_t r8(std::uint32_t address) const override {
    return core_.mem_r8(address);
  }
  void w32(std::uint32_t address, std::uint32_t value) override {
    core_.mem_w32(address, value);
  }
  void w16(std::uint32_t address, std::uint16_t value) override {
    core_.mem_w16(address, value);
  }
  void w8(std::uint32_t address, std::uint8_t value) override {
    core_.mem_w8(address, value);
  }
  // Everything the guest owns, which is the whole address space this accessor is handed.
  [[nodiscard]] bool mapped(std::uint32_t) const override {
    return true;
  }

private:
  Core &core_;
};

// Guest-shaped ranges backed by host bytes.
//
// A mapping copies the guest's current bytes, so a range that is only partly rewritten by a pass
// still holds the level data the passes read out of it. Overlapping mappings MERGE: the primitive
// arena's top is inside the scratch block, and the ordering table's low bins are inside it too, so
// treating them as separate objects would make one of the two lose the other's writes.
class HostMemory final : public TerrainMemory {
public:
  explicit HostMemory(Core &core) : core_(core) {}

  // Own [base, base + bytes) of host storage, seeded from the guest's bytes at the address it
  // already owns and zero elsewhere. A zero `bytes` reserves nothing.
  void map(std::uint32_t base, std::uint32_t bytes);

  // Empty [base, base + bytes) instead of seeding it from the guest's bytes. A range the traversal
  // must find EMPTY is not a range it also reads level data out of, and `map`'s seeding would leave
  // the guest's last field's own contents there for it to adopt — which for an ordering table means
  // linking a fresh packet behind a stale head and drawing a chain the guest never built.
  //
  // This is about the HOST's copy only. The guest's bytes are not touched, which is the whole
  // reason the in-between can have a table at all.
  void mapEmpty(std::uint32_t base, std::uint32_t bytes);

  // Forget every mapping and every counted read and write.
  void clear();

  [[nodiscard]] std::uint32_t r32(std::uint32_t address) const override;
  [[nodiscard]] std::uint16_t r16(std::uint32_t address) const override;
  [[nodiscard]] std::uint8_t r8(std::uint32_t address) const override;
  void w32(std::uint32_t address, std::uint32_t value) override;
  void w16(std::uint32_t address, std::uint16_t value) override;
  void w8(std::uint32_t address, std::uint8_t value) override;
  [[nodiscard]] bool mapped(std::uint32_t address) const override;

  // How many bytes the mappings own in total, and how many reads and writes fell through to the
  // guest or were refused. The write count is the number the run logs, because a non-zero count is
  // a claim about a guarantee this class makes.
  [[nodiscard]] std::uint32_t mappedBytes() const;
  [[nodiscard]] std::uint64_t guestReads() const {
    return guestReads_;
  }
  [[nodiscard]] std::uint64_t refusedWrites() const {
    return refusedWrites_;
  }

  // The merged ranges, in address order. Read by the tests and by a diagnostic that has to name
  // what an in-between field actually reserved.
  struct Range {
    std::uint32_t base = 0;
    std::uint32_t bytes = 0;
  };
  [[nodiscard]] std::vector<Range> ranges() const;

private:
  struct Segment {
    std::uint32_t base = 0;
    std::vector<std::uint8_t> bytes;
  };

  // The segment holding `address`, or nullptr.
  [[nodiscard]] const Segment *find(std::uint32_t address) const;
  [[nodiscard]] Segment *find(std::uint32_t address);
  [[nodiscard]] std::uint8_t guestByte(std::uint32_t address) const;
  void refuseWrite(std::uint32_t address, std::uint32_t bytes) const;

  Core &core_;
  std::vector<Segment> segments_;
  mutable std::uint64_t guestReads_ = 0;
  mutable std::uint64_t refusedWrites_ = 0;
};

} // namespace spyro::guest_terrain