#pragma once

#include "guest_cd_stream_callback_layout.h"
#include "guest_widescreen_owner.h"
#include "platform_hle.h"
#include "spyro3_widescreen_facts.h"
#include "spyro_runtime.h"

namespace spyro3 {

// Process-lifetime owner of SCUS_944.67's measured executable facts. It binds no Spyro 1
// compatibility state, context, or native overrides: its runtime executes the Spyro 3 executable
// directly, and the only shared owners it uses are title-neutral ones (the field owner, the archive
// transfer, the framework's CD seam).
class Spyro3Runtime final : public spyro::SpyroRuntime {
public:
  Spyro3Runtime();

  void *createContext(Core &core) override;
  void destroyContext(void *context) override;
  void registerOverrides(Game &game) override;
  void bootInit(Core &core) override;
  bool guestVramIsPicture(const Game &game) const override;
  void stockCdReadLanded(Core &core, const psx::cd::StockReadLanding &landing) override;

  // SCUS_944.67 now OWNS a native producer (the terrain world pass, spyro::makeTerrainWorldPass)
  // and a temporal product, so its capability is no longer widescreen-only. See the Spyro 2 runtime
  // for why the two titles' answers are the same shape and their facts are not.
  RenderCapabilities renderCapabilities() const override;
  std::unique_ptr<TemporalFramePresentation> createTemporalFramePresentation(Game &game) override;

  const PlatformHlePlan *platformHlePlan() const override;
  const char *discEnvVar() const override;
  const GuestCdStreamCallbackLayout *guestCdStreamCallbackLayout() const override;
  std::unique_ptr<FrameDriver> createFrameDriver(Game &game) override;

  // WHERE THIS DISC'S OWN LOGO IS, once it is measured in-product. See spyro3_logo_facts.h.
  const spyro::TitleLogoFacts &logoFacts() const override;
  void pacePresentation(Core &core, int fields, int parts) override;

  // THE TITLE'S ANSWER to the configured aspect, and the only place `gpu_vk_latch_guest_projection`
  // can learn one. Without it every plan resolves 4:3 whatever the settings file says.
  const GuestWidescreenProjection *guestWidescreenProjection() const override;

  [[nodiscard]] spyro::GuestWidescreenOwner &widescreen() {
    return widescreen_;
  }

private:
  static const GuestProgramImage programImage_;
  static const PlatformHlePlan platformHlePlan_;
  static const GuestCdStreamCallbackLayout cdStreamCallbackLayout_;

  // Process-lifetime, and reached from a native override through
  // `spyro::context(core).projectionHook` rather than through this member: an override is a plain
  // function pointer with nowhere to hang a back-pointer.
  spyro::GuestWidescreenOwner widescreen_{spyro3::kWidescreenFacts};
};

} // namespace spyro3
