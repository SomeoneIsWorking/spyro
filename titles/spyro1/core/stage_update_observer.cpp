#include "stage_update_observer.h"

#include "core.h"

#include <cstdlib>
#include <lucent/log.h>

namespace spyro1 {
namespace {

// Addresses and offsets from the authenticated Spyro 1 executable and external/spyro-1 symbols.
constexpr std::uint32_t kStage = 0x800757D8u;
constexpr std::uint32_t kLevelTick = 0x800758C8u;
constexpr std::uint32_t kGameTick = 0x8007572Cu;
constexpr std::uint32_t kCamera = 0x80076DD0u;
constexpr std::uint32_t kCameraPosition = kCamera + 0x28u;
constexpr std::uint32_t kCameraState = kCamera + 0x58u;
constexpr std::uint32_t kCameraTargetState = kCamera + 0xC0u;
constexpr std::uint32_t kCameraBlock = kCamera + 0xF0u;
constexpr std::uint32_t kSpyroPosition = 0x80078A58u;
constexpr std::uint32_t kCameraMode = 0x80075914u;
constexpr std::uint32_t kLookMode = 0x8007592Cu;
constexpr std::uint32_t kCameraEntranceTimer = 0x80075938u;
constexpr std::uint32_t kPlayerCameraGate = 0x80078BECu;
constexpr std::uint32_t kProjectionRtps = 0x4A180001u; // func_80017AA4 at 0x80017B20
constexpr std::uint32_t kUnreachableActor = 0xFFFFFFFCu;
constexpr std::uint32_t kObservedOfx100 = 100u << 16u;

std::array<std::int32_t, 3> position(Core &core, std::uint32_t address) {
  return {static_cast<std::int32_t>(core.mem_r32(address)),
          static_cast<std::int32_t>(core.mem_r32(address + 4u)),
          static_cast<std::int32_t>(core.mem_r32(address + 8u))};
}

} // namespace

StageUpdateObserver::StageUpdateObserver(bool enabled, std::uint32_t target)
    : enabled_(enabled), target_(target) {}

void StageUpdateObserver::beginSpriteQueue(Core &core, spyro::render::GteOffsetSample entry) {
  if (!enabled_) {
    return;
  }
  if (queueActive_) {
    lucent::error("stage-observe", "sprite queue observer entered twice without an exit");
    std::abort();
  }
  queueActive_ = true;
  activeQueue_ = {.ordinal = ++queueCalls_,
                  .stage = core.mem_r32(kStage),
                  .gameTick = core.mem_r32(kGameTick),
                  .entry = entry};
}

void StageUpdateObserver::spriteActorWrite(std::uint32_t actor,
                                           spyro::render::GteOffsetSample before,
                                           spyro::render::GteOffsetSample after) {
  if (!enabled_) {
    return;
  }
  if (!queueActive_) {
    lucent::error("stage-observe", "sprite actor OFX write occurred outside an observed queue");
    std::abort();
  }
  ++actorWrites_;
  ++activeQueue_.actorWrites;
  ofx100Writes_ += after.ofx == kObservedOfx100 ? 1u : 0u;
  sentinelHits_ += actor == kUnreachableActor ? 1u : 0u;
  activeQueue_.actor = actor;
  activeQueue_.actorBefore = before;
  activeQueue_.actorAfter = after;
}

void StageUpdateObserver::endSpriteQueue(spyro::render::GteOffsetSample exit) {
  if (!enabled_) {
    return;
  }
  if (!queueActive_) {
    lucent::error("stage-observe", "sprite queue observer exit has no entry");
    std::abort();
  }
  activeQueue_.exit = exit;
  lastQueue_ = activeQueue_;
  ++queueExits_;
  queueActive_ = false;
}

void StageUpdateObserver::beginStage(Core &core, std::uint32_t entry) {
  if (!enabled_) {
    return;
  }
  stageRtpsOps_ = 0;
  stageGteOps_ = 0;
  pendingProjectionOrdinal_ = 0;
  stageProjection_ = {};
  if (entry != target_) {
    return;
  }
  if (stageArmed_ || core.rsub.gtePreOp.armed()) {
    lucent::error("stage-observe",
                  "cannot arm camera projection: a GTE observer already owns this Core");
    std::abort();
  }
  gte_op_observer_arm(&core, beforeGte, afterGte, this);
  stageArmed_ = true;
}

void StageUpdateObserver::beforeGte(
    Core *core, std::uint64_t ordinal, std::uint32_t, std::uint32_t instruction, void *user) {
  auto &observer = *static_cast<StageUpdateObserver *>(user);
  if (instruction != kProjectionRtps) {
    return;
  }
  ++observer.stageRtpsOps_;
  if (core->mem_r32(kStage) != 0u || core->mem_r32(kCameraTargetState) != 0u) {
    return;
  }

  // func_80017AA4 loads this exact vector and camera rotation into GTE V0/CR0..4 before RTPS.
  // Lightrec does not currently supply an exact instruction PC to this callback, so require the
  // instruction AND its live operands, and refuse attribution unless exactly one candidate remains.
  const std::uint32_t x = core->mem_r32(kSpyroPosition) - core->mem_r32(kCameraPosition);
  const std::uint32_t y = core->mem_r32(kCameraPosition + 4u) - core->mem_r32(kSpyroPosition + 4u);
  const std::uint32_t z = core->mem_r32(kCameraPosition + 8u) - core->mem_r32(kSpyroPosition + 8u);
  const std::uint32_t packedYz = (y & 0xFFFFu) | ((z & 0xFFFFu) << 16u);
  // VZ0 is a signed 16-bit GTE input even though the guest computes the subtraction in 32 bits.
  const std::uint32_t gteX = static_cast<std::uint32_t>(static_cast<std::int16_t>(x & 0xFFFFu));
  if (gte_read_data(0) != packedYz || gte_read_data(1) != gteX) {
    return;
  }
  for (std::uint32_t registerIndex = 0; registerIndex < 5u; ++registerIndex) {
    const std::uint32_t mask = registerIndex == 4u ? 0xFFFFu : 0xFFFFFFFFu;
    if ((gte_read_ctrl(registerIndex) & mask) !=
        (core->mem_r32(kCamera + registerIndex * 4u) & mask)) {
      return;
    }
  }
  for (std::uint32_t registerIndex = 5u; registerIndex < 8u; ++registerIndex) {
    if (gte_read_ctrl(registerIndex) != 0u) {
      return;
    }
  }

  ++observer.stageProjection_.candidates;
  observer.pendingProjectionOrdinal_ = ordinal;
  if (observer.stageProjection_.candidates != 1u) {
    return;
  }
  observer.stageProjection_.targetState = core->mem_r32(kCameraTargetState);
  observer.stageProjection_.cameraMode = core->mem_r32(kCameraMode);
  observer.stageProjection_.lookMode = core->mem_r32(kLookMode);
  observer.stageProjection_.playerCameraGate = core->mem_r32(kPlayerCameraGate);
  observer.stageProjection_.cameraBlock = core->mem_r32(kCameraBlock);
  observer.stageProjection_.offset = {gte_read_ctrl(24u), gte_read_ctrl(25u)};
  observer.stageProjection_.h = gte_read_ctrl(26u);
  observer.stageProjection_.precedingQueue = observer.lastQueue_;
  observer.stageProjection_.precedingActorWrites = observer.actorWrites_;
  observer.stageProjection_.precedingOfx100Writes = observer.ofx100Writes_;
  observer.stageProjection_.precedingSentinelHits = observer.sentinelHits_;
}

void StageUpdateObserver::afterGte(
    Core *, std::uint64_t ordinal, std::uint32_t, std::uint32_t, void *user) {
  auto &observer = *static_cast<StageUpdateObserver *>(user);
  if (ordinal != observer.pendingProjectionOrdinal_) {
    return;
  }
  observer.pendingProjectionOrdinal_ = 0;
  if (observer.stageProjection_.candidates != 1u) {
    return;
  }
  const std::uint32_t screen = gte_read_data(14); // SXY2, read by func_80017AA4 after RTPS
  observer.stageProjection_.x = static_cast<std::int16_t>(screen & 0xFFFFu);
  observer.stageProjection_.y = static_cast<std::int16_t>(screen >> 16u);
  observer.stageProjection_.depth =
      gte_read_data(27); // MAC3, stored to the local projection triple
}

void StageUpdateObserver::afterReturn(Core &core, std::uint32_t entry) {
  if (!enabled_) {
    return;
  }
  if (stageArmed_) {
    stageGteOps_ = gte_preop_observer_disarm(&core);
    stageArmed_ = false;
  }
  gteOps_ += stageGteOps_;
  rtpsOps_ += stageRtpsOps_;
  projectionCandidates_ += stageProjection_.candidates;
  uniqueProjections_ += stageProjection_.candidates == 1u ? 1u : 0u;
  ++scanned_;
  if (entry != target_) {
    return;
  }
  ++matched_;
  if (core.mem_r32(kStage) != 0u) {
    return;
  }
  ++gameplay_;
  if (used_ == samples_.size()) {
    return;
  }
  samples_[used_++] = {
      .stage = 0u,
      .levelTick = core.mem_r32(kLevelTick),
      .gameTick = core.mem_r32(kGameTick),
      .cameraState = core.mem_r32(kCameraState),
      .cameraTargetState = core.mem_r32(kCameraTargetState),
      .cameraMode = core.mem_r32(kCameraMode),
      .lookMode = core.mem_r32(kLookMode),
      .playerCameraGate = core.mem_r32(kPlayerCameraGate),
      .cameraBlock = core.mem_r32(kCameraBlock),
      .cameraEntranceTimer = core.mem_r32(kCameraEntranceTimer),
      .gteOps = stageGteOps_,
      .rtpsOps = stageRtpsOps_,
      .projection = stageProjection_,
      .player = position(core, kSpyroPosition),
      .camera = position(core, kCameraPosition),
  };
}

void StageUpdateObserver::report() const {
  if (!enabled_) {
    return;
  }
  lucent::info(
      "stage-observe",
      "completed-return target=0x{:08X} scanned={} matched={} gameplay={} recorded={} omitted={} "
      "gte_ops={} rtps_ops={} projection_candidates={} unique_projections={}",
      target_,
      scanned_,
      matched_,
      gameplay_,
      used_,
      gameplay_ - used_,
      gteOps_,
      rtpsOps_,
      projectionCandidates_,
      uniqueProjections_);
  lucent::info("stage-observe",
               "sprite-queue entered={} exited={} actor_writes={} ofx100_writes={} "
               "sentinel_actor=0x{:08X} sentinel_hits={} last_queue={} active={}",
               queueCalls_,
               queueExits_,
               actorWrites_,
               ofx100Writes_,
               kUnreachableActor,
               sentinelHits_,
               lastQueue_.ordinal,
               queueActive_ ? 1 : 0);
  lucent::info("stage-observe",
               "sprite-queue-last ordinal={} stage={} game_tick={} actor=0x{:08X} writes={} "
               "entry=0x{:08X},0x{:08X} actor_before=0x{:08X},0x{:08X} "
               "actor_after=0x{:08X},0x{:08X} exit=0x{:08X},0x{:08X}",
               lastQueue_.ordinal,
               lastQueue_.stage,
               lastQueue_.gameTick,
               lastQueue_.actor,
               lastQueue_.actorWrites,
               lastQueue_.entry.ofx,
               lastQueue_.entry.ofy,
               lastQueue_.actorBefore.ofx,
               lastQueue_.actorBefore.ofy,
               lastQueue_.actorAfter.ofx,
               lastQueue_.actorAfter.ofy,
               lastQueue_.exit.ofx,
               lastQueue_.exit.ofy);
  for (std::size_t index = 0; index < used_; ++index) {
    const StageUpdateSample &sample = samples_[index];
    lucent::info("stage-observe",
                 "return={} stage={} level_tick={} game_tick={} camera_state=0x{:08X} "
                 "camera_target_state=0x{:08X} camera_mode=0x{:08X} look_mode={} "
                 "player_camera_gate={} camera_block={} camera_timer={} "
                 "player=({},{},{}) camera=({},{},{}) gte_ops={} rtps_ops={} "
                 "projection_candidates={} projection_valid={} projected=({},{},{}) "
                 "branch_target=0x{:08X} branch_mode=0x{:08X} branch_look={} "
                 "branch_player_gate={} branch_camera_block={} "
                 "rtps_ofx=0x{:08X} rtps_ofy=0x{:08X} rtps_h=0x{:08X} "
                 "preceding_queue={} queue_stage={} queue_tick={} queue_actor=0x{:08X} "
                 "queue_writes={} queue_entry=0x{:08X},0x{:08X} "
                 "actor_before=0x{:08X},0x{:08X} actor_after=0x{:08X},0x{:08X} "
                 "queue_exit=0x{:08X},0x{:08X} preceding_actor_writes={} "
                 "preceding_ofx100_writes={} preceding_sentinel_hits={}",
                 index,
                 sample.stage,
                 sample.levelTick,
                 sample.gameTick,
                 sample.cameraState,
                 sample.cameraTargetState,
                 sample.cameraMode,
                 sample.lookMode,
                 sample.playerCameraGate,
                 sample.cameraBlock,
                 sample.cameraEntranceTimer,
                 sample.player[0],
                 sample.player[1],
                 sample.player[2],
                 sample.camera[0],
                 sample.camera[1],
                 sample.camera[2],
                 sample.gteOps,
                 sample.rtpsOps,
                 sample.projection.candidates,
                 sample.projection.candidates == 1u ? 1 : 0,
                 sample.projection.x,
                 sample.projection.y,
                 sample.projection.depth,
                 sample.projection.targetState,
                 sample.projection.cameraMode,
                 sample.projection.lookMode,
                 sample.projection.playerCameraGate,
                 sample.projection.cameraBlock,
                 sample.projection.offset.ofx,
                 sample.projection.offset.ofy,
                 sample.projection.h,
                 sample.projection.precedingQueue.ordinal,
                 sample.projection.precedingQueue.stage,
                 sample.projection.precedingQueue.gameTick,
                 sample.projection.precedingQueue.actor,
                 sample.projection.precedingQueue.actorWrites,
                 sample.projection.precedingQueue.entry.ofx,
                 sample.projection.precedingQueue.entry.ofy,
                 sample.projection.precedingQueue.actorBefore.ofx,
                 sample.projection.precedingQueue.actorBefore.ofy,
                 sample.projection.precedingQueue.actorAfter.ofx,
                 sample.projection.precedingQueue.actorAfter.ofy,
                 sample.projection.precedingQueue.exit.ofx,
                 sample.projection.precedingQueue.exit.ofy,
                 sample.projection.precedingActorWrites,
                 sample.projection.precedingOfx100Writes,
                 sample.projection.precedingSentinelHits);
  }
}

} // namespace spyro1
