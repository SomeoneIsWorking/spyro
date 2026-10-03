#include "spyro_runtime.h"

namespace spyro {
namespace {

// A title with no measured logo facts answers with these: no rectangle, so nothing is read and
// nothing is drawn. The host shows a panel with no name, which is the truth about a disc whose
// logo has not been located yet.
const TitleLogoFacts kNoLogoFacts{};

} // namespace

SpyroRuntime::SpyroRuntime(const GuestProgramImage &programImage, SpyroTitle title)
    : programImage_(programImage), title_(title) {}

const GuestProgramImage *SpyroRuntime::guestProgramImage() const {
  return &programImage_;
}

SpyroTitle SpyroRuntime::title() const {
  return title_;
}

RenderCapabilities SpyroRuntime::renderCapabilities() const {
  return RenderCapabilities::widescreenOnly();
}

const TitleLogoFacts &SpyroRuntime::logoFacts() const {
  return kNoLogoFacts;
}

AttractState SpyroRuntime::attractState(const Game &, Core &) const {
  // No title has measured this yet, so the honest answer is Unknown: the host falls back to
  // freezing the panel on its first real picture, which is the behaviour from before any title
  // could say, and never a claim about a demo that is not running.
  return AttractState::Unknown;
}

} // namespace spyro
