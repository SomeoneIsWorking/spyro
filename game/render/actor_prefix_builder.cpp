#include "actor_prefix_builder.h"

#include <algorithm>
#include <string_view>

namespace spyro::actor_prefix {
namespace {

int32_t sar32(uint32_t value, uint8_t shift) {
  return (int32_t)value >> (shift & 31u);
}

int32_t wrapSub(int32_t left, uint32_t right) {
  return (int32_t)((uint32_t)left - right);
}

actor_model_codec::StreamResult decode(const OwnedStream &stream, uint32_t count, uint8_t shift) {
  return actor_model_codec::decodeStream(
      {stream.firstFull, stream.fullWords, stream.deltaWords, count, shift});
}

psxport::native_projection::FixedAffine affineFrom(const Input &input,
                                                   std::array<uint32_t, 16> &controls) {
  const uint32_t c0 = input.matrixWords[0], c1 = input.matrixWords[1], c2 = input.matrixWords[2],
                 c3 = input.matrixWords[3], c4 = input.matrixWords[4];
  psxport::native_projection::FixedAffine affine{};
  affine.m = {{{(int16_t)c0, (int16_t)(c0 >> 16), (int16_t)c1},
               {(int16_t)(c1 >> 16), (int16_t)c2, (int16_t)(c2 >> 16)},
               {(int16_t)c3, (int16_t)(c3 >> 16), (int16_t)c4}}};
  const uint8_t shift = input.transformShift & 31u;
  affine.t = {{sar32((uint32_t)input.tx << 2, shift),
               sar32((uint32_t)input.ty << 2, shift),
               sar32((uint32_t)input.tz << 2, shift)}};
  for (unsigned i = 0; i < 5; ++i) {
    controls[i] = input.matrixWords[i];
  }
  // The record packs CR30 in the high half of its final matrix word. CTC2 CR4
  // consumes only the low matrix half; CR30 is loaded separately.
  controls[4] &= 0xffffu;
  for (unsigned i = 0; i < 3; ++i) {
    controls[5 + i] = (uint32_t)affine.t[i];
  }
  return affine;
}

psxport::native_projection::ModelVertex projectionInput(actor_model_codec::Vec3i value) {
  const uint32_t yz = ((uint32_t)value.z << 16) + (uint32_t)value.y;
  return {(int16_t)yz, (int16_t)(yz >> 16), (int16_t)value.x};
}

std::array<int32_t, 3> farColor(uint32_t rgb) {
  return {
      (int32_t)((rgb << 4) & 4080u), (int32_t)((rgb >> 4) & 4080u), (int32_t)((rgb >> 12) & 4080u)};
}

uint32_t packedSxy(const psxport::native_projection::NativeProjectedVertex &projected) {
  return (uint16_t)projected.sx | ((uint32_t)(uint16_t)projected.sy << 16);
}

uint32_t clipStatusWord(uint32_t sxy) {
  uint32_t flags = 0;
  if ((int32_t)(sxy - 0x00010000u) <= 0) {
    flags |= 1u;
  }
  if ((int32_t)(sxy - 0x01000000u) >= 0) {
    flags |= 2u;
  }
  const uint32_t vertical = sxy << 16;
  if ((int32_t)vertical <= 0) {
    flags |= 4u;
  }
  if ((int32_t)(vertical - 0x02000000u) >= 0) {
    flags |= 8u;
  }
  return (sxy << 5) | flags;
}

} // namespace

Output build(const Input &input) {
  Output out{};
  if (input.optionalExpansion) {
    out.status = Status::OptionalExpansion;
    return out;
  }
  if (input.vertexCount == 0) {
    out.status = Status::CountZero;
    return out;
  }
  if ((input.header & 0xffu) != 0) {
    out.status = Status::TransformBlend;
    return out;
  }
  if (input.colorArm == ColorArm::Plain) {
    out.status = Status::PlainColor;
    return out;
  }
  if (input.colorArm == ColorArm::NegativeBlend) {
    out.status = Status::NegativeBlend;
    return out;
  }

  const uint8_t coordShift = input.header >> 24;
  const int8_t translationBias = (int8_t)(input.header >> 16);
  int32_t cr14 = ((int32_t)input.tz >> 5) - translationBias;
  if (cr14 < 0) {
    cr14 = 0;
  }
  if (cr14 >= 272) {
    cr14 = (int32_t)((uint32_t)cr14 + 32u);
  }
  const auto affine = affineFrom(input, out.controls);
  out.controls[13] = (coordShift & 31u) + ((uint32_t)input.transformShift << 8);
  out.controls[14] = (uint32_t)cr14;
  out.controls[15] = (uint32_t)wrapSub(affine.t[2], 512u << (coordShift & 31u));

  const uint16_t vertexScale = (uint16_t)((input.header & 0xff00u) >> 2);
  const bool paired = vertexScale != 0;
  const bool clipMode = (int32_t)input.header < 0;
  const auto primary = decode(input.primary, input.vertexCount, input.streamShift);
  if (primary.status != actor_model_codec::StreamStatus::Ok) {
    out.status = Status::Stream;
    return out;
  }
  actor_model_codec::StreamResult alternate{};
  if (paired) {
    alternate = decode(input.alternate, input.vertexCount, input.streamShift);
    if (alternate.status != actor_model_codec::StreamStatus::Ok) {
      out.status = Status::Stream;
      return out;
    }
  }
  out.vertices.reserve(input.vertexCount);
  uint32_t commonStatus = 0xffffffffu;
  for (uint32_t i = 0; i < input.vertexCount; ++i) {
    const auto a = primary.vertices[i];
    const auto b = paired ? alternate.vertices[i] : actor_model_codec::Vec3i{};
    actor_model_codec::Vec3i resolved = a;
    if (paired) {
      const auto blend = actor_model_codec::blendPose(a, b, (int16_t)vertexScale);
      resolved = {blend.mac[0], blend.mac[1], blend.mac[2]};
    }
    const auto packed = projectionInput(resolved);
    const auto projected = psxport::native_projection::project(affine, input.projection, packed);
    const uint32_t sxy = packedSxy(projected);
    const uint32_t scratchWord = clipMode ? clipStatusWord(sxy) : sxy;
    commonStatus &= scratchWord;
    out.vertices.push_back({a, b, resolved, packed, projected, scratchWord});
  }
  out.commonStatus = clipMode ? commonStatus & 31u : 0u;
  if (out.commonStatus != 0) {
    out.status = Status::VisibilityRejected;
    return out;
  }

  if (input.colorArm == ColorArm::High) {
    out.colors = input.primaryColors;
  } else {
    if (input.primaryColors.size() != input.secondaryColors.size()) {
      out.status = Status::ColorCount;
      out.vertices.clear();
      return out;
    }
    const int16_t factor = (int16_t)((1024 - input.cr30) * 4);
    out.colors.reserve(input.primaryColors.size());
    for (size_t i = 0; i < input.primaryColors.size(); ++i) {
      out.colors.push_back(actor_model_codec::depthCueRgb(
                               input.primaryColors[i], farColor(input.secondaryColors[i]), factor)
                               .rgb);
    }
  }
  out.primitiveWords = input.primitiveWords;
  out.status = Status::Ok;
  return out;
}

CallBoundary classifyCall(std::span<const Output> records) {
  CallBoundary result{};
  result.records = (uint32_t)records.size();
  if (records.empty()) {
    return result;
  }
  for (const Output &record : records) {
    if (record.status == Status::Ok) {
      ++result.visibleRecords;
    } else if (record.status == Status::VisibilityRejected) {
      ++result.rejectedRecords;
    } else {
      ++result.unsupportedRecords;
    }
  }
  result.status = result.unsupportedRecords == 0 ? CallStatus::Owned : CallStatus::Unsupported;
  return result;
}

CompareResult compareOutputs(const Output &expected, const Output &actual) {
  CompareResult result{};
  auto mismatch = [&](bool different, const char *field) {
    if (!different) {
      return;
    }
    ++result.mismatches;
    if (result.firstField == std::string_view{"none"}) {
      result.firstField = field;
    }
  };
  mismatch(expected.status != actual.status, "status");
  for (size_t i = 0; i < expected.controls.size(); ++i) {
    mismatch(expected.controls[i] != actual.controls[i], "control");
  }
  mismatch(expected.vertices.size() != actual.vertices.size(), "vertex_count");
  result.vertices = (uint32_t)std::min(expected.vertices.size(), actual.vertices.size());
  for (size_t i = 0; i < result.vertices; ++i) {
    const Vertex &a = expected.vertices[i], &b = actual.vertices[i];
    mismatch(a.primary.x != b.primary.x || a.primary.y != b.primary.y || a.primary.z != b.primary.z,
             "primary_vertex");
    mismatch(a.alternate.x != b.alternate.x || a.alternate.y != b.alternate.y ||
                 a.alternate.z != b.alternate.z,
             "alternate_vertex");
    mismatch(a.resolved.x != b.resolved.x || a.resolved.y != b.resolved.y ||
                 a.resolved.z != b.resolved.z,
             "resolved_vertex");
    mismatch(a.projectionInput.x != b.projectionInput.x ||
                 a.projectionInput.y != b.projectionInput.y ||
                 a.projectionInput.z != b.projectionInput.z,
             "projection_input");
    mismatch(a.projected.raw_view_fixed != b.projected.raw_view_fixed, "raw_view_fixed");
    mismatch(a.projected.ir != b.projected.ir, "ir");
    mismatch(a.projected.sx != b.projected.sx || a.projected.sy != b.projected.sy, "sxy");
    mismatch(a.projected.sz != b.projected.sz, "sz");
    mismatch(a.scratchWord != b.scratchWord, "scratch_word");
  }
  mismatch(expected.commonStatus != actual.commonStatus, "common_status");
  mismatch(expected.colors.size() != actual.colors.size(), "color_count");
  result.colors = (uint32_t)std::min(expected.colors.size(), actual.colors.size());
  for (size_t i = 0; i < result.colors; ++i) {
    mismatch(expected.colors[i] != actual.colors[i], "color");
  }
  mismatch(expected.primitiveWords.size() != actual.primitiveWords.size(), "primitive_count");
  result.primitiveWords =
      (uint32_t)std::min(expected.primitiveWords.size(), actual.primitiveWords.size());
  for (size_t i = 0; i < result.primitiveWords; ++i) {
    mismatch(expected.primitiveWords[i] != actual.primitiveWords[i], "primitive");
  }
  return result;
}

} // namespace spyro::actor_prefix
