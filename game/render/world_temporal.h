#pragma once

#include "image_identity.h"
#include "scene_camera_inputs.h"
#include "world_scene_submitter.h"
#include "world_source.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class Core;
struct RenderQueue;
struct SpyroPairedFrame;

namespace spyro::world_temporal {

inline constexpr uint32_t kProducerKey = 0x800258f0u;

struct Resource {
  GuestAddressRange range;
  std::optional<psx::cpu::ImageIdentity> image;
  std::string contentDigest;
};

struct PendingChannel {
  uint8_t sector = 0;
  uint8_t channel = 0;
  std::vector<Resource> resources;
};

struct Frame {
  world_source::Source source;
  world_scene_submitter::DrawState draw;
  std::vector<Resource> resources;
  std::vector<PendingChannel> pendingChannels;
  uint64_t serial = 0;
};

// One owner for source lifetime, scene continuity and residency admission. Recipes are rebuilt
// for each sample; they are never retained as interpolation inputs.
class History {
public:
  void begin(uint64_t scene, bool reference, bool active);
  bool retain(const Core &core, world_source::Source source, world_scene_submitter::DrawState draw);
  bool materializePending(Core &core, const char *&why);
  void refuse();
  void rotate();
  bool compatible(const Core &core, const char *&why) const;
  bool camerasMatch(const SpyroPairedFrame &previous, const SpyroPairedFrame &current) const;
  bool emit(Core &core, RenderQueue &target, double t) const;
  uint64_t frameSerial() const {
    return serial_;
  }
  const Frame *previous() const {
    return previous_ ? &*previous_ : nullptr;
  }
  const Frame *current() const {
    return current_ ? &*current_ : nullptr;
  }
  bool eligible = false;

private:
  std::optional<Frame> previous_;
  std::optional<Frame> current_;
  uint64_t serial_ = 0;
  uint64_t scene_ = 0;
  bool active_ = false;
  bool seen_ = false;
  bool refused_ = false;
};

} // namespace spyro::world_temporal
