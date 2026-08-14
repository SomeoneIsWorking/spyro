#pragma once

#include <array>
#include <cstdint>

class Core;

struct SpyroPairedActorTransform {
  std::array<std::array<uint32_t, 8>, 3> layer_cr{};
  std::array<std::array<int32_t, 3>, 2> root_input{};
  uint32_t root_words[4]{};
  uint32_t ofx = 0, ofy = 0, h = 0;
};

bool spyro_paired_actor_build_transform(Core* c, SpyroPairedActorTransform& out);

// First ownership slice of guest renderer 0x80023AC4.  It resolves the three
// animation layers into host-side model-space vertices, but deliberately emits
// no faces yet.  False means the live actor/model data was structurally invalid.
bool spyro_paired_actor_decode_pose(Core* c);

// Diagnostic oracle: arm immediately before the guest render driver and finish
// after it returns. The callback resolves the native pose at the guest's first
// vertex RTPS, when all producer inputs are live.
void spyro_paired_actor_oracle_arm(Core* c);
bool spyro_paired_actor_oracle_finish(Core* c);

// Hermetic checks for the shipping delta codec and /16 frame blend.
int spyro_paired_actor_selftest();
