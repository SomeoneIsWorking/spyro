#include "actor_stage.h"

#include <cstdlib>
#include <lucent/log.h>

namespace spyro::actor_stage {

const char *name(Emit stage) {
  return name(promote(stage));
}

const char *name(Temporal stage) {
  switch (stage) {
  case Temporal::Ready:
    return "ready";
  case Temporal::ValidEmpty:
    return "valid-empty";
  case Temporal::NoEndpoints:
    return "no-endpoints";
  case Temporal::Recipe:
    return "recipe";
  case Temporal::Submission:
    return "submission";
  case Temporal::DrawArea:
    return "draw-area";
  }
  return "unknown";
}

Temporal promote(Emit stage) {
  switch (stage) {
  case Emit::Ready:
    return Temporal::Ready;
  case Emit::ValidEmpty:
    return Temporal::ValidEmpty;
  case Emit::Recipe:
    return Temporal::Recipe;
  case Emit::Submission:
    return Temporal::Submission;
  case Emit::DrawArea:
    return Temporal::DrawArea;
  }
  // Every enumerator is covered above. A value outside them means a corpus carried a stage nothing
  // in this vocabulary produced, and reporting it as some other refusal would hide that.
  lucent::error("actorstage", "FATAL: unknown emit stage {}", (unsigned)stage);
  std::abort();
}

bool completed(Temporal stage) {
  return stage == Temporal::Ready || stage == Temporal::ValidEmpty;
}

bool completed(Emit stage) {
  return completed(promote(stage));
}

} // namespace spyro::actor_stage
