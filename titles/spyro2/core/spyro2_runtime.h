#pragma once

#include "guest_widescreen_owner.h"
#include "platform_hle.h"
#include "spyro2_frame_cut.h"
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

  // The guest's GP0 output replayed from the frame record, interpolated by keyed producers.
  RenderCapabilities renderCapabilities() const override;
  bool sealedFrameIsCut(Core &core) const override;

  const GuestPacketPoolWindows *guestPacketPoolWindows() const override;
  const PlatformHlePlan *platformHlePlan() const override;
  const char *discEnvVar() const override;
  const GuestCdStreamCallbackLayout *guestCdStreamCallbackLayout() const override;
  std::unique_ptr<FrameDriver> createFrameDriver(Game &game) override;

  // Where this disc's own logo is; see spyro2_logo_facts.h.
  const spyro::TitleLogoFacts &logoFacts() const override;
  void pacePresentation(Core &core, int fields, int parts) override;

  // THE TITLE'S ANSWER to the configured aspect, and the only place `gpu_vk_latch_guest_projection`
  // can learn one. A null answer makes the framework resolve `requested = Standard4x3`, so every
  // plan would be 4:3 whatever the settings file says.
  const GuestWidescreenProjection *guestWidescreenProjection() const override;

  [[nodiscard]] spyro::GuestWidescreenOwner &widescreen() {
    return widescreen_;
  }

private:
  static const GuestProgramImage programImage_;
  static const PlatformHlePlan platformHlePlan_;
  static const GuestCdStreamCallbackLayout cdStreamCallbackLayout_;
  static const GuestPacketPoolWindows packetPoolWindows_;

  // Process-lifetime, and reached from a native override through
  // `spyro::context(core).projectionHook` rather than through this member: an override is a plain
  // function pointer with nowhere to hang a back-pointer.
  spyro::GuestWidescreenOwner widescreen_{spyro2::kWidescreenFacts};
  FrameCut frameCut_;
};

} // namespace spyro2
