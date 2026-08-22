#include "actor_draw_recipe.h"

#include <algorithm>

namespace spyro::actor_draw_recipe {
namespace {

int32_t sar(uint32_t value, uint32_t shift) {
  return (int32_t)value >> (shift & 31u);
}

int32_t nclip(uint32_t a, uint32_t b, uint32_t c) {
  const int32_t x0 = (int16_t)a, y0 = (int16_t)(a >> 16), x1 = (int16_t)b, y1 = (int16_t)(b >> 16),
                x2 = (int16_t)c, y2 = (int16_t)(c >> 16);
  return (int32_t)((int64_t)x0 * y1 + (int64_t)x1 * y2 + (int64_t)x2 * y0 - (int64_t)x0 * y2 -
                   (int64_t)x1 * y0 - (int64_t)x2 * y1);
}

} // namespace

QuadDecision classifyQuad(int32_t first, int32_t second, bool twoSided) {
  if (twoSided && first > 0) {
    first = -first;
  }
  if (twoSided && second < 0) {
    second = -second;
  }
  if (first >= 0) {
    return second > 0 ? QuadDecision::Second : QuadDecision::Reject;
  }
  return second > 0 ? QuadDecision::Full : QuadDecision::First;
}

namespace {

uint32_t vertexOffset(uint32_t word, unsigned index) {
  static constexpr unsigned shifts[] = {20, 11, 2};
  return (word >> shifts[index]) & 0x7fcu;
}

bool populate(const actor_prefix::Output &record,
              uint32_t sourceWord,
              PrimitiveInput &out,
              Reason &reason) {
  if (sourceWord >= record.primitiveWords.size()) {
    reason = Reason::Malformed;
    return false;
  }
  const size_t available = record.primitiveWords.size() - sourceWord;
  for (size_t i = 0; i < std::min(available, out.words.size()); ++i) {
    out.words[i] = record.primitiveWords[sourceWord + i];
  }
  const bool quad = (int32_t)out.words[0] < 0;
  const unsigned count = quad ? 4u : 3u;
  const unsigned requiredWords =
      quad ? ((out.words[0] & 2u) ? 6u : 3u) : ((out.words[0] & 2u) ? 5u : 2u);
  if (available < requiredWords) {
    reason = Reason::Malformed;
    return false;
  }
  out.depthOrigin = record.depthOrigin;
  out.shift = record.otShift;
  out.fog = record.fog;
  for (unsigned i = 0; i < count; ++i) {
    const uint32_t offset = i == 3 ? out.words[2] & 0x7fcu : vertexOffset(out.words[0], i);
    if ((offset & 3u) || offset / 4u >= record.vertices.size()) {
      reason = Reason::Malformed;
      return false;
    }
    const auto &vertex = record.vertices[offset / 4u];
    out.status[i] = vertex.scratchWord;
    out.xy[i] = (uint16_t)vertex.projected.sx | ((uint32_t)(uint16_t)vertex.projected.sy << 16);
    out.depth[i] = vertex.projected.sz;
    out.screenX[i] = vertex.projected.px;
    out.screenY[i] = vertex.projected.py;
    out.viewZ[i] = vertex.projected.pz;
  }
  const uint32_t material = out.words[1];
  const uint32_t offsets[] = {(material >> 17) & 0x7fcu,
                              (material >> 8) & 0x7fcu,
                              (material << 1) & 0x7fcu,
                              (out.words[2] >> 9) & 0x7fcu};
  for (unsigned i = 0; i < count; ++i) {
    if ((offsets[i] & 3u) || offsets[i] / 4u >= record.colors.size()) {
      reason = Reason::Malformed;
      return false;
    }
    out.color[i] = record.colors[offsets[i] / 4u];
  }
  out.color[0] &= 0x00ffffffu;
  return true;
}

std::vector<uint32_t> payload(const PrimitiveInput &s, Family family, bool second) {
  switch (family) {
  case Family::G4:
    return {0x08000000u,
            s.color[0] + 0x38000000u,
            s.xy[0],
            s.color[1],
            s.xy[1],
            s.color[2],
            s.xy[2],
            s.color[3],
            s.xy[3]};
  case Family::GT4:
    return {0x0c000000u,
            s.color[0] + 0x3c000000u,
            s.xy[0],
            s.words[3] + s.fog,
            s.color[1],
            s.xy[1],
            s.words[4],
            s.color[2],
            s.xy[2],
            s.words[5],
            s.color[3],
            s.xy[3],
            s.words[5] >> 16};
  case Family::G3: {
    const unsigned a = second ? 3u : 0u;
    return {0x06000000u,
            (s.color[a] & 0x00ffffffu) + 0x30000000u,
            s.xy[a],
            s.color[1],
            s.xy[1],
            s.color[2],
            s.xy[2]};
  }
  case Family::GT3: {
    const bool quad = (int32_t)s.words[0] < 0;
    uint32_t uv0 = (quad ? s.words[3] : s.words[2]) + s.fog;
    const uint32_t uv1 = quad ? s.words[4] : s.words[3];
    const uint32_t uv2 = quad ? s.words[5] : s.words[4];
    if (second) {
      uv0 = (uv0 & 0xffff0000u) | (s.words[5] >> 16);
    }
    const unsigned a = second ? 3u : 0u;
    return {0x09000000u,
            (s.color[a] & 0x00ffffffu) + 0x34000000u,
            s.xy[a],
            uv0,
            s.color[1],
            s.xy[1],
            uv1,
            s.color[2],
            s.xy[2],
            uv2};
  }
  }
  return {};
}

} // namespace

Evaluation evaluate(const PrimitiveInput &s) {
  Evaluation out{};
  const uint32_t control = s.words[0], material = s.words[1];
  const bool quad = (int32_t)control < 0, textured = (control & 2u) != 0;
  out.nextWord = quad ? (textured ? 6u : 3u) : (textured ? 5u : 2u);
  if (quad && (control & 4u)) {
    out.nextWord = 5u;
    out.supported = false;
    out.reason = Reason::Ft4;
    return out;
  }
  const unsigned count = quad ? 4u : 3u;
  if ((int32_t)s.shift < 0) {
    uint32_t common = ~0u;
    for (unsigned i = 0; i < count; ++i) {
      common &= s.status[i];
    }
    if (common & 31u) {
      out.reason = Reason::Outcode;
      return out;
    }
  }
  if (control & 8u) {
    out.reason = Reason::Skip;
    return out;
  }
  const int32_t first = nclip(s.xy[0], s.xy[1], s.xy[2]);
  bool second = false, full = false;
  if (!quad) {
    if ((control & 1u) ? first == 0 : first <= 0) {
      out.reason = first == 0 ? Reason::ZeroArea : Reason::Nclip;
      return out;
    }
    out.origin = Origin::Direct;
  } else {
    const int32_t other = nclip(s.xy[1], s.xy[2], s.xy[3]);
    switch (classifyQuad(first, other, (control & 1u) != 0)) {
    case QuadDecision::Reject:
      out.reason = first == 0 || other == 0 ? Reason::ZeroArea : Reason::Nclip;
      return out;
    case QuadDecision::First:
      out.origin = Origin::QuadFirst;
      break;
    case QuadDecision::Second:
      second = true;
      out.origin = Origin::QuadSecond;
      break;
    case QuadDecision::Full:
      full = true;
      out.origin = Origin::FullQuad;
      break;
    }
  }
  uint32_t depth;
  if (full) {
    depth = s.depth[0] - s.depthOrigin + s.depth[1] + s.depth[2] + s.depth[3];
  } else {
    const unsigned a = second ? 3u : 0u;
    depth = s.depth[a] + (s.depth[a] >> 1) - s.depthOrigin + s.depth[1] + (s.depth[1] >> 1) +
            s.depth[2];
  }
  if ((int32_t)depth < 0) {
    out.reason = Reason::Depth;
    return out;
  }
  const uint32_t bias = (uint32_t)((int32_t)material >> 28) << 1;
  const uint32_t q = full ? (uint32_t)sar(depth + (bias << (s.shift & 31u)), s.shift)
                          : (uint32_t)sar(depth, s.shift) + bias;
  if ((int32_t)q < 0) {
    out.reason = Reason::Depth;
    return out;
  }
  out.family = full ? (textured ? Family::GT4 : Family::G4) : (textured ? Family::GT3 : Family::G3);
  out.localBin = q;
  out.payload = payload(s, out.family, second);
  out.emitted = true;
  return out;
}

Recipe compose(std::span<const actor_prefix::Output> records) {
  Recipe recipe{};
  recipe.records = (uint32_t)records.size();
  const auto boundary = actor_prefix::classifyCall(records);
  recipe.visibleRecords = boundary.visibleRecords;
  recipe.rejectedRecords = boundary.rejectedRecords;
  if (boundary.status == actor_prefix::CallStatus::NoCorpus) {
    return recipe;
  }
  if (boundary.status != actor_prefix::CallStatus::Owned) {
    recipe.status = Status::Unsupported;
    recipe.firstReason = Reason::Prefix;
    return recipe;
  }
  for (uint32_t recordIndex = 0; recordIndex < records.size(); ++recordIndex) {
    const auto &record = records[recordIndex];
    if (record.status == actor_prefix::Status::VisibilityRejected) {
      continue;
    }
    uint32_t source = 0, ordinal = 0;
    while (source < record.primitiveWords.size()) {
      PrimitiveInput input{};
      Reason malformed = Reason::None;
      if (!populate(record, source, input, malformed)) {
        recipe.status = Status::Unsupported;
        recipe.firstReason = malformed;
        recipe.faces.clear();
        return recipe;
      }
      if (input.words[1] & 1u) {
        recipe.status = Status::Unsupported;
        recipe.firstReason = Reason::Semi;
        recipe.faces.clear();
        return recipe;
      }
      Evaluation result = evaluate(input);
      ++recipe.candidates;
      recipe.candidateOrder.push_back({recordIndex, source, input, result});
      if (!result.supported || result.nextWord == 0 || source + result.nextWord <= source ||
          source + result.nextWord > record.primitiveWords.size() ||
          (result.emitted && result.localBin >= 288u)) {
        recipe.status = Status::Unsupported;
        recipe.firstReason = !result.supported                           ? result.reason
                             : result.emitted && result.localBin >= 288u ? Reason::BinRange
                                                                         : Reason::Malformed;
        recipe.faces.clear();
        return recipe;
      }
      if (result.emitted) {
        recipe.faces.push_back({recordIndex,
                                source,
                                ordinal,
                                result.family,
                                result.origin,
                                result.localBin,
                                input,
                                result.payload});
      } else {
        ++recipe.rejectedCandidates;
      }
      source += result.nextWord;
      ++ordinal;
    }
  }
  recipe.status = recipe.faces.empty() ? Status::ValidEmpty : Status::Ready;
  return recipe;
}

} // namespace spyro::actor_draw_recipe
