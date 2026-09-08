#include "world_source.h"

#include "world_material_codec.h"
#include "world_projection_math.h"

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace spyro::world_source {
namespace {
constexpr uint32_t kEnvironment = 0x800785a8u;
constexpr uint32_t kCamera = 0x80076dd0u;
// First transition descriptor through the last triangle rotation/attribute adjustment. These
// are authored table inputs used by world_hq_refinement, not executable or projected data.
constexpr uint32_t kRefinementTables = 0x8006cf98u;
constexpr uint32_t kRefinementTablesEnd = 0x8006d5c8u;
} // namespace

Selection select(const world_chunk_codec::RamView &ram, int32_t group) {
  Selection out{};
  out.group = group;
  if (!ram.contains(kEnvironment, 44u) || !ram.contains(kCamera, 52u) ||
      !ram.contains(0x8007591cu, 4u)) {
    out.refusal = "global_bounds";
    return out;
  }
  const uint32_t table = ram.r32(kEnvironment);
  const uint32_t count = ram.r32(kEnvironment + 4u);
  if (count > 256u || (count && !ram.contains(table, count * 4u))) {
    out.refusal = "sector_table";
    return out;
  }
  if (count) {
    out.resourceRanges.push_back(*ram.range(table, count * 4u));
  }
  if (group < 0) {
    for (uint32_t i = 0; i < count; ++i) {
      out.occurrences.push_back((uint8_t)i);
    }
  } else {
    const uint32_t groups = ram.r32(kEnvironment + 8u);
    const uint64_t slot = (uint64_t)groups + (uint32_t)group * 4ull;
    if (slot > UINT32_MAX || !ram.contains((uint32_t)slot, 4u)) {
      out.refusal = "occlusion_group_slot";
      return out;
    }
    out.resourceRanges.push_back(*ram.range((uint32_t)slot, 4u));
    uint32_t cursor = ram.r32((uint32_t)slot);
    const uint32_t groupStart = cursor;
    for (uint32_t guard = 0; guard <= 256u; ++guard) {
      if (!ram.contains(cursor, 1u)) {
        out.refusal = "occlusion_group_bounds";
        return out;
      }
      const uint8_t index = ram.r8(cursor++);
      if (index == 0xffu) {
        out.resourceRanges.push_back(*ram.range(groupStart, cursor - groupStart));
        break;
      }
      if (index >= count) {
        out.refusal = "occlusion_sector_index";
        return out;
      }
      out.occurrences.push_back(index);
      if (guard == 256u) {
        out.refusal = "occlusion_group_unterminated";
        return out;
      }
    }
  }
  out.camera.projectionMatrix = world_projection_math::decodeMatrix(ram, kCamera);
  out.camera.cullingMatrix = world_projection_math::decodeMatrix(ram, kCamera + 0x14u);
  for (uint32_t i = 0; i < 3; ++i) {
    out.camera.position[i] = (int32_t)ram.r32(kCamera + 0x28u + i * 4u);
  }
  out.lodDistance = ram.r32(kEnvironment + 0x24u);
  out.cullingDistance = ram.r32(kEnvironment + 0x28u);
  out.skipLow = ram.r32(0x8007591cu) != 0u;
  for (uint8_t index : out.occurrences) {
    const uint32_t address = ram.r32(table + (uint32_t)index * 4u);
    if ((address & 3u) || !ram.contains(address, 0x1cu)) {
      out.refusal = "sector_bounds";
      return out;
    }
    if (!out.sectors[index]) {
      out.resourceRanges.push_back(*ram.range(address, 0x1cu));
    }
    out.sectors[index] =
        SectorHeader{address, ram.r32(address), ram.r32(address + 4u), ram.r32(address + 0x18u)};
  }
  out.valid = true;
  out.refusal = "none";
  return out;
}

Materials Materials::capture(const world_chunk_codec::RamView &ram) {
  Materials out;
  if (!ram.contains(kEnvironment, 44u)) {
    return out;
  }
  out.count_ = ram.r32(kEnvironment + 0x20u);
  out.lowBase_ = ram.r32(kEnvironment + 0x18u);
  out.highBase_ = ram.r32(kEnvironment + 0x1cu);
  const auto retain = [&](uint32_t address, uint32_t requested) {
    if (!ram.contains(address, 1u)) {
      return; // An absent record stays absent; its consumer retains the bounds refusal.
    }
    // Keep a valid prefix even when a later field is outside RAM. Consumers own their exact
    // read spans; capture must not turn an in-bounds tile into a whole-record refusal.
    uint32_t size = std::min(requested, 0x200000u - (address & 0x1fffffffu));
    while (size && !ram.contains(address, size)) {
      --size;
    }
    Block block{address & 0x1fffffffu, {}};
    block.bytes.reserve(size);
    for (uint32_t i = 0; i < size; ++i) {
      block.bytes.push_back(ram.r8(address + i));
    }
    out.blocks_.push_back(std::move(block));
  };
  retain(kRefinementTables, kRefinementTablesEnd - kRefinementTables);
  const auto registerTile = [&](uint32_t address) {
    if (ram.contains(address, 1u)) {
      out.tilePairs_.push_back(address & 0x1fffffffu);
    }
  };
  const auto retainTiles = [&](uint32_t address, uint32_t size) {
    retain(address, size);
    if (ram.contains(address, 1u)) {
      for (uint32_t offset = 0; offset < size; offset += world_material_codec::Tile::kPackedSize) {
        registerTile(address + offset);
      }
    }
  };
  // The material ID encodes seven bits. Preserve every addressable authored record regardless
  // of current fog/LOD choice; material branches may change when the camera moves.
  const uint32_t count = std::min(out.count_, 128u);
  for (uint32_t i = 0; i < count; ++i) {
    retainTiles(out.lowBase_ + i * 16u, 16u);
    const uint32_t material = out.highBase_ + i * 0xa8u;
    retainTiles(material, 0xa8u);
    for (const auto &pairs :
         {world_material_codec::kMediumTrianglePairs, world_material_codec::kNearTrianglePairs}) {
      if (!ram.contains(pairs.selectors, pairs.count)) {
        continue; // Missing table bytes remain unavailable to refinement.
      }
      for (uint32_t selector = 0; selector < pairs.count; ++selector) {
        const uint32_t pair = pairs.address(material, (int8_t)ram.r8(pairs.selectors + selector));
        if (pair < material || pair - material > 0xa8u - world_material_codec::Tile::kPackedSize) {
          retain(pair, world_material_codec::Tile::kPackedSize);
        }
        registerTile(pair);
      }
    }
  }
  std::sort(out.blocks_.begin(), out.blocks_.end(), [](const Block &left, const Block &right) {
    return left.address < right.address;
  });
  std::vector<Block> merged;
  for (auto &block : out.blocks_) {
    if (!merged.empty() && block.address <= merged.back().address + merged.back().bytes.size()) {
      auto &previous = merged.back();
      const size_t overlap = previous.address + previous.bytes.size() - block.address;
      if (overlap < block.bytes.size()) {
        previous.bytes.insert(
            previous.bytes.end(), block.bytes.begin() + overlap, block.bytes.end());
      }
    } else {
      merged.push_back(std::move(block));
    }
  }
  out.blocks_ = std::move(merged);
  std::sort(out.tilePairs_.begin(), out.tilePairs_.end());
  out.tilePairs_.erase(std::unique(out.tilePairs_.begin(), out.tilePairs_.end()),
                       out.tilePairs_.end());
  return out;
}

bool Materials::isTileUvByte(uint32_t address) const {
  if (address >= (kRefinementTables & 0x1fffffffu) &&
      address < (kRefinementTablesEnd & 0x1fffffffu)) {
    return false;
  }
  constexpr uint32_t size = world_material_codec::Tile::kPackedSize;
  auto pair = std::lower_bound(
      tilePairs_.begin(), tilePairs_.end(), address > size - 1u ? address - (size - 1u) : 0u);
  bool covered = false;
  for (; pair != tilePairs_.end() && *pair <= address; ++pair) {
    // An overlapping signed selector may interpret another tile's UV as an identity field.
    // A changed byte is UV state only when every tile that reads it agrees.
    if (!world_material_codec::Tile::isUvByte(address - *pair)) {
      return false;
    }
    covered = true;
  }
  return covered;
}

bool Materials::sameIdentity(const Materials &other) const {
  if (count_ != other.count_ || lowBase_ != other.lowBase_ || highBase_ != other.highBase_ ||
      tilePairs_ != other.tilePairs_ || blocks_.size() != other.blocks_.size()) {
    return false;
  }
  for (size_t i = 0; i < blocks_.size(); ++i) {
    const auto &left = blocks_[i];
    const auto &right = other.blocks_[i];
    if (left.address != right.address || left.bytes.size() != right.bytes.size()) {
      return false;
    }
    for (size_t j = 0; j < left.bytes.size(); ++j) {
      if (left.bytes[j] != right.bytes[j] && !isTileUvByte(left.address + (uint32_t)j)) {
        return false;
      }
    }
  }
  return true;
}

MaterialDifference Materials::difference(const Materials &other) const {
  MaterialDifference out;
  out.layoutMismatch = count_ != other.count_ || lowBase_ != other.lowBase_ ||
                       highBase_ != other.highBase_ || blocks_.size() != other.blocks_.size();
  for (size_t i = 0; i < std::min(blocks_.size(), other.blocks_.size()); ++i) {
    const auto &left = blocks_[i];
    const auto &right = other.blocks_[i];
    if (left.address != right.address || left.bytes.size() != right.bytes.size()) {
      out.layoutMismatch = true;
      continue;
    }
    ++out.scannedBlocks;
    for (size_t j = 0; j < left.bytes.size(); ++j) {
      ++out.scannedBytes;
      if (left.bytes[j] != right.bytes[j]) {
        if (!out.changedBytes) {
          out.blockAddress = left.address;
          out.blockSize = (uint32_t)left.bytes.size();
          out.firstAddress = left.address + (uint32_t)j;
          out.before = left.bytes[j];
          out.after = right.bytes[j];
        }
        ++out.changedBytes;
      }
    }
  }
  return out;
}

std::vector<GuestAddressRange> Materials::resourceRanges() const {
  std::vector<GuestAddressRange> out;
  out.reserve(blocks_.size());
  for (const auto &block : blocks_) {
    out.push_back({block.address, block.address + (uint32_t)block.bytes.size()});
  }
  return out;
}

std::vector<GuestAddressRange> Source::resourceRanges() const {
  if (!selection.valid) {
    return {};
  }
  auto out = selection.resourceRanges;
  const auto materialRanges = materials.resourceRanges();
  out.insert(out.end(), materialRanges.begin(), materialRanges.end());
  for (const auto &sector : sectors) {
    if (!sector) {
      continue;
    }
    if (sector->lowStatus == world_chunk_codec::Status::Ok && sector->low.payloadRange) {
      out.push_back(*sector->low.payloadRange);
    }
    if (sector->highStatus == world_chunk_codec::Status::Ok && sector->high.payloadRange) {
      out.push_back(*sector->high.payloadRange);
    }
  }
  std::sort(out.begin(), out.end(), [](const auto &left, const auto &right) {
    return left.begin < right.begin || (left.begin == right.begin && left.end < right.end);
  });
  out.erase(std::unique(out.begin(),
                        out.end(),
                        [](const auto &left, const auto &right) {
                          return left.begin == right.begin && left.end == right.end;
                        }),
            out.end());
  return out;
}

const Materials::Block *Materials::find(uint32_t address, uint32_t size) const {
  if (!(address < 0x00200000u || (address >= 0x80000000u && address < 0x80200000u))) {
    return nullptr;
  }
  const uint32_t physical = address & 0x1fffffffu;
  auto after = std::upper_bound(
      blocks_.begin(), blocks_.end(), physical, [](uint32_t value, const Block &block) {
        return value < block.address;
      });
  if (after == blocks_.begin()) {
    return nullptr;
  }
  const auto &block = *std::prev(after);
  const size_t offset = physical - block.address;
  return offset <= block.bytes.size() && size <= block.bytes.size() - offset ? &block : nullptr;
}

bool Materials::contains(uint32_t address, uint32_t size) const {
  return find(address, size) != nullptr;
}

uint8_t Materials::r8(uint32_t address) const {
  const Block *block = find(address, 1u);
  if (!block) {
    throw std::out_of_range("world material read outside captured authored records");
  }
  return block->bytes[(address & 0x1fffffffu) - block->address];
}
uint16_t Materials::r16(uint32_t address) const {
  const Block *block = find(address, 2u);
  if (!block) {
    throw std::out_of_range("world material read outside captured authored records");
  }
  const auto *bytes = block->bytes.data() + ((address & 0x1fffffffu) - block->address);
  return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8u);
}
uint32_t Materials::r32(uint32_t address) const {
  const Block *block = find(address, 4u);
  if (!block) {
    throw std::out_of_range("world material read outside captured authored records");
  }
  const auto *bytes = block->bytes.data() + ((address & 0x1fffffffu) - block->address);
  return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8u) | ((uint32_t)bytes[2] << 16u) |
         ((uint32_t)bytes[3] << 24u);
}

Source capture(const world_chunk_codec::RamView &ram,
               int32_t selection,
               psxport::native_projection::ProjectionParams projection,
               int32_t clipRight,
               std::optional<uint32_t> cullingDistance) {
  Source out{};
  out.selection = select(ram, selection);
  out.projection = projection;
  out.clipRight = clipRight;
  if (!out.selection.valid) {
    return out;
  }
  if (cullingDistance) {
    out.selection.cullingDistance = *cullingDistance;
  }
  out.materials = Materials::capture(ram);
  for (uint8_t index : out.selection.occurrences) {
    if (out.sectors[index]) {
      continue;
    }
    Sector sector;
    const uint32_t address = out.selection.sectors[index]->address;
    sector.lowStatus = world_chunk_codec::decodeLow(ram, address, sector.low);
    sector.highStatus = world_chunk_codec::decodeHigh(ram, address, sector.high);
    out.sectors[index] = std::move(sector);
  }
  return out;
}

} // namespace spyro::world_source
