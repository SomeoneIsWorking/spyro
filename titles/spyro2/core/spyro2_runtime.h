#pragma once

#include "platform_hle.h"
#include "spyro_runtime.h"

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

  const PlatformHlePlan *platformHlePlan() const override;
  const char *discEnvVar() const override;
  const GuestCdStreamCallbackLayout *guestCdStreamCallbackLayout() const override;
  std::unique_ptr<FrameDriver> createFrameDriver(Game &game) override;
  void pacePresentation(Core &core, int fields, int parts) override;

private:
  static const GuestProgramImage programImage_;
  static const PlatformHlePlan platformHlePlan_;
  static const GuestCdStreamCallbackLayout cdStreamCallbackLayout_;
};

} // namespace spyro2
