#include "secondary_actor_recipe.h"

#include <utility>

namespace spyro::secondary_actor_recipe {

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
    // Bit 2 reaches 0x80021C70..0x800227B0's view-normal/specular program.
    // It is distinct from the ordinary descriptor-colour stream below and
    // must be owned atomically rather than rendered with regular-actor colour.
    if (candidate.evaluation.emitted && (candidate.input.words[0] & 4u) != 0u) {
      recipe.status = Status::UnsupportedLighting;
      recipe.firstUnsupportedRecord = candidate.record;
      recipe.firstUnsupportedSourceWord = candidate.sourceWord;
      return recipe;
    }
  }

  recipe.faces = std::move(topology.faces);
  recipe.status = recipe.faces.empty() ? Status::ValidEmpty : Status::Ready;
  return recipe;
}

} // namespace spyro::secondary_actor_recipe
