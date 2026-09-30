#pragma once

#include <chrono>

#include <cstdint>
#include <string_view>

class Core;
class Game;

namespace spyro {

// One display field's request. `present` crosses the framework's single presentation fence and
// `pace` waits for the host deadline; a field can be either, both, or neither, exactly as the
// retail tail needs.
struct FieldRequest {
  const char *site;
  bool present;
  bool pace;
};

// The title facts the shared field owner cannot know. Everything here is measured from the
// title's own executable; a zero `rootHandlerSlot` means the title has no guest vblank root of
// its own and the host owns the counter outright.
struct FieldOwnerFacts {
  const char *titleName = "";
  // The guest word the host advances once per delivered field when the guest has no root that
  // does it. The title's own copy, so a title that reads it sees delivered fields.
  std::uint32_t fieldCounter = 0;
  // The guest word holding this title's IRQ root, or 0.
  std::uint32_t rootHandlerSlot = 0;
  // The guest IRQ stack the vblank callbacks run on.
  std::uint32_t handlerStackTop = 0;
  std::uint32_t handlerStackBytes = 0;
  // Fields one product step must span. A drawn iteration of the retail main loop spends at
  // least this many; a boot step may legitimately deliver fewer.
  std::uint32_t fieldsPerLogicFrame = 2;
};

// The per-field title hook. The shared owner runs every title-neutral effect of a field and
// then asks the title to observe it, so a title's own state map is a small derived read rather
// than a second copy of the delivery sequence.
class FieldObserver {
public:
  virtual ~FieldObserver() = default;
  virtual void onField(Core &core, bool startEdge) = 0;
};

// Counts fields delivered during one logic iteration and states the minimum a product step
// must reach. The guest's own frame tail compares the previous field stamp and has the same
// minimum; the host scheduler validates that native and diagnostic paths preserve it.
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

// The one definition of "a display field happened" in this product, shared by every title in
// the lineage. Native boot, guest frame tails, native frame tails and host turns all call it;
// guest libetc VSync is intercepted by the framework as a bounded frame-boundary exit and its
// body never runs.
//
// What one delivery is, in order: park the REPL, sample the host pad, advance the display
// clock, run this title's guest vblank callbacks, advance the field counter, let the title
// observe, snapshot, cross the presentation fence when the field is visible, service audio, and
// release the host-turn token. Every one of those is title-neutral; the only title-specific
// inputs are the measured addresses above and the observer.
class FieldOwner {
public:
  // A title with nothing to observe per field passes no observer; the delivery sequence
  // is identical either way.
  FieldOwner(Game &game, FieldOwnerFacts facts, FieldObserver *observer = nullptr);

  // Publish this owner as the per-Core one. It is an explicit call and not constructor work on
  // purpose: publication reaches the shared context, and a constructor that requires it makes every
  // owner's member ORDER part of a hidden precondition -- a fixture whose context is published in
  // its body, after the members are constructed, aborts inside the owner with "per-Core Spyro game
  // context missing", which is what happened to the Spyro 1 scheduler test.
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

  // Whether the Start/Cross edge that ends a presentation-only hold is down now. The pad
  // subsystem keeps exposing input to later title states unchanged.
  bool presentationSkipPressed() const;

  std::uint64_t fields() const {
    return fields_;
  }

  std::uint64_t presents() const {
    return presents_;
  }

  const FieldOwnerFacts &facts() const {
    return facts_;
  }

  // The host-turn entry point: one more physical field, never a second presentation fence.
  static void hostTurnThunk(Core *core);

private:
  // True while a guest root owns its counter, including an IRQ deferred by masking/critical
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
  std::uint32_t callbackFallback_ = 0;
  std::uint16_t previousButtons_ = 0xFFFFu;
  // Wall-clock epoch of this owner, taken in the constructor. A `pace` report needs milliseconds
  // since the run began, and that origin is per-owner state rather than a process-wide one: a
  // function-local static would make the first owner's epoch the only one any later owner could
  // ever report, and two titles in one process would print each other's elapsed time.
  std::chrono::steady_clock::time_point epoch_ = std::chrono::steady_clock::now();
};

// The process-lifetime field owner of the selected title, published by its frame driver. A
// host turn can arrive before the driver exists, so this refuses by name instead of guessing.
FieldOwner &fieldOwner(Core &core);
const FieldOwner &fieldOwner(const Core &core);

// One field delivered by a title-owned native tail, outside the boot and guest paths.
bool deliverNativeField(Core &core, const char *site, bool fps60CommitPending);

} // namespace spyro
