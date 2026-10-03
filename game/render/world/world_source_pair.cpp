#include "world_source_pair.h"

#include <lucent/log.h>
#include <tuple>

namespace spyro::world_source_pair {
namespace {

bool sameRange(const std::optional<GuestAddressRange> &a,
               const std::optional<GuestAddressRange> &b) {
  return a.has_value() == b.has_value() && (!a || (a->begin == b->begin && a->end == b->end));
}

bool lowCompatible(const world_chunk_codec::LowChunk &a,
                   const world_chunk_codec::LowChunk &b,
                   const char *&why) {
  if (a.address != b.address || !sameRange(a.payloadRange, b.payloadRange)) {
    why = "low_source_range";
    return false;
  }
  if (a.descriptor != b.descriptor || a.vertices.size() != b.vertices.size() ||
      a.colors.size() != b.colors.size() || a.faces.size() != b.faces.size()) {
    why = "low_layout";
    return false;
  }
  if (a.colors != b.colors) {
    why = "low_colors";
    return false;
  }
  for (size_t i = 0; i < a.faces.size(); ++i) {
    const auto &left = a.faces[i];
    const auto &right = b.faces[i];
    if (left.address != right.address || left.vertexWord != right.vertexWord ||
        left.materialWord != right.materialWord) {
      why = "low_face";
      return false;
    }
  }
  return true;
}

bool highCompatible(const world_chunk_codec::HighChunk &a,
                    const world_chunk_codec::HighChunk &b,
                    const char *&why) {
  if (a.address != b.address || !sameRange(a.payloadRange, b.payloadRange)) {
    why = "high_source_range";
    return false;
  }
  if (a.layout != b.layout || a.vertices.size() != b.vertices.size() ||
      a.farColors.size() != b.farColors.size() || a.nearColors.size() != b.nearColors.size() ||
      a.faces.size() != b.faces.size()) {
    why = "high_layout";
    return false;
  }
  // world_hq_recipe uses the low halfword as its per-face status-table offset.
  // The high halfword contributes to the authored Z origin and may move.
  if ((a.originAndOffset & 0xffffu) != (b.originAndOffset & 0xffffu)) {
    why = "high_status_offset";
    return false;
  }
  if (a.farColors != b.farColors || a.nearColors != b.nearColors) {
    why = "high_colors";
    return false;
  }
  for (size_t i = 0; i < a.faces.size(); ++i) {
    const auto &left = a.faces[i];
    const auto &right = b.faces[i];
    if (left.address != right.address || left.vertexWord != right.vertexWord ||
        left.colorWord != right.colorWord || left.materialWord != right.materialWord ||
        left.flags != right.flags) {
      why = "high_face";
      return false;
    }
  }
  return true;
}

bool sameProjection(const psxport::native_projection::ProjectionParams &a,
                    const psxport::native_projection::ProjectionParams &b) {
  return std::tie(a.ofx, a.ofy, a.h, a.dqa, a.dqb) == std::tie(b.ofx, b.ofy, b.h, b.dqa, b.dqb);
}

} // namespace

bool compatible(const world_source::Source &previous,
                const world_source::Source &current,
                const char *&why) {
  why = "none";
  const auto &a = previous.selection;
  const auto &b = current.selection;
  if (!a.valid || !b.valid) {
    why = "selection_invalid";
    return false;
  }
  if (!sameProjection(previous.projection, current.projection) ||
      previous.clipRight != current.clipRight) {
    why = "projection_changed";
    return false;
  }
  if (a.lodDistance != b.lodDistance || a.cullingDistance != b.cullingDistance ||
      a.skipLow != b.skipLow) {
    why = "selection_policy";
    return false;
  }
  if (a.group != b.group || a.occurrences != b.occurrences) {
    why = "selection_occurrences";
    return false;
  }
  // Fps60 presents both passes against the current live texture residency. The sampler uses
  // current discrete UV state for interior geometry, while exact t0/t1 rebuild their endpoints.
  // UV scrolling is not a resource/layout change; texture identity and all refinement rules are.
  if (!previous.materials.sameIdentity(current.materials)) {
    const auto difference = previous.materials.difference(current.materials);
    lucent::debug("worldtemporal",
                  "material difference: blocks={} scanned={} changed={} layout={} "
                  "count={}/{} low={:08X}/{:08X} high={:08X}/{:08X} "
                  "first_block=[{:08X},+{:X}) first={:08X} byte={:02X}->{:02X}",
                  difference.scannedBlocks,
                  difference.scannedBytes,
                  difference.changedBytes,
                  difference.layoutMismatch,
                  previous.materials.count(),
                  current.materials.count(),
                  previous.materials.lowBase(),
                  current.materials.lowBase(),
                  previous.materials.highBase(),
                  current.materials.highBase(),
                  difference.blockAddress,
                  difference.blockSize,
                  difference.firstAddress,
                  difference.before,
                  difference.after);
    why = "captured_materials";
    return false;
  }
  for (size_t index = 0; index < previous.sectors.size(); ++index) {
    if (a.sectors[index].has_value() != b.sectors[index].has_value() ||
        previous.sectors[index].has_value() != current.sectors[index].has_value()) {
      why = "sector_presence";
      return false;
    }
    if (a.sectors[index]) {
      const auto &left = *a.sectors[index];
      const auto &right = *b.sectors[index];
      // world_scene_prepare consumes low13 as radius, bits13..15 as LOD
      // controls; the high halfword is center Z rather than an extent.
      if (left.address != right.address || (left.extent & 0xffffu) != (right.extent & 0xffffu)) {
        why = "sector_control";
        return false;
      }
    }
    if (!previous.sectors[index]) {
      continue;
    }
    const auto &left = *previous.sectors[index];
    const auto &right = *current.sectors[index];
    if (left.lowStatus != right.lowStatus || left.highStatus != right.highStatus) {
      why = "lod_availability";
      return false;
    }
    if (left.lowStatus == world_chunk_codec::Status::Ok &&
        !lowCompatible(left.low, right.low, why)) {
      return false;
    }
    if (left.highStatus == world_chunk_codec::Status::Ok &&
        !highCompatible(left.high, right.high, why)) {
      return false;
    }
  }
  return true;
}

} // namespace spyro::world_source_pair
