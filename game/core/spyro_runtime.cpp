#include "spyro_runtime.h"

#include "core.h"
#include "paired_actor_temporal_evidence.h"
#include "render_stats.h" // render_depth_coverage_report
#include "title_logo.h"

namespace spyro {
namespace {

// No rectangle, so nothing is read and nothing is drawn.
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

std::optional<psx::host::LogoImage> SpyroRuntime::panelLogo(Core &core) const {
  return extractPanelLogo(core, logoFacts());
}

void SpyroRuntime::reportRun(Core &core) const {
  // These counters are incremented inside the framework's guest-OT classifier, which a run whose
  // producers are all guest code never executes; a zero here means "never ran", not "no coverage".
  render_depth_coverage_report(&core, "run-complete");
  core.rsub.census.report("spyro run-complete");
  paired_actor::temporal_evidence::finish(&core);
}

} // namespace spyro
