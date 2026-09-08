#pragma once

#include "fx_paired_actor.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

class Core;

namespace spyro::paired_actor {

constexpr int kLayers = 3;

struct Vec3i {
  int32_t x = 0;
  int32_t y = 0;
  int32_t z = 0;
};

struct StreamDesc {
  uint32_t model = 0;
  uint32_t base = 0;
  uint32_t bytes = 0;
  uint32_t shorts = 0;
  uint32_t frameWord = 0;
  uint32_t keyframe = 0;
  uint32_t count = 0;
  bool shortFirst = false;
};

struct LayerDesc {
  StreamDesc a;
  StreamDesc b;
  uint8_t blend = 0;
  bool hasB = false;
};

struct LayerPose {
  std::vector<Vec3i> vertices;
};

struct PairedPose {
  std::array<LayerPose, kLayers> layers;
};

Vec3i unpack_accum(uint32_t packed);
Vec3i blend16(Vec3i a, Vec3i b, uint8_t blend);
uint32_t keyframe_ptr(uint32_t frameWord);
Vec3i rtps_input(Vec3i v);
std::array<int32_t, 3> packed_root_input(const std::array<int32_t, 3> &root);

bool make_stream(Core *c, uint8_t anim, uint8_t frame, int layer, StreamDesc &out);
bool decode_stream(Core *c, const StreamDesc &d, std::vector<Vec3i> &out);
bool build_descs(Core *c, std::array<LayerDesc, kLayers> &desc);
bool decode_pose(Core *c,
                 const std::array<LayerDesc, kLayers> &desc,
                 PairedPose &pose,
                 std::array<uint32_t, kLayers> &decoded);
bool build_transform(Core *c, SpyroPairedActorTransform &out);

} // namespace spyro::paired_actor
