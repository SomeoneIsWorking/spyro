#pragma once

#include <chrono>

#include <cstdint>
#include <string_view>

class Core;
class Game;

namespace spyro {

// One display field's request. `present` crosses the presentation fence, `pace` waits for the host
// deadline.
struct FieldRequest {
  const char *site;
  bool present;
  bool pace;
};

// Title facts the shared field owner cannot know; a zero rootHandlerSlot means the title has no
// guest vblank root.
struct FieldOwnerFacts {
  const char *titleName = "";
  // Advanced once per delivered field when the guest has no root that does it; the title's own
  // copy, so a title that reads it sees delivered fields.
  std::uint32_t fieldCounter = 0;
  std::uint32_t rootHandlerSlot = 0;
  // The guest IRQ stack the vblank callbacks run on.
  std::uint32_t handlerStackTop = 0;
  std::uint32_t handlerStackBytes = 0;
  // A drawn iteration of the retail main loop spends at least this many; a boot step may
  // legitimately deliver fewer.
  std::uint32_t fieldsPerLogicFrame = 2;
};

// The shared owner runs every title-neutral effect of a field, then asks the title to observe it.
class FieldObserver {
public:
  virtual ~FieldObserver() = default;
  virtual void onField(Core &core, bool startEdge) = 0;
};

// The per-STEP title hook, between the guest's last work and the field being presented.
class FrameTailObserver {
public:
  virtual ~FrameTailObserver() = default;
  // The retail per-frame draw returned; its last act walked the ordering table it built.
  virtual void onFrameDrawn(Core &) {}
  virtual void onFrameTail(Core &core) = 0;
};

// Counts fields delivered during one logic iteration and states the minimum a product step must
// reach.
class FieldCadence {
public:
  void beginLogicFrame() {
    fields_ = 0;
  }

  void delivered() {
    ++fields_;
  }

  std::uint32_t fields() const {
    return fields_;
  }

  bool completesLogicFrame() const {
    return fields_ >= minimumFieldsPerProductStep_;
  }

  void setMinimumFieldsPerProductStep(std::uint32_t minimum) {
    minimumFieldsPerProductStep_ = minimum;
  }

  std::uint32_t minimumFieldsPerProductStep() const {
    return minimumFieldsPerProductStep_;
  }

private:
  std::uint32_t fields_ = 0;
  std::uint32_t minimumFieldsPerProductStep_ = 2;
};

// The one definition of "a display field happened": park the REPL, sample the pad, advance the
// display clock, run the guest vblank callbacks, advance the field counter, let the title observe,
// snapshot, cross the presentation fence when visible, service audio, release the host-turn token.
class FieldOwner {
public:
  // A title with nothing to observe per field passes no observer.
  FieldOwner(Game &game, FieldOwnerFacts facts, FieldObserver *observer = nullptr);

  // Publication reaches the shared context, so it is an explicit call rather than constructor
  // work that makes every owner's member order a hidden precondition.
  void publish();

  bool deliver(FieldRequest request);

  void beginLogicFrame() {
    cadence_.beginLogicFrame();
  }

  bool finishLogicFrame() const {
    return cadence_.completesLogicFrame();
  }

  std::uint32_t fieldsThisLogicFrame() const {
    return cadence_.fields();
  }

  FieldCadence &cadence() {
    return cadence_;
  }

  std::int32_t counter() const;
  std::string_view activeDeliverySite() const;
  void armHostClock();
  void observeVblankCallback(std::uint32_t function);

  // Whether the Start/Cross edge that ends a presentation-only hold is down now.
  bool presentationSkipPressed() const;

  std::uint64_t fields() const {
    return fields_;
  }

  std::uint64_t presents() const {
    return presents_;
  }

  std::uint64_t temporalDeferrals() const {
    return temporalDeferrals_;
  }

  const FieldOwnerFacts &facts() const {
    return facts_;
  }

  // The host-turn entry point: one more physical field, never a second presentation fence.
  static void hostTurnThunk(Core *core);

private:
  // True while a guest root owns its counter, including an IRQ deferred by masking or critical
  // state. Returns whether the guest root ticked the counter itself.
  bool dispatchCallbacks();
  void serviceRepl();
  void armHandlerStack(Core &core);
  void reportField(const FieldRequest &request, int queueSize, bool queueWasUnconsumed);

  Game &game_;
  FieldOwnerFacts facts_;
  FieldObserver *observer_;
  FieldCadence cadence_{};
  bool inField_ = false;
  const char *activeDeliverySite_ = nullptr;
  bool handlerStackArmed_ = false;
  bool hostClockArmed_ = false;
  bool replQuit_ = false;
  long replBudget_ = 0;
  std::uint64_t refused_ = 0;
  std::uint64_t fields_ = 0;
  std::uint64_t paces_ = 0;
  std::uint64_t presents_ = 0;
  // Visible fields a temporal product swallowed: the guest's ordering table was empty, so the
  // 60 Hz output's real/in-between pair already covered the field.
  std::uint64_t temporalDeferrals_ = 0;
  // Scene-producer tick as of the last delivered field, so a field can ask whether the guest's
  // native scene producers ran on IT rather than on an earlier field.
  std::uint32_t sceneTick_ = 0;

  std::uint32_t callbackFallback_ = 0;
  std::uint16_t previousButtons_ = 0xFFFFu;
  // Wall-clock epoch of this owner; a `pace` report needs milliseconds since the run began, and
  // two titles in one process would otherwise print each other's elapsed time.
  std::chrono::steady_clock::time_point epoch_ = std::chrono::steady_clock::now();
};

// The process-lifetime field owner of the selected title, published by its frame driver. A host
// turn can arrive before the driver exists, so this refuses by name instead of guessing.
FieldOwner &fieldOwner(Core &core);
const FieldOwner &fieldOwner(const Core &core);
// Null when nothing is published yet: the boot prefix issues its first CD read (the WAD header
// read at 0x8001253C) before the title's field owner exists, and a diagnostic there must be able to
// say "no field counter yet" instead of killing the process.
const FieldOwner *fieldOwnerIfPublished(const Core &core);

// One field delivered by a title-owned native tail, outside the boot and guest paths.
bool deliverNativeField(Core &core, const char *site, bool fps60CommitPending);

} // namespace spyro
