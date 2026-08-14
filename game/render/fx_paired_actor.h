#pragma once

#include <array>
#include <cstdint>
#include <vector>
#include "paired_actor_decode.h"

class Core;
class RenderQueue;

struct SpyroPairedGpuSnapshot {
  int off_x=0,off_y=0,da_x0=0,da_y0=0,da_x1=0,da_y1=0;
  int tw_mx=0,tw_my=0,tw_ox=0,tw_oy=0;
};

struct SpyroPairedActorTransform {
  std::array<std::array<uint32_t, 8>, 3> layer_cr{};
  std::array<int32_t, 3> base_mac{};
  std::array<std::array<int32_t, 3>, 2> root_input{};
  uint32_t root_words[4]{};
  uint32_t ofx = 0, ofy = 0, h = 0;
  uint32_t depth_origin = 0;
  uint32_t depth_near = 0;
  uint32_t ot_control = 0;
  uint8_t ot_shift = 0;
};

struct SpyroPairedFrame {
  bool valid=false;
  bool culled=false;
  uint64_t epoch=0;
  uint64_t topology=0;
  std::array<uint32_t,3> layer_counts{};
  SpyroPairedActorTransform transform{};
  std::vector<std::array<int32_t,3>> pose;
  std::vector<spyro::paired_actor::Primitive> primitives;
  std::vector<uint32_t> materials;
  uint32_t override_control=0;
  SpyroPairedGpuSnapshot gpu{};
};

struct SpyroPairedActorFrameState {
  uint32_t invocations = 0;
  uint32_t groups = 0;
  uint32_t candidates = 0;
  uint32_t faces = 0;
  bool culled = false;
  const char* refusal = nullptr;
  SpyroPairedFrame previous{};
  SpyroPairedFrame current{};
  bool endpoints_compatible = false;
  bool was_state2 = false;
  uint64_t stage2_epoch = 0;
};

bool spyro_paired_actor_build_transform(Core* c, SpyroPairedActorTransform& out);

// Production normal opaque/textured arm of guest renderer 0x80023AC4.
bool spyro_paired_actor_decode_pose(Core* c);
bool spyro_paired_actor_submit(Core* c, SpyroPairedActorFrameState& state);
enum class SpyroPairedRebuildResult : uint8_t { Refused, NoOutput, Emitted };
SpyroPairedRebuildResult spyro_paired_actor_rebuild_endpoint(Core* c, RenderQueue& target,
                                                              const SpyroPairedFrame& frame);
void spyro_paired_actor_frame_begin(SpyroPairedActorFrameState& state,
                                    bool state2,bool reference_leg);
bool spyro_paired_actor_frame_finish(const SpyroPairedActorFrameState& state,
                                     bool reference_leg, bool expect_group);

// Diagnostic oracle: arm immediately before the guest render driver and finish
// after it returns. The callback resolves the native pose at the guest's first
// vertex RTPS, when all producer inputs are live.
void spyro_paired_actor_oracle_arm(Core* c);
bool spyro_paired_actor_oracle_finish(Core* c);

// Hermetic checks for the shipping delta codec and /16 frame blend.
int spyro_paired_actor_selftest();
