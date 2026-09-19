#include "terrain_scene.h"

#include "core.h"
#include "game.h"
#include "gpu_vk.h"
#include "wide_clip_plan.h"

#include <vector>

namespace spyro::terrain_scene {
namespace {

// The guest's terrain object list: a count and base pointer, plus a table of per-region index
// lists.
constexpr uint32_t kObjectListSelector = 0x80078A40u;
// The primitive pool the guest's own renderer would have written these faces into.
constexpr uint32_t kPoolCursor = 0x800757B0u;
constexpr uint32_t kPoolLimit = 0x80075780u;
// The walk refuses rather than running forever if the guest's index list has no terminator.
constexpr uint32_t kObjectListGuard = 4096u;

bool inRam(uint32_t address, uint32_t bytes) {
  const uint32_t offset = address & 0x1FFFFFFFu;
  return (address < 0x00200000u || (address >= 0x80000000u && address < 0x80200000u)) &&
         offset <= 0x200000u && bytes <= 0x200000u - offset;
}

Capture refuse(const char *why) {
  return {.status = Status::Refused, .refusal = why, .input = {}};
}

psxport::native_projection::FixedAffine affineFromWords(const std::array<uint32_t, 5> &words) {
  // The guest loads a SHORTMATRIX with a zero translation, which is why an object's world position
  // reaches its vertices through the origin in its header rather than through this matrix.
  return {.m = psxport::native_projection::rotationFromControlWords(words), .t = {}};
}

// The objects the guest's own list resolves that also clear their own visibility depth.
bool collectVisible(Core &core,
                    int32_t selector,
                    const psxport::native_projection::FixedAffine &cull,
                    std::vector<uint32_t> &visible,
                    const char *&refusal) {
  const uint32_t listBase = core.mem_r32(kObjectListSelector + 4);
  uint32_t cursor = 0;
  uint32_t end = 0;
  if (selector < 0) {
    const uint32_t count = core.mem_r32(kObjectListSelector);
    cursor = listBase;
    end = listBase + (count << 2);
  } else {
    const uint32_t table = core.mem_r32(kObjectListSelector + 12);
    if (!inRam(table + (uint32_t)selector * 4u, 4)) {
      refusal = "selector_bounds";
      return false;
    }
    cursor = core.mem_r32(table + (uint32_t)selector * 4u);
  }
  for (uint32_t step = 0; step < kObjectListGuard; ++step) {
    uint32_t object = 0;
    if (selector < 0) {
      if (cursor == end) {
        break;
      }
      if (!inRam(cursor, 4)) {
        refusal = "object_list_bounds";
        return false;
      }
      object = core.mem_r32(cursor);
      cursor += 4;
    } else {
      if (!inRam(cursor, 1)) {
        refusal = "object_index_bounds";
        return false;
      }
      const uint8_t index = core.mem_r8(cursor++);
      if (index == 255) {
        break;
      }
      if (!inRam(listBase + (uint32_t)index * 4u, 4)) {
        refusal = "object_index_target";
        return false;
      }
      object = core.mem_r32(listBase + (uint32_t)index * 4u);
    }
    if (!inRam(object, 24)) {
      refusal = "object_bounds";
      return false;
    }
    const uint32_t headerXY = core.mem_r32(object);
    const uint32_t headerZLimit = core.mem_r32(object + 4);
    const psxport::native_projection::ModelVertex point{
        .x = (int16_t)headerXY, .y = (int16_t)(headerXY >> 16), .z = (int16_t)(headerZLimit >> 16)};
    if (terrain_recipe::visible(cull, point, (int16_t)headerZLimit)) {
      visible.push_back(object);
    }
    if (selector < 0 && cursor == end) {
      break;
    }
    if (step == kObjectListGuard - 1u) {
      refusal = "object_list_unterminated";
      return false;
    }
  }
  return true;
}

// One object's vertices, face table and colour words. The vertex span is bounds-checked here
// because retail checks it for every object that survives the visibility test, before any
// projection; the face table and colour words are only flagged, for the reason in the header.
bool readObject(Core &core, uint32_t address, terrain_recipe::Object &out, const char *&refusal) {
  out.address = address;
  const uint32_t originXY = core.mem_r32(address + 8);
  const uint32_t meta = core.mem_r32(address + 12);
  const uint32_t faceMeta = core.mem_r32(address + 16);
  const int32_t originY = (int16_t)originXY;
  const int32_t originX = (int16_t)(originXY >> 16);
  const int32_t originZ = (int16_t)(meta >> 16);
  const uint32_t vertexCount = (meta & 0xFFFFu) + 1u;
  if (vertexCount >= 1024u || !inRam(address + 24, (vertexCount + 1u) * 4u)) {
    refusal = "vertex_span";
    return false;
  }
  out.vertices.reserve(vertexCount);
  for (uint32_t i = 0; i < vertexCount; ++i) {
    const uint32_t word = core.mem_r32(address + 24 + i * 4u);
    // X and Y arrive as unsigned offsets subtracted from the object's origin, packed into one word
    // so that the subtraction wraps through 16 bits exactly as the guest's own decode does.
    const uint32_t packed = (uint32_t)(originY - (int32_t)((word >> 10) & 0x7FFu)) +
                            ((uint32_t)(originX - (int32_t)(word & 0x3FFu)) << 16);
    out.vertices.push_back({.x = (int16_t)packed,
                            .y = (int16_t)(packed >> 16),
                            .z = (int16_t)((word >> 21) + (uint32_t)originZ)});
  }
  const uint32_t colourBase = address + 24 + (vertexCount - 1u) * 4u;
  const uint32_t faceBegin = colourBase + (faceMeta >> 14);
  const uint32_t faceBytes = (faceMeta << 3) & 0xFFF8u;
  out.faceTableInRam = inRam(faceBegin, faceBytes);
  if (!out.faceTableInRam) {
    return true;
  }
  out.faces.reserve(faceBytes / 8u);
  for (uint32_t at = faceBegin; at < faceBegin + faceBytes; at += 8) {
    const uint32_t indexWord = core.mem_r32(at);
    const uint32_t colourWord = core.mem_r32(at + 4);
    const std::array<uint32_t, 3> colour{
        colourWord >> 20, (colourWord >> 10) & 0x3FCu, colourWord & 0x3FCu};
    terrain_recipe::Object::Face face{};
    face.source = at;
    face.index = {indexWord >> 20, (indexWord >> 10) & 0x3FCu, indexWord & 0x3FCu};
    face.gouraud = !(colour[0] == colour[1] && colour[0] == colour[2]);
    for (size_t i = 0; i < colour.size(); ++i) {
      face.colourInRam = face.colourInRam && inRam(colourBase + colour[i], 4);
    }
    if (face.colourInRam) {
      for (size_t i = 0; i < colour.size(); ++i) {
        face.rgb[i] = core.mem_r32(colourBase + colour[i]);
      }
    }
    out.faces.push_back(face);
  }
  return true;
}

} // namespace

std::optional<std::array<uint32_t, 5>> matrixWords(Core &core, uint32_t address) {
  if (!inRam(address, 20)) {
    return std::nullopt;
  }
  std::array<uint32_t, 5> words{};
  for (uint32_t i = 0; i < words.size(); ++i) {
    words[i] = core.mem_r32(address + i * 4u);
  }
  return words;
}

Capture capture(Core &core,
                int32_t selector,
                const std::array<uint32_t, 5> &cullWords,
                const std::array<uint32_t, 5> &viewWords) {
  Capture out{};
  out.input.view = affineFromWords(viewWords);
  // The projection the guest set, widened at the one place that owns the extra horizontal area: the
  // screen centre moves with the wider viewport and the right clip edge moves with it.
  const bool wide = gpu_vk_wide_engine(&core);
  const int wideWidth = wide ? gpu_vk_wide_engine_w(&core) : 0;
  out.input.rightClip = wide ? wideWidth : wide::kNativeClipWidth;
  out.input.projection = {.ofx = wide ? (int32_t)((uint32_t)(wideWidth / 2) << 16)
                                      : (int32_t)gte_read_ctrl(24),
                          .ofy = (int32_t)gte_read_ctrl(25),
                          .h = (uint16_t)gte_read_ctrl(26)};
  out.input.poolCursor = core.mem_r32(kPoolCursor) + 4u;
  out.input.poolEnd = core.mem_r32(kPoolLimit) - 1024u;

  std::vector<uint32_t> visible;
  visible.reserve(256);
  const char *refusal = "none";
  if (!collectVisible(core, selector, affineFromWords(cullWords), visible, refusal)) {
    return refuse(refusal);
  }
  out.input.objects.reserve(visible.size());
  for (const uint32_t address : visible) {
    terrain_recipe::Object object{};
    if (!readObject(core, address, object, refusal)) {
      return refuse(refusal);
    }
    out.input.objects.push_back(std::move(object));
  }
  out.status = Status::Ready;
  return out;
}

} // namespace spyro::terrain_scene
