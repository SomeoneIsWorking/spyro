#include "terrain_recipe.h"

#include "projection_stream.h"
#include "wide_clip_plan.h"

#include <cstddef>

namespace spyro::terrain_recipe {
namespace {

// The guest tags a flat-shaded triangle's three colour words with the same primitive tag it would
// have written into the ordering table; the renderer wants the colour without it.
constexpr uint32_t kFlatColourTag = 0x10000000u;

Vertex asVertex(const psxport::native_projection::NativeProjectedVertex &result) {
  return {.sx = result.sx,
          .sy = result.sy,
          .screenX = result.px,
          .screenY = result.py,
          .viewZ = result.pz};
}

// The predecessor of `input.objects[index]`, or nullptr when there is none to sample against. A
// paired object must present the same mesh to the sampler: each endpoint's own model vertex goes
// through its own transform, so a differing vertex count has no correspondence to sample.
const Object *pairedEndpoint(const Interval *interval, size_t index, const Object &object) {
  if (interval == nullptr || index >= interval->previous.size()) {
    return nullptr;
  }
  const Object *const endpoint = interval->previous[index];
  if (endpoint == nullptr || endpoint->vertices.size() != object.vertices.size()) {
    return nullptr;
  }
  return endpoint;
}

// What an object's geometry was actually projected through. `Declined` is not `Own`: one means the
// object had no predecessor to sample against, the other that it had one and the framework refused
// the interval. A single "not sampled" count could not tell a rule that ran from one that never
// did.
enum class Projected : uint8_t { Own, Sampled, Declined, Unavailable };

Projected projectObject(const Object &object,
                        const Object *endpoint,
                        const Input &input,
                        const Interval *interval,
                        std::vector<Vertex> &projected) {
  projected.clear();
  projected.reserve(object.vertices.size());
  bool declined = false;
  if (endpoint != nullptr) {
    const ProjectionStream sampler(
        interval->previousView, input.view, input.projection, interval->t);
    bool complete = true;
    for (size_t i = 0; i < object.vertices.size() && complete; ++i) {
      const auto sampled = sampler.project(endpoint->vertices[i], object.vertices[i]);
      complete = sampled.has_value();
      if (complete) {
        projected.push_back(asVertex(*sampled));
      }
    }
    if (complete) {
      return Projected::Sampled;
    }
    declined = true;
    projected.clear();
  }
  const ProjectionStream own(input.view, input.projection);
  for (const auto &source : object.vertices) {
    const auto result = own.project(source, source);
    if (!result) {
      return Projected::Unavailable;
    }
    projected.push_back(asVertex(*result));
  }
  return declined ? Projected::Declined : Projected::Own;
}

Recipe refuse(Recipe recipe, Status status, const char *why) {
  recipe.status = status;
  recipe.refusal = why;
  recipe.faces.clear();
  return recipe;
}

} // namespace

const char *statusName(Status status) {
  switch (status) {
  case Status::Ready:
    return "Ready";
  case Status::ValidEmpty:
    return "ValidEmpty";
  case Status::InvalidInput:
    return "InvalidInput";
  case Status::PoolExhausted:
    return "PoolExhausted";
  }
  return "<unknown>";
}

bool visible(const psxport::native_projection::FixedAffine &cull,
             psxport::native_projection::ModelVertex point,
             int32_t limit) {
  const auto view = psxport::native_projection::transform(cull, point);
  const float depth = (float)view.raw_view_fixed[2] / 4096.0f;
  return (int32_t)((uint32_t)(int32_t)depth - (uint32_t)limit) > 0;
}

Recipe derive(const Input &input, const Interval *interval) {
  Recipe recipe{};
  if (input.rightClip <= 0) {
    return refuse(std::move(recipe), Status::InvalidInput, "clip_width");
  }
  uint32_t poolCursor = input.poolCursor;
  std::vector<Vertex> projected;
  std::vector<uint32_t> clips;
  for (size_t objectIndex = 0; objectIndex < input.objects.size(); ++objectIndex) {
    const Object &object = input.objects[objectIndex];
    ++recipe.objects;
    const Object *const endpoint = pairedEndpoint(interval, objectIndex, object);
    switch (projectObject(object, endpoint, input, interval, projected)) {
    case Projected::Sampled:
      ++recipe.sampled;
      break;
    case Projected::Declined:
      ++recipe.sampleDeclined;
      break;
    case Projected::Unavailable:
      return refuse(std::move(recipe), Status::InvalidInput, "endpoint_projection_unavailable");
    case Projected::Own:
      break;
    }
    clips.clear();
    clips.reserve(projected.size());
    uint32_t sharedClip = 0xFFFFFFFFu;
    for (const auto &vertex : projected) {
      const uint32_t clip = wide::clipCode(vertex.sx, vertex.sy, input.rightClip);
      clips.push_back(clip);
      sharedClip &= clip;
      ++recipe.vertices;
    }
    // Every vertex outside the same screen edge: retail drops the object without ever reading its
    // face table, so nothing about that table can refuse here either.
    if (sharedClip & 0xFu) {
      continue;
    }
    if (!object.faceTableInRam) {
      return refuse(std::move(recipe), Status::InvalidInput, "face_span");
    }
    for (const auto &face : object.faces) {
      ++recipe.candidates;
      std::array<uint32_t, 3> resolved{};
      for (size_t i = 0; i < resolved.size(); ++i) {
        if (face.index[i] & 3u) {
          return refuse(std::move(recipe), Status::InvalidInput, "vertex_index_alignment");
        }
        resolved[i] = face.index[i] / 4u;
        if (resolved[i] >= projected.size()) {
          // Native acceptance cannot depend on projected scratch from a guest renderer.
          return refuse(std::move(recipe), Status::InvalidInput, "external_vertex_source_unowned");
        }
      }
      if (clips[resolved[0]] & clips[resolved[1]] & clips[resolved[2]] & 0x1Fu) {
        ++recipe.rejects;
        continue;
      }
      const uint32_t stride = face.gouraud ? 28u : 20u;
      if ((int32_t)(input.poolEnd - poolCursor) <= 0) {
        return refuse(std::move(recipe), Status::PoolExhausted, "pool_exhaustion_equivalent");
      }
      poolCursor += stride;
      if (!face.colourInRam) {
        return refuse(std::move(recipe), Status::InvalidInput, "color_index");
      }
      Face out{};
      out.object = object.address;
      out.source = face.source;
      out.gouraud = face.gouraud;
      for (size_t i = 0; i < resolved.size(); ++i) {
        out.vertices[i] = projected[resolved[i]];
        out.rgb[i] = face.gouraud ? face.rgb[i] : face.rgb[i] - kFlatColourTag;
      }
      recipe.faces.push_back(out);
      if (face.gouraud) {
        ++recipe.g3;
      } else {
        ++recipe.f3;
      }
    }
  }
  recipe.status = recipe.faces.empty() ? Status::ValidEmpty : Status::Ready;
  return recipe;
}

} // namespace spyro::terrain_recipe
