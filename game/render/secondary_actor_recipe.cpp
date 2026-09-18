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
  case Status::UnsupportedLighting:
    return "UnsupportedLighting";
  }
  return "unknown";
}

Recipe derive(const secondary_actor_scene::Frame &frame) {
  Recipe recipe{};
  recipe.sourceRecords = (uint32_t)frame.records.size();
  if (frame.records.empty()) {
    return recipe;
  }
  recipe.outputs.reserve(frame.records.size());
  for (const auto &record : frame.records) {
    recipe.outputs.push_back(record.actor.expected);
  }

  auto topology = actor_draw_recipe::compose(recipe.outputs);
  recipe.candidates = topology.candidates;
  recipe.rejectedCandidates = topology.rejectedCandidates;
  if (topology.status == actor_draw_recipe::Status::ValidEmpty) {
    return recipe;
  }
  if (topology.status != actor_draw_recipe::Status::Ready) {
    recipe.status = Status::UnsupportedPrefix;
    recipe.firstReason = topology.firstReason;
    recipe.firstUnsupportedRecord = topology.firstUnsupportedRecord;
    recipe.firstUnsupportedSourceWord = topology.firstUnsupportedSourceWord;
    return recipe;
  }

  for (const auto &candidate : topology.candidateOrder) {
    // Bit 2 selects the distinct view-normal/specular program at 0x80021C70.
    // Base material colour does not reproduce that lighting contract.
    if (candidate.evaluation.emitted && (candidate.input.words[0] & 4u) != 0u) {
      recipe.status = Status::UnsupportedLighting;
      recipe.firstUnsupportedRecord = candidate.record;
      recipe.firstUnsupportedSourceWord = candidate.sourceWord;
      recipe.firstUnsupportedControl = candidate.input.words[0];
      return recipe;
    }
  }

  recipe.faces = std::move(topology.faces);
  recipe.status = recipe.faces.empty() ? Status::ValidEmpty : Status::Ready;
  return recipe;
}

} // namespace spyro::secondary_actor_recipe
