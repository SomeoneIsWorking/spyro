#include "actor_draw_recipe.h"

#include "actor_billboard_face.h"

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

// The three halfwords retail's projection loop stores at 0x800212C0: the RTPS MAC1..3 outputs,
// truncated on their way into memory.
face_light::ViewVertex view_vertex(const psxport::native_projection::NativeProjectedVertex &v) {
  return {(int16_t)(uint16_t)(uint32_t)(int32_t)(v.raw_view_fixed[0] >> 12),
          (int16_t)(uint16_t)(uint32_t)(int32_t)(v.raw_view_fixed[1] >> 12),
          (int16_t)(uint16_t)(uint32_t)(int32_t)(v.raw_view_fixed[2] >> 12)};
}

// Why a primitive could not be populated, in enough detail to act on.
struct Malformation {
  Reason reason = Reason::None;
  uint32_t slot = 0;   // the vertex or colour index within the primitive
  uint32_t offset = 0; // the byte offset its stream asked for
  uint32_t limit = 0;  // the size of the array that offset ran off
};

bool populate(const actor_prefix::Output &record,
              uint32_t sourceWord,
              const face_light::Environment &lighting,
              PrimitiveInput &out,
              Malformation &why) {
  // The caller's own `while (source < record.primitiveWords.size())` is what makes this subtraction
  // safe; a guard here could never fire, so there is none to mistake for a reachable refusal.
  const size_t available = record.primitiveWords.size() - sourceWord;
  for (size_t i = 0; i < std::min(available, out.words.size()); ++i) {
    out.words[i] = record.primitiveWords[sourceWord + i];
  }
  const bool quad = (int32_t)out.words[0] < 0;
  // A quad with bit 2 is the billboard arm: five words, and ONE real vertex. Reading four would
  // decode three offsets the arm never uses, which can refuse a face retail draws.
  const bool billboard = quad && (out.words[0] & 4u) != 0u;
  const unsigned count = billboard ? 1u : (quad ? 4u : 3u);
  const unsigned requiredWords =
      billboard ? 5u : (quad ? ((out.words[0] & 2u) ? 6u : 3u) : ((out.words[0] & 2u) ? 5u : 2u));
  if (available < requiredWords) {
    why = {Reason::ShortPrimitive, 0, requiredWords, (uint32_t)available};
    return false;
  }
  out.depthOrigin = record.depthOrigin;
  out.shift = record.otShift;
  out.fog = record.fog;
  for (unsigned i = 0; i < count; ++i) {
    const uint32_t offset = i == 3 ? out.words[2] & 0x7fcu : vertexOffset(out.words[0], i);
    if ((offset & 3u) || offset / 4u >= record.vertices.size()) {
      why = {Reason::VertexOffset, i, offset, (uint32_t)record.vertices.size()};
      return false;
    }
    const auto &vertex = record.vertices[offset / 4u];
    out.status[i] = vertex.scratchWord;
    out.xy[i] = (uint16_t)vertex.projected.sx | ((uint32_t)(uint16_t)vertex.projected.sy << 16);
    out.depth[i] = vertex.projected.sz;
    out.screenX[i] = vertex.projected.px;
    out.screenY[i] = vertex.projected.py;
    out.viewZ[i] = vertex.projected.pz;
    out.view[i] = view_vertex(vertex.projected);
  }
  // The billboard reads ONE colour, at the same offset the other arms use for their first vertex.
  // Decoding the other three refused a face retail draws: record 5 of the attract demo's first
  // crowded scene reported `color-offset slot=1 offset=424 limit=1`, describing a colour array this
  // arm never indexes — the other three offsets are where its half-extents live.
  if (billboard) {
    const uint32_t offset = (out.words[1] >> 17) & 0x7fcu;
    if ((offset & 3u) || offset / 4u >= record.colors.size()) {
      why = {Reason::ColorOffset, 0, offset, (uint32_t)record.colors.size()};
      return false;
    }
    const auto &centre = record.vertices[vertexOffset(out.words[0], 0) / 4u];
    const auto box = actor_billboard::extents(record.projection, centre.projected, out.words[1]);
    const int32_t xs[4] = {box.left, box.right, box.left, box.right};
    const int32_t ys[4] = {box.top, box.top, box.bottom, box.bottom};
    const uint32_t rgb = record.colors[offset / 4u] & 0x00ffffffu;
    for (unsigned i = 0; i < 4; ++i) {
      out.xy[i] = (uint32_t)(uint16_t)(int16_t)xs[i] | ((uint32_t)(uint16_t)(int16_t)ys[i] << 16);
      out.screenX[i] = (float)xs[i];
      out.screenY[i] = (float)ys[i];
      // One sprite stands at one distance, so every corner shares the centre's view depth.
      out.viewZ[i] = centre.projected.pz;
      out.color[i] = rgb;
    }
    out.lightingControl = record.lightingControl;
    return true;
  }
  {
    const uint32_t material = out.words[1];
    const uint32_t offsets[] = {(material >> 17) & 0x7fcu,
                                (material >> 8) & 0x7fcu,
                                (material << 1) & 0x7fcu,
                                (out.words[2] >> 9) & 0x7fcu};
    for (unsigned i = 0; i < count; ++i) {
      if ((offsets[i] & 3u) || offsets[i] / 4u >= record.colors.size()) {
        why = {Reason::ColorOffset, i, offsets[i], (uint32_t)record.colors.size()};
        return false;
      }
      out.color[i] = record.colors[offsets[i] / 4u];
    }
    out.color[0] &= 0x00ffffffu;
  }
  out.lightingControl = record.lightingControl;
  // Bit 2 on a triangle replaces all three material colours with one computed term. On a quad the
  // same bit means the separate billboard program at 0x8002256C, which evaluate() refuses as Ft4.
  if (!quad && (out.words[0] & 4u) != 0u) {
    const auto lit = face_light::face_color(
        {out.view[0], out.view[1], out.view[2]}, out.lightingControl, lighting);
    out.lighting = lit.status;
    if (lit.status == face_light::Status::Ready) {
      out.color[0] = lit.color;
      out.color[1] = lit.color;
      out.color[2] = lit.color;
    }
  }
  return true;
}

std::vector<uint32_t> payload(const PrimitiveInput &s, Family family, bool second) {
  const uint32_t semiCommandBit = (s.words[1] & 1u) << 25;
  switch (family) {
  case Family::G4:
    return {0x08000000u,
            s.color[0] + 0x38000000u + semiCommandBit,
            s.xy[0],
            s.color[1],
            s.xy[1],
            s.color[2],
            s.xy[2],
            s.color[3],
            s.xy[3]};
  case Family::Billboard:
    // Ten words, tag length 9: a POLY_FT4 whose one colour is the material's single table entry and
    // whose three UV words follow the material in the stream, the last of them reused twice.
    return {0x09000000u,
            s.color[0] + 0x2c000000u + semiCommandBit,
            s.xy[0],
            s.words[2],
            s.xy[1],
            s.words[3],
            s.xy[2],
            s.words[4],
            s.xy[3],
            s.words[4] >> 16};
  case Family::GT4:
    return {0x0c000000u,
            s.color[0] + 0x3c000000u + semiCommandBit,
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
            (s.color[a] & 0x00ffffffu) + 0x30000000u + semiCommandBit,
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
            (s.color[a] & 0x00ffffffu) + 0x34000000u + semiCommandBit,
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

const char *statusName(Status status) {
  switch (status) {
  case Status::NoCorpus:
    return "no-corpus";
  case Status::Ready:
    return "ready";
  case Status::ValidEmpty:
    return "valid-empty";
  case Status::Unsupported:
    return "unsupported";
  }
  return "unknown";
}

const char *reasonName(Reason reason) {
  switch (reason) {
  case Reason::None:
    return "none";
  case Reason::Outcode:
    return "outcode";
  case Reason::Skip:
    return "skip";
  case Reason::Nclip:
    return "nclip";
  case Reason::ZeroArea:
    return "zero-area";
  case Reason::Depth:
    return "depth";
  case Reason::Ft4:
    return "ft4";
  case Reason::ShortPrimitive:
    return "short-primitive";
  case Reason::VertexOffset:
    return "vertex-offset";
  case Reason::ColorOffset:
    return "color-offset";
  case Reason::NextWord:
    return "next-word";
  case Reason::BinRange:
    return "bin-range";
  case Reason::Prefix:
    return "prefix";
  case Reason::FaceLight:
    return "face-light";
  }
  return "unknown";
}

Evaluation evaluate(const PrimitiveInput &s) {
  Evaluation out{};
  const uint32_t control = s.words[0], material = s.words[1];
  const bool quad = (int32_t)control < 0, textured = (control & 2u) != 0;
  out.nextWord = quad ? (textured ? 6u : 3u) : (textured ? 5u : 2u);
  if (quad && (control & 4u)) {
    out.nextWord = 5u;
    // The arm tests only its own vertex, and it tests it before anything else — there is no skip
    // bit, no NCLIP and no material depth bias on this path.
    if ((int32_t)s.shift < 0 && (s.status[0] & 31u) != 0u) {
      out.reason = Reason::Outcode;
      return out;
    }
    // One vertex where the other arms sum four, so the depth is scaled to the same range before
    // the shared origin is removed. Retail drops the sprite when that lands at or behind zero.
    const uint32_t depth = ((uint32_t)s.depth[0] << 2) + 4u - s.depthOrigin;
    if ((int32_t)depth <= 0) {
      out.reason = Reason::Depth;
      return out;
    }
    const uint32_t bin = (uint32_t)sar(depth, s.shift);
    if ((int32_t)bin < 0) {
      out.reason = Reason::Depth;
      return out;
    }
    out.family = Family::Billboard;
    out.origin = Origin::FullQuad;
    out.localBin = bin;
    out.payload = payload(s, Family::Billboard, false);
    out.emitted = true;
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
  // Retail reaches its two per-face colour programs only after culling, at 0x80021C0C, so a face
  // that never survives to be drawn never needs one and must not refuse the call on its behalf.
  if (!quad && (control & 4u) != 0u && s.lighting != face_light::Status::Ready) {
    out.supported = false;
    out.reason = Reason::FaceLight;
    return out;
  }
  out.family = full ? (textured ? Family::GT4 : Family::G4) : (textured ? Family::GT3 : Family::G3);
  out.localBin = q;
  out.payload = payload(s, out.family, second);
  out.emitted = true;
  return out;
}

Recipe compose(std::span<const actor_prefix::Output> records,
               const face_light::Environment &lighting) {
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
      Malformation malformed{};
      if (!populate(record, source, lighting, input, malformed)) {
        recipe.status = Status::Unsupported;
        recipe.firstReason = malformed.reason;
        recipe.firstUnsupportedRecord = recordIndex;
        recipe.firstUnsupportedSourceWord = source;
        // `populate` fills the words it read before it refuses, so the refusal carries the stream
        // it was looking at rather than the zeroes an unset field would print.
        recipe.firstUnsupportedWords = {input.words[0], input.words[1]};
        recipe.firstUnsupportedSlot = malformed.slot;
        recipe.firstUnsupportedOffset = malformed.offset;
        recipe.firstUnsupportedLimit = malformed.limit;
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
                                                                         : Reason::NextWord;
        recipe.firstUnsupportedRecord = recordIndex;
        recipe.firstUnsupportedSourceWord = source;
        recipe.firstUnsupportedWords = {input.words[0], input.words[1]};
        recipe.faces.clear();
        return recipe;
      }
      if (result.emitted) {
        if ((int32_t)input.words[0] >= 0 && (input.words[0] & 4u) != 0u) {
          ++recipe.faceLightFaces;
        }
        recipe.faces.push_back({recordIndex,
                                record.moby,
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
