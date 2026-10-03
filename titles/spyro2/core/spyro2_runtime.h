#pragma once

#include "guest_widescreen_owner.h"
#include "platform_hle.h"
#include "spyro2_widescreen_facts.h"
#include "spyro_runtime.h"

#include <memory>

namespace spyro2 {

// Process-lifetime owner of SCUS_944.25's measured executable facts. It binds no Spyro 1
// compatibility state, context, or native overrides: its runtime executes the Spyro 2
// executable directly, and the only shared owners it uses are title-neutral ones (the field
// owner, the archive transfer, the framework's CD seam).
class Spyro2Runtime final : public spyro::SpyroRuntime {
public:
  Spyro2Runtime();

  void *createContext(Core &core) override;
  void destroyContext(void *context) override;
  void registerOverrides(Game &game) override;
  void bootInit(Core &core) override;
  bool guestVramIsPicture(const Game &game) const override;
  void stockCdReadLanded(Core &core, const psx::cd::StockReadLanding &landing) override;

  const PlatformHlePlan *platformHlePlan() const override;
  const char *discEnvVar() const override;
  const GuestCdStreamCallbackLayout *guestCdStreamCallbackLayout() const override;
  std::unique_ptr<FrameDriver> createFrameDriver(Game &game) override;

  // WHERE THIS DISC'S OWN LOGO IS, and whether the guest is in its attract demo — both measured,
  // both answered by this title and by no other. See spyro2_logo_facts.h and kAttractDemoMode
  // below.
  const spyro::TitleLogoFacts &logoFacts() const override;
  spyro::AttractState attractState(const Game &game, Core &core) const override;
  void pacePresentation(Core &core, int fields, int parts) override;

  // THE TITLE'S ANSWER to the configured aspect, and the only place `gpu_vk_latch_guest_projection`
  // can learn one. Before it existed this returned the base nullptr, so the framework resolved
  // `requested = Standard4x3` and every plan was 4:3 whatever the settings file said.
  const GuestWidescreenProjection *guestWidescreenProjection() const override;

  [[nodiscard]] spyro::GuestWidescreenOwner &widescreen() {
    return widescreen_;
  }

private:
  static const GuestProgramImage programImage_;
  static const PlatformHlePlan platformHlePlan_;
  static const GuestCdStreamCallbackLayout cdStreamCallbackLayout_;

  // Process-lifetime, and reached from a native override through
  // `spyro_context(core).projectionHook` rather than through this member: an override is a plain
  // function pointer with nowhere to hang a back-pointer.
  spyro::GuestWidescreenOwner widescreen_{spyro2::kWidescreenFacts};
};

} // namespace spyro2
