// The stages an actor face corpus passes through on its way to the render queue, and their names.
//
// WHY IT IS ITS OWN OWNER. Four owners on this route — the shared submission planner, the regular
// and secondary emit owners, and the two temporal sources — each reported WHICH stage declined, and
// each had grown the same enumerator list and the same switch turning it into a word. Two producers
// times two callers is four copies of one vocabulary, and a refusal reported as "submission" by one
// of them and "plan" by another is a refusal the log cannot be read across.
//
// TWO ENUMS, NOT ONE. A source that is preparing a corpus cannot be missing its endpoints, and a
// temporal source can be. Collapsing them would let a return type promise a state its function
// cannot produce, which is exactly what the named stages exist to prevent.
#pragma once

#include <cstdint>

namespace spyro::actor_stage {

// The stages any caller with a corpus in hand can reach. `Ready` published or is ready to publish;
// `ValidEmpty` completed with nothing to draw, which is a success. The rest are refusals, named for
// the stage that declined.
enum class Emit : uint8_t { Ready, ValidEmpty, Recipe, Submission, DrawArea };

// The same stages plus the one only a temporal source can reach: it was asked to reconstruct an
// in-between present without two consecutive endpoints to reconstruct it from.
enum class Temporal : uint8_t { Ready, ValidEmpty, NoEndpoints, Recipe, Submission, DrawArea };

const char *name(Emit stage);
const char *name(Temporal stage);

// The same stage, reported by a temporal source.
Temporal promote(Emit stage);

// The source completed: it either published a picture or correctly published nothing. Every other
// stage is a refusal. Written once because presentation, preflight and both producers all have to
// agree on where that line falls.
bool completed(Temporal stage);
bool completed(Emit stage);

} // namespace spyro::actor_stage
