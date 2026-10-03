#include "actor_global_order.h"
#include "actor_ot_coalescer.h"

#include <algorithm>
#include <array>

namespace spyro::actor_global_order {
namespace {

using actor_ot_coalescer::kGlobalBinCount;
using actor_ot_coalescer::kLocalBinCount;

uint32_t localBase(const actor_prefix::Output &record) {
  const uint32_t depthOrigin = record.depthOrigin;
  if ((int32_t)depthOrigin > 0) {
    return 0;
  }
  const uint32_t offset = ((uint32_t)(0u - depthOrigin) >> (record.otShift & 31u)) << 3u;
  return offset;
}

bool mapRecord(const actor_prefix::Output &record,
               std::span<const size_t> members,
               std::span<const actor_draw_recipe::Face> faces,
               uint32_t recordOrdinal,
               std::vector<FaceKey> &out) {
  std::vector<uint32_t> bins;
  bins.reserve(members.size());
  for (size_t index : members) {
    bins.push_back(faces[index].localBin);
  }
  const auto mapping =
      actor_ot_coalescer::map({localBase(record),
                               record.controls[14],
                               (record.controls[13] >> 8u) + (record.controls[13] & 255u)},
                              bins);
  if (!mapping.valid) {
    return false;
  }
  std::array<uint16_t, kLocalBinCount> mapped{};
  for (size_t i = 0; i < bins.size(); ++i) {
    mapped[bins[i]] = mapping.bins[i];
  }

  std::vector<size_t> replay(members.begin(), members.end());
  std::stable_sort(replay.begin(), replay.end(), [&](size_t left, size_t right) {
    const auto &a = faces[left];
    const auto &b = faces[right];
    if (a.localBin != b.localBin) {
      return a.localBin > b.localBin;
    }
    return a.sourceOrdinal < b.sourceOrdinal;
  });
  std::array<uint32_t, kGlobalBinCount> chainOrdinals{};
  for (const size_t index : replay) {
    const uint32_t localBin = faces[index].localBin;
    const uint16_t globalBin = mapped[localBin];
    out.push_back({index, globalBin, recordOrdinal, chainOrdinals[globalBin]++});
  }
  return true;
}

} // namespace

Result build(std::span<const actor_prefix::Output> records,
             std::span<const actor_draw_recipe::Face> faces) {
  Result result{};
  if (faces.empty()) {
    return result;
  }
  std::vector<std::vector<size_t>> members(records.size());
  for (size_t i = 0; i < faces.size(); ++i) {
    if (faces[i].record >= records.size()) {
      result.status = Status::InvalidRecord;
      result.refusal = "face_record";
      return result;
    }
    if (faces[i].localBin >= kLocalBinCount) {
      result.status = Status::InvalidLocalBin;
      result.refusal = "local_bin";
      return result;
    }
    members[faces[i].record].push_back(i);
  }
  result.faces.reserve(faces.size());
  for (uint32_t record = 0; record < records.size(); ++record) {
    if (members[record].empty()) {
      continue;
    }
    if (!mapRecord(records[record], members[record], faces, record, result.faces)) {
      result.faces.clear();
      result.status = Status::InvalidMapping;
      result.refusal = "global_mapping";
      return result;
    }
  }
  if (result.faces.size() != faces.size()) {
    result.faces.clear();
    result.status = Status::InvalidMapping;
    result.refusal = "face_count";
    return result;
  }
  result.status = Status::Ready;
  return result;
}

} // namespace spyro::actor_global_order
