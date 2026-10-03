#pragma once

#include "paired_actor_decode.h"
#include "paired_actor_temporal_evidence.h"
#include "scene_camera_inputs.h"
#include <array>
#include <cstdint>
#include <vector>

class Core;
class RenderQueue;

namespace spyro::paired_actor {

// Guest renderer 0x80023AC4. It is the producer key every painter object this owner emits is
// tagged with, the identity the duplicate-endpoint refusal tests, and the guest function whose
// invocation count the ownership gate checks. Those are one fact, so it is stated once here
// instead of as a literal at each of those sites.
inline constexpr uint32_t kProducerAddress = 0x80023AC4u;

struct GpuSnapshot {
  int off_x = 0, off_y = 0, da_x0 = 0, da_y0 = 0, da_x1 = 0, da_y1 = 0;
  int tw_mx = 0, tw_my = 0, tw_ox = 0, tw_oy = 0;
};

struct Transform {
  spyro::SceneCameraInputs sceneCamera{};
  std::array<std::array<uint32_t, 8>, 3> layer_cr{};
  std::array<int32_t, 3> base_mac{};
  std::array<std::array<int32_t, 3>, 2> root_input{};
  uint32_t root_words[4]{};
  uint32_t ofx = 0, ofy = 0, h = 0;
  uint32_t depth_origin = 0;
  uint32_t depth_near = 0;
  uint32_t ot_control = 0;
  uint8_t ot_shift = 0;
  uint8_t depth_bias = 0;
};

struct Frame {
  bool valid = false;
  bool culled = false;
  uint64_t frameSerial = 0;
  uint64_t epoch = 0;
  uint64_t topology = 0;
  std::array<uint32_t, 3> layer_counts{};
  bool authored_replay = false;
  Transform transform{};
  std::vector<std::array<int32_t, 3>> pose;
  std::vector<Primitive> primitives;
  std::vector<uint32_t> materials;
  GpuSnapshot gpu{};
};

struct FrameState {
  uint32_t invocations = 0;
  uint32_t groups = 0;
  uint32_t candidates = 0;
  uint32_t faces = 0;
  uint32_t semiFaces = 0; // of `faces`, the ones drawn semi-transparent
  bool culled = false;
  const char *refusal = nullptr;
  Frame previous{};
  Frame current{};
  bool endpoints_compatible = false;
  bool temporal_eligible = false;
  bool was_state2 = false;
  uint64_t stage2_epoch = 0;
  bool was_fps60_active = false;
  temporal_evidence::Evidence temporal{};
  uint64_t parser_scanned = 0;
  uint64_t parser_normal = 0;
  uint64_t parser_faded = 0;
};

bool buildTransform(Core *c, Transform &out);

// Production normal opaque/textured arm of guest renderer 0x80023AC4. One live invocation each;
// what the temporal half does with the captured frame is `paired_actor_temporal.h`.
bool decodePose(Core *c);
bool submit(Core *c, FrameState &state);
void logFrameCompatibility(const Frame &previous, const Frame &current, bool compatible);
bool submitField(Core *c, FrameState &state);

enum class RebuildResult : uint8_t { Refused, NoOutput, Emitted };

// The per-Core owner of the two endpoints this producer fills. Held here, next to `FrameState`,
// because the producer and the temporal half are two views of one piece of state.
FrameState &state(Core *c);

// Hermetic checks for the shipping delta codec, /16 frame blend, projection and endpoint rules.
int selftest();

} // namespace spyro::paired_actor
