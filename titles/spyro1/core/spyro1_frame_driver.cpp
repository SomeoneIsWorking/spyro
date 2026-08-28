#include "spyro1_frame_driver.h"

#include "cfg.h"
#include "core.h"
#include "frame_env.h"
#include "game.h"
#include "guest_call.h"
#include "guest_gp.h"
#include "render.h"

#include <algorithm>
#include <cstdlib>
#include <lucent/log.h>
#include <memory>

namespace spyro1 {
namespace {

constexpr std::uint32_t kStaticConstructors = 0x8005B988u;
constexpr std::uint32_t kFrameUpdate = 0x8003385Cu;

constexpr std::uint32_t kInputLatchOpen = kGp + 0x604u;
constexpr std::uint32_t kVblanksThisFrame = kGp + 0x4FCu;
constexpr std::uint32_t kFrameStep = kGp + 0x468u;
constexpr std::uint32_t kRenderSuppressed = kGp + 0x538u;
constexpr std::int32_t kFrameStepMin = 2;
constexpr std::int32_t kFrameStepMax = 4;

class FrameState {
public:
  explicit FrameState(Core &core) : core_(core) {}

  void closeInputLatch() {
    core_.mem_w8(kInputLatchOpen, 0);
  }
  void openInputLatch() {
    core_.mem_w8(kInputLatchOpen, 1);
  }
  std::int32_t elapsedFields() const {
    return static_cast<std::int32_t>(core_.mem_r32(kVblanksThisFrame));
  }
  void restartFieldCount() {
    core_.mem_w32(kVblanksThisFrame, 0);
  }
  void setFrameStep(std::int32_t step) {
    core_.mem_w32(kFrameStep, static_cast<std::uint32_t>(step));
  }
  bool renderSuppressed() const {
    return core_.mem_r32(kRenderSuppressed) != 0;
  }

private:
  Core &core_;
};

} // namespace

Spyro1FrameDriver::Spyro1FrameDriver(Game &game)
    : fields_(game), boot_(fields_), renderer_(std::make_unique<SpyroRenderer>(&game.core)) {}

Spyro1FrameDriver::~Spyro1FrameDriver() = default;

void Spyro1FrameDriver::initialize(Core &core) {
  SpyroRenderer::installModeFromConfig(&core);
  rc0(&core, kStaticConstructors);
  boot_.initialize(core);
  lucent::info("frameloop",
               "Spyro1FrameDriver initialized; guest main 0x{:08X} and boot body 0x{:08X} are "
               "not dispatched",
               0x80012204u,
               0x800127C0u);
}

void Spyro1FrameDriver::stepFrame(Core &core, std::uint32_t) {
  if (!boot_.complete() && !boot_.step(core)) {
    return;
  }

  const std::uint32_t frame = ++gameplayFrame_;
  FrameState state(core);
  fields_.beginLogicFrame();

  core.game->timing.logicFrame = frame;
  core.rsub.otAttr.beginLogicFrame(frame);
  state.closeInputLatch();
  rc0(&core, kFrameUpdate);
  state.openInputLatch();
  state.setFrameStep(std::clamp(state.elapsedFields(), kFrameStepMin, kFrameStepMax));
  const bool suppressed = state.renderSuppressed();
  state.restartFieldCount();
  // Gameplay probes intentionally keep the logic/field path alive while the native picture is still
  // missing a stage-0 producer. This does not alter the product default: it presents the previous
  // guest VRAM frame, preserves the retail two-field logic quota, and makes control-state
  // measurements independent of the rendering backlog.
  const bool gameplayProbe = cfg_on("PSXPORT_GAMEPLAY_PROBE") != 0;
  if (!suppressed && !gameplayProbe) {
    renderer_->drawFrame();
  } else {
    const char *site = gameplayProbe ? "gameplay-probe" : "render-suppressed";
    for (std::uint32_t field = 0; field < kFieldsPerLogicFrame; ++field) {
      // Match frame_commit's two-field fence: the first field advances the guest/audio clock
      // without presenting, and the second owns the single visible presentation for this logic
      // iteration. Presenting both fields would violate FrameDriver's one-fence-per-step contract.
      const bool deferPresentation = field + 1u < kFieldsPerLogicFrame;
      if (!deliverNativeField(core, site, deferPresentation)) {
        lucent::error("frameloop",
                      "{} product step could not deliver its field {} of {}",
                      site,
                      field + 1,
                      kFieldsPerLogicFrame);
        std::abort();
      }
    }
  }

  if (!fields_.finishLogicFrame()) {
    lucent::error("frameloop",
                  "product step {} completed without reaching a host-owned field boundary "
                  "(delivered={})",
                  frame,
                  fields_.fieldsThisLogicFrame());
    std::abort();
  }
}

FieldScheduler &Spyro1FrameDriver::fields() {
  return fields_;
}

const FieldScheduler &Spyro1FrameDriver::fields() const {
  return fields_;
}

Spyro1FrameDriver &frameDriver(Core &core) {
  if (core.game == nullptr || core.game->frameDriver == nullptr) {
    lucent::error("frameloop", "Spyro 1 runtime has no FrameDriver");
    std::abort();
  }
  return static_cast<Spyro1FrameDriver &>(*core.game->frameDriver);
}

const Spyro1FrameDriver &frameDriver(const Core &core) {
  if (core.game == nullptr || core.game->frameDriver == nullptr) {
    lucent::error("frameloop", "Spyro 1 runtime has no FrameDriver");
    std::abort();
  }
  return static_cast<const Spyro1FrameDriver &>(*core.game->frameDriver);
}

} // namespace spyro1
