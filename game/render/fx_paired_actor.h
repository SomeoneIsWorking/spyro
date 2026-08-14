#pragma once

#include <array>
#include <cstdint>

class Core;

struct SpyroPairedActorFrameState {
  uint32_t invocations = 0;
  uint32_t groups = 0;
  uint32_t candidates = 0;
  uint32_t faces = 0;
  bool culled = false;
  const char* refusal = nullptr;
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

bool spyro_paired_actor_build_transform(Core* c, SpyroPairedActorTransform& out);

// Production normal opaque/textured arm of guest renderer 0x80023AC4.
bool spyro_paired_actor_decode_pose(Core* c);
bool spyro_paired_actor_submit(Core* c, SpyroPairedActorFrameState& state);
void spyro_paired_actor_frame_begin(SpyroPairedActorFrameState& state);
bool spyro_paired_actor_frame_finish(const SpyroPairedActorFrameState& state,
                                     bool reference_leg, bool expect_group);

// Diagnostic oracle: arm immediately before the guest render driver and finish
// after it returns. The callback resolves the native pose at the guest's first
// vertex RTPS, when all producer inputs are live.
void spyro_paired_actor_oracle_arm(Core* c);
bool spyro_paired_actor_oracle_finish(Core* c);

// Hermetic checks for the shipping delta codec and /16 frame blend.
int spyro_paired_actor_selftest();
