#pragma once

#include "spyro_runtime.h"

namespace spyro1 {

// Runtime owner for SCUS_942.28.
class Spyro1Runtime final : public spyro::SpyroRuntime {
public:
  Spyro1Runtime();

  void *createContext(Core &core) override;
  void destroyContext(void *context) override;
  void registerOverrides(Game &game) override;
  void bootInit(Core &core) override;
  std::unique_ptr<FrameDriver> createFrameDriver(Game &game) override;
  RenderCapabilities renderCapabilities() const override {
    return RenderCapabilities::interpolatedNative();
  }
  bool guestVramIsPicture(const Game &game) const override;
  void pacePresentation(Core &core, int fields, int parts) override;
  const char *discEnvVar() const override {
    return "PSXPORT_SPYRO_DISC";
  }
  const GuestPadBufferLayout *guestPadBufferLayout() const override;
  std::unique_ptr<TemporalFramePresentation> createTemporalFramePresentation(Game &game) override;
  const PlatformHlePlan *platformHlePlan() const override;

private:
  static const GuestProgramImage programImage_;
};

} // namespace spyro1
