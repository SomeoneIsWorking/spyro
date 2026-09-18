#pragma once

#include "actor_draw_recipe.h"
#include "actor_face_submitter.h"
#include "actor_recipe_capture.h"

#include <cstdint>
#include <span>
#include <vector>

class Core;
struct RenderQueue;

namespace spyro::actor_emit {

enum class Status : uint8_t { Ready, ValidEmpty, Recipe, Submission, DrawArea };

// Named so a refusal reports WHICH stage declined rather than a bare enum value.
const char *statusName(Status status);

// Everything one regular-actor record corpus needs in order to reach a queue, resolved without
// touching either the queue or guest state.
//
// The split exists because the logic-frame producer owns guest-side bookkeeping — the shadow list
// its scene preparation staged — and that commit has to sit between the last refusal and the first
// queue mutation. A caller that owns no guest state, such as the temporal reconstruction, simply
// publishes straight after preparing.
struct Prepared {
  Status status = Status::ValidEmpty;
  std::vector<actor_prefix::Output> outputs;
  actor_draw_recipe::Recipe recipe;
  actor_face_submitter::Plan plan;
};

Prepared prepare(const Core &core,
                 const RenderQueue &queue,
                 uint32_t producerKey,
                 std::span<const actor_recipe_capture::Record> records);

// Publishes a Ready plan under the producer's scope. ValidEmpty publishes nothing and is not a
// failure: a corpus whose every face was culled still completed. Any other status is a programming
// error at the call site, which must have refused before reaching here.
void publish(Core &core,
             RenderQueue &queue,
             uint32_t producerKey,
             const char *producerName,
             const Prepared &prepared);

} // namespace spyro::actor_emit
