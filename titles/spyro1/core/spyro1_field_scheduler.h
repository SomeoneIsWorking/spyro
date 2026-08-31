#pragma once

#include "spyro1_frame_policy.h"

#include <cstdint>

class Core;
class Game;

namespace spyro1 {

struct FieldRequest {
  const char *site;
  bool present;
  bool pace;
  bool acknowledgeHostTurn;
};

// The sole Spyro 1 definition of one 60 Hz display field. Native boot, frame tails, and host turns
// call it directly; guest VSync is a mandatory fatal trap and never reaches this owner.
class FieldScheduler {
public:
  explicit FieldScheduler(Game &game);

  bool deliver(const FieldRequest &request);
  void beginLogicFrame();
  bool finishLogicFrame() const;
  std::uint32_t fieldsThisLogicFrame() const;

  void bootSequenceBegin();
  void bootSequenceEnd();
  void armHostClock();
  void observeVblankCallback(std::uint32_t function);
  void fps60CommitDelivered();

  std::int32_t counter() const;

private:
  bool dispatchCallbacks();
  void serviceInspection();
  void serviceSkipMap(bool startEdge);
  void reportField(const FieldRequest &request, int queueSize, bool queueWasUnconsumed);

  Game &game_;
  bool bootSequenceActive_ = false;
  FieldCadence cadence_{};
  bool inField_ = false;
  bool handlerStackArmed_ = false;
  bool hostClockArmed_ = false;
  bool replQuit_ = false;
  long replBudget_ = 0;
  std::uint64_t refused_ = 0;
  std::uint64_t fields_ = 0;
  std::uint64_t paces_ = 0;
  std::uint64_t presents_ = 0;
  std::uint64_t acknowledgements_ = 0;
  std::uint64_t queueFirstConsumers_ = 0;
  std::uint32_t callbackFallback_ = 0;
  std::uint32_t deepestHandlerStack_ = 0x8000E000u;
  std::uint16_t previousButtons_ = 0xFFFFu;
  std::uint32_t skipMapFields_ = 0;
  std::uint32_t skipMapBootFields_ = 0;
  std::uint32_t skipMapStageFields_ = 0;
  std::uint32_t skipMapStartEdges_ = 0;
  std::uint32_t previousStage_ = ~0u;
  std::uint32_t previousSubstate_ = ~0u;
  std::uint32_t previousSubSubstate_ = ~0u;
  std::uint32_t previousBootPhase_ = ~0u;
  bool previousBootActive_ = false;
};

FieldScheduler &fieldScheduler(Core &core);
const FieldScheduler &fieldScheduler(const Core &core);

bool deliverNativeField(Core &core, const char *site, bool fps60CommitPending);
void acknowledgeTemporalCommit(Core &core);
void beginBootSequence(Core &core);
void endBootSequence(Core &core);
void observeVblankCallback(Core &core, std::uint32_t function);
void hostTurn(Core *core);

} // namespace spyro1
