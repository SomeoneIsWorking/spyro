#include "secondary_actor_recipe.h"

#include <utility>

namespace spyro::secondary_actor_recipe {

const char *status_name(Status status) {
  switch (status) {
  case Status::Ready:
    return "Ready";
  case Status::ValidEmpty:
    return "ValidEmpty";
  case Status::UnsupportedPrefix:
    return "UnsupportedPrefix";
  case Status::UnsupportedTopology:
    return "UnsupportedTopology";
  }
  return "unknown";
}

Recipe derive(const secondary_actor_scene::Frame &frame, const face_light::Environment &lighting) {
  Recipe recipe{};
  recipe.sourceRecords = (uint32_t)frame.records.size();
  if (frame.records.empty()) {
    return recipe;
  }
  recipe.outputs.reserve(frame.records.size());
  for (const auto &record : frame.records) {
    recipe.outputs.push_back(record.actor.expected);
    // The prefix builder works from transform and stream state alone, so the word that selects the
    // per-face colour program is attached here, where the scene record still holds it.
    recipe.outputs.back().lightingControl = record.lightingControl;
  }

  auto topology = actor_draw_recipe::compose(recipe.outputs, lighting);
  recipe.candidates = topology.candidates;
  recipe.rejectedCandidates = topology.rejectedCandidates;
  if (topology.status == actor_draw_recipe::Status::ValidEmpty) {
    return recipe;
  }
  if (topology.status != actor_draw_recipe::Status::Ready) {
    recipe.status = topology.firstReason == actor_draw_recipe::Reason::Prefix
                        ? Status::UnsupportedPrefix
                        : Status::UnsupportedTopology;
    recipe.firstReason = topology.firstReason;
    recipe.firstUnsupportedRecord = topology.firstUnsupportedRecord;
    recipe.firstUnsupportedSourceWord = topology.firstUnsupportedSourceWord;
    if (!topology.candidateOrder.empty()) {
      const auto &last = topology.candidateOrder.back();
      recipe.firstUnsupportedControl = last.input.words[0];
      recipe.firstUnsupportedLighting = last.input.lightingControl;
      recipe.firstLightingStatus = last.input.lighting;
    }
    return recipe;
  }

  recipe.faceLightFaces = topology.faceLightFaces;
  recipe.faces = std::move(topology.faces);
  recipe.status = recipe.faces.empty() ? Status::ValidEmpty : Status::Ready;
  return recipe;
}

} // namespace spyro::secondary_actor_recipe
