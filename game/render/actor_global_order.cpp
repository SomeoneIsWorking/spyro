#include "actor_global_order.h"

#include <algorithm>
#include <array>
#include <limits>

namespace spyro::actor_global_order {
namespace {

constexpr uint32_t kLocalOtBase = 0x8006fcf4u;
constexpr uint32_t kLocalOtEnd = 0x800704f4u;
constexpr uint32_t kLocalBinCount = 288u;
constexpr uint32_t kGlobalBinCount = 2048u;

uint32_t localBase(const actor_prefix::Output &record) {
  const uint32_t depthOrigin = record.depthOrigin;
  if ((int32_t)depthOrigin > 0) {
    return kLocalOtBase;
  }
  const uint32_t offset = ((uint32_t)(0u - depthOrigin) >> (record.otShift & 31u)) << 3u;
  return kLocalOtBase + offset;
}

bool mapRecord(const actor_prefix::Output &record,
               std::span<const size_t> members,
               std::span<const actor_draw_recipe::Face> faces,
               uint32_t recordOrdinal,
               std::vector<FaceKey> &out) {
  uint32_t minBin = kLocalBinCount;
  uint32_t maxBin = 0;
  for (const size_t index : members) {
    const uint32_t bin = faces[index].localBin;
    if (bin >= kLocalBinCount) {
      return false;
    }
    minBin = std::min(minBin, bin);
    maxBin = std::max(maxBin, bin);
  }

  const uint32_t base = localBase(record);
  const uint32_t minPair = base + minBin * 8u;
  const uint32_t maxPair = base + maxBin * 8u;
  const uint32_t shift = ((record.controls[13] >> 8u) + (record.controls[13] & 255u)) & 31u;
  const uint32_t step = 256u >> shift;
  if (step < 8u || (step & 7u) != 0u) {
    return false;
  }

  uint32_t globalOffset = (record.controls[14] << 3u) + ((32u << shift) - 8u);
  const uint32_t gap = kLocalOtEnd - maxPair;
  const uint32_t adjust = (int32_t)gap < 0 ? 0u : (((gap << shift) >> 8u) << 3u);
  globalOffset -= adjust;
  if ((int32_t)globalOffset < 0) {
    globalOffset = 0;
  }
  if ((globalOffset & 7u) != 0u || globalOffset / 8u >= kGlobalBinCount) {
    return false;
  }

  std::array<uint16_t, kLocalBinCount> mapped{};
  std::array<bool, kLocalBinCount> assigned{};
  uint32_t scan = maxPair;
  const uint32_t terminal = minPair - 8u;
  while (true) {
    const uint32_t candidate = scan - step;
    const uint32_t limit = (int32_t)(terminal - candidate) > 0 ? terminal : candidate;
    for (uint32_t pair = scan; pair != limit; pair -= 8u) {
      const uint32_t bin = (pair - base) / 8u;
      if (bin >= kLocalBinCount) {
        return false;
      }
      mapped[bin] = (uint16_t)(globalOffset / 8u);
      assigned[bin] = true;
    }
    if (limit == terminal) {
      break;
    }
    scan = limit;
    globalOffset = globalOffset == 0u ? 8u : globalOffset - 8u;
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
    if (!assigned[localBin]) {
      return false;
    }
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
