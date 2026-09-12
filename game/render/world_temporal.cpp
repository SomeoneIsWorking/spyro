#include "world_temporal.h"

#include "content_identity.h"
#include "core.h"
#include "fx_paired_actor.h"
#include "producer_scope.h"
#include "world_animation.h"
#include "world_chunk_codec.h"
#include "world_projection_math.h"
#include "world_scene_builder.h"
#include "world_scene_prepare.h"
#include "world_source_pair.h"

#include <algorithm>
#include <array>
#include <lucent/log.h>
#include <span>
#include <tuple>
#include <utility>

namespace spyro::world_temporal {
namespace {

std::string digestResource(const Core &core, GuestAddressRange range) {
  const std::span<const uint8_t> ram(core.ram);
  if (!range.valid() || range.end > ram.size()) {
    return {};
  }
  return sha256(ram.subspan(range.begin, range.end - range.begin));
}

bool resident(const Core &core, const Frame &frame) {
  for (const auto &resource : frame.resources) {
    if (core.currentImageIdentity(resource.range) != resource.image ||
        (!resource.contentDigest.empty() &&
         digestResource(core, resource.range) != resource.contentDigest)) {
      return false;
    }
  }
  return true;
}

bool sameResources(const Frame &a, const Frame &b) {
  if (a.resources.size() != b.resources.size()) {
    return false;
  }
  for (size_t i = 0; i < a.resources.size(); ++i) {
    const auto &left = a.resources[i];
    const auto &right = b.resources[i];
    if (left.range.begin != right.range.begin || left.range.end != right.range.end ||
        left.image != right.image || left.contentDigest != right.contentDigest) {
      return false;
    }
  }
  return true;
}

bool sameDrawPolicy(const world_scene_submitter::DrawState &a,
                    const world_scene_submitter::DrawState &b) {
  // Physical destinations may flip; viewport shape and material sampling policy may not.
  const auto policy = [](const world_scene_submitter::DrawState &draw) {
    return std::tuple{int64_t{draw.areaLeft} - draw.offsetX,
                      int64_t{draw.areaTop} - draw.offsetY,
                      int64_t{draw.areaRight} - draw.offsetX,
                      int64_t{draw.areaBottom} - draw.offsetY,
                      draw.windowMaskX,
                      draw.windowMaskY,
                      draw.windowOffsetX,
                      draw.windowOffsetY,
                      draw.dither,
                      draw.projectionH};
  };
  return policy(a) == policy(b);
}

bool cameraMatches(const Frame &world, const SpyroPairedFrame &paired) {
  const auto &camera = world.source.selection.camera;
  const SceneCameraInputs inputs{true, camera.projectionMatrix.m, camera.position};
  return world.serial != 0 && world.serial == paired.frameSerial &&
         inputs == paired.transform.sceneCamera;
}

void sortResources(std::vector<Resource> &resources) {
  std::sort(resources.begin(), resources.end(), [](const Resource &a, const Resource &b) {
    const auto order = [](const Resource &resource) {
      return std::tuple{resource.range.begin,
                        resource.range.end,
                        resource.image.has_value(),
                        resource.image ? resource.image->id : 0u,
                        resource.image ? resource.image->generation : 0u,
                        resource.contentDigest};
    };
    return order(a) < order(b);
  });
  resources.erase(std::unique(resources.begin(),
                              resources.end(),
                              [](const Resource &a, const Resource &b) {
                                return a.range.begin == b.range.begin &&
                                       a.range.end == b.range.end && a.image == b.image &&
                                       a.contentDigest == b.contentDigest;
                              }),
                  resources.end());
}

bool matchesCaptured(const Core &core,
                     const world_animation::Plan &plan,
                     const PendingChannel &captured) {
  if (plan.resources.size() != captured.resources.size()) {
    return false;
  }
  for (size_t i = 0; i < plan.resources.size(); ++i) {
    const auto &range = plan.resources[i];
    const auto &resource = captured.resources[i];
    if (range.begin != resource.range.begin || range.end != resource.range.end ||
        core.currentImageIdentity(range) != resource.image ||
        digestResource(core, range) != resource.contentDigest) {
      return false;
    }
  }
  return true;
}

bool applyAnimationPlan(Frame &frame,
                        const world_scene_prepare::AnimationSector &animation,
                        uint32_t channel,
                        const world_animation::Plan &plan,
                        const char *&why) {
  auto &slot = frame.source.selection.sectors[animation.index];
  if (!slot || slot->address != animation.address) {
    why = "animation_endpoint_sector";
    return false;
  }
  auto &sourceSector = frame.source.sectors[animation.index];
  if (!sourceSector) {
    why = "animation_endpoint_chunk";
    return false;
  }
  const auto &sector = *sourceSector;
  if (plan.channels != 1u) {
    why = "animation_endpoint_channels";
    return false;
  }
  size_t stamps = 0;
  for (const auto &write : plan.writes) {
    if (write.width == 1u) {
      ++stamps;
    } else if (write.width != 4u) {
      why = "animation_endpoint_width";
      return false;
    }
  }
  if (stamps != 1u) {
    why = "animation_endpoint_stamp";
    return false;
  }
  const bool low = channel < 2u;
  const world_chunk_codec::LowChunk *lowChunk =
      sector.lowStatus == world_chunk_codec::Status::Ok ? &sector.low : nullptr;
  const world_chunk_codec::HighChunk *highChunk =
      sector.highStatus == world_chunk_codec::Status::Ok ? &sector.high : nullptr;
  if ((low && !lowChunk) || (!low && !highChunk)) {
    why = "animation_endpoint_chunk";
    return false;
  }
  uint32_t base = 0;
  std::vector<uint32_t> *values = nullptr;
  if (channel == 0u) {
    base = lowChunk->address + 0x1cu;
    values = &sourceSector->low.vertices;
  } else if (channel == 1u) {
    base = lowChunk->address + 0x1cu + (uint32_t)lowChunk->vertices.size() * 4u;
    values = &sourceSector->low.colors;
  } else if (channel == 2u) {
    base = highChunk->address + 0x1cu + ((highChunk->layout >> 22) & 0x3fcu);
    values = &sourceSector->high.vertices;
  } else {
    const uint32_t layout = highChunk->layout;
    const uint32_t first =
        highChunk->address + 0x1cu + ((layout >> 22) & 0x3fcu) + ((layout << 2) & 0x3fcu);
    const uint32_t second = first + ((layout >> 6) & 0x3fcu);
    size_t wordIndex = 0;
    for (const auto &write : plan.writes) {
      if (write.width == 1u) {
        if (write.address != animation.address + 24u + channel) {
          why = "animation_endpoint_stamp";
          return false;
        }
        continue;
      }
      const uint32_t streamBase = (wordIndex++ & 1u) == 0u ? first : second;
      auto &stream =
          (wordIndex & 1u) == 1u ? sourceSector->high.farColors : sourceSector->high.nearColors;
      if (write.address < streamBase || write.address >= streamBase + stream.size() * 4u ||
          ((write.address - streamBase) & 3u)) {
        why = "animation_endpoint_destination";
        return false;
      }
      const size_t index = (write.address - streamBase) / 4u;
      if (index >= stream.size()) {
        why = "animation_endpoint_index";
        return false;
      }
      stream[index] = write.value;
    }
    return true;
  }
  for (const auto &write : plan.writes) {
    if (write.width == 1u) {
      if (write.address != animation.address + 24u + channel) {
        why = "animation_endpoint_stamp";
        return false;
      }
      continue;
    }
    if (write.address < base || ((write.address - base) & 3u)) {
      why = "animation_endpoint_destination";
      return false;
    }
    const size_t index = (write.address - base) / 4u;
    if (index >= values->size()) {
      why = "animation_endpoint_index";
      return false;
    }
    (*values)[index] = write.value;
  }
  return true;
}

} // namespace

void History::begin(uint64_t scene, bool reference, bool active) {
  ++serial_;
  eligible = false;
  current_.reset();
  seen_ = false;
  refused_ = false;
  const bool enabled = active && !reference;
  if (!enabled || enabled != active_ || scene != scene_) {
    previous_.reset();
  }
  scene_ = scene;
  active_ = enabled;
}

bool History::retain(const Core &core,
                     world_source::Source source,
                     world_scene_submitter::DrawState draw) {
  if (!active_) {
    return false;
  }
  if (seen_ || refused_ || !source.selection.valid || draw.areaLeft > draw.areaRight ||
      draw.areaTop > draw.areaBottom) {
    refuse();
    return false;
  }
  seen_ = true;
  std::vector<Resource> resources;
  for (const auto range : source.resourceRanges()) {
    const auto image = core.currentImageIdentity(range);
    if (!image) {
      lucent::debug("worldtemporal",
                    "REFUSED frame={} resource=[{:08X},{:08X}) has no complete residency",
                    serial_,
                    range.begin,
                    range.end);
      refuse();
      return false;
    }
    resources.push_back({range, *image});
  }
  std::vector<PendingChannel> pendingChannels;
  const world_chunk_codec::RamView ram(std::span<const uint8_t>(core.ram));
  std::array<bool, 256> visited{};
  for (const uint8_t sectorIndex : source.selection.occurrences) {
    if (visited[sectorIndex]) {
      continue;
    }
    visited[sectorIndex] = true;
    const auto &header = source.selection.sectors[sectorIndex];
    if (!header) {
      continue;
    }
    for (uint8_t channel = 0; channel < 4u; ++channel) {
      const uint8_t index = (uint8_t)(header->animation >> (channel * 8u));
      if (index >= 0x80u) {
        continue;
      }
      const uint32_t active =
          (0xffffffffu & ~(0xffu << (channel * 8u))) | ((uint32_t)index << (channel * 8u));
      world_animation::Plan plan{};
      const char *reason = nullptr;
      if (!world_animation::collectSectorResources(ram, header->address, active, plan, reason) ||
          plan.channels != 1u) {
        lucent::debug("worldtemporal",
                      "pending animation inputs unavailable frame={} sector={:08X} channel={} "
                      "reason={}",
                      serial_,
                      header->address,
                      channel,
                      reason ? reason : "plan_shape");
        continue; // A later-visible channel requires a complete retained descriptor to enter.
      }
      PendingChannel captured{sectorIndex, channel, {}};
      bool complete = true;
      for (const auto range : plan.resources) {
        const auto image = core.currentImageIdentity(range);
        const auto digest = digestResource(core, range);
        if (digest.empty()) {
          complete = false;
          break;
        }
        captured.resources.push_back({range, image, digest});
      }
      if (complete) {
        pendingChannels.push_back(std::move(captured));
      }
    }
  }
  current_ =
      Frame{std::move(source), draw, std::move(resources), std::move(pendingChannels), serial_};
  return true;
}

bool History::materializePending(Core &core, const char *&why) {
  if (!active_ || !previous_ || !current_ || refused_) {
    why = "missing consecutive world source";
    return false;
  }
  const world_projection_math::ProjectionStream culling(
      previous_->source.selection.camera.cullingMatrix,
      current_->source.selection.camera.cullingMatrix,
      {},
      0.5);
  world_scene_prepare::Prepared prepared{};
  why = "none";
  if (!world_scene_prepare::prepare(previous_->source.selection,
                                    current_->source.selection,
                                    culling,
                                    current_->source.clipRight,
                                    prepared,
                                    why,
                                    true)) {
    return false;
  }
  const world_chunk_codec::RamView ram(std::span<const uint8_t>(core.ram));
  if (!world_source_pair::compatible(previous_->source, current_->source, why)) {
    return false;
  }
  Frame staged = *previous_;
  std::vector<Resource> pendingResources;
  for (const auto &animation : prepared.animations) {
    auto &previousHeader = staged.source.selection.sectors[animation.index];
    if (!previousHeader || previousHeader->address != animation.address) {
      why = "animation_endpoint_sector";
      return false;
    }
    const uint32_t pending = previousHeader->animation;
    for (uint32_t channel = 0; channel < 4u; ++channel) {
      const uint8_t index = (uint8_t)(pending >> (channel * 8u));
      if (index >= 0x80u || (uint8_t)(animation.activeMask >> (channel * 8u)) == 0xffu) {
        continue;
      }
      const uint32_t active =
          0xffffffffu & ~(0xffu << (channel * 8u)) | ((uint32_t)index << (channel * 8u));
      lucent::debug("worldtemporal",
                    "materialize sector={:08X} channel={} index={} active={:08X}",
                    animation.address,
                    channel,
                    index,
                    active);
      world_animation::Plan plan{};
      if (!world_animation::appendSector(ram, animation.address, active, plan, why)) {
        return false;
      }
      const auto captured =
          std::find_if(previous_->pendingChannels.begin(),
                       previous_->pendingChannels.end(),
                       [&](const PendingChannel &candidate) {
                         return candidate.sector == animation.index && candidate.channel == channel;
                       });
      if (captured == previous_->pendingChannels.end()) {
        why = "animation_resource_unretained";
        return false;
      }
      if (!matchesCaptured(core, plan, *captured)) {
        why = "animation_resource_changed";
        return false;
      }
      if (!applyAnimationPlan(staged, animation, channel, plan, why)) {
        return false;
      }
      previousHeader->animation |= 0xffu << (channel * 8u);
      pendingResources.insert(
          pendingResources.end(), captured->resources.begin(), captured->resources.end());
    }
  }
  staged.resources.insert(staged.resources.end(), pendingResources.begin(), pendingResources.end());
  sortResources(staged.resources);
  current_->resources.insert(
      current_->resources.end(), pendingResources.begin(), pendingResources.end());
  sortResources(current_->resources);
  previous_->source = std::move(staged.source);
  previous_->resources = std::move(staged.resources);
  return true;
}

void History::refuse() {
  current_.reset();
  eligible = false;
  refused_ = true;
}

void History::rotate() {
  previous_ = std::move(current_);
  current_.reset();
  eligible = false;
}

bool History::compatible(const Core &core, const char *&why) const {
  if (!active_ || !previous_ || !current_ || refused_) {
    why = "missing consecutive world source";
    return false;
  }
  if (previous_->serial + 1 != current_->serial) {
    why = "nonconsecutive world frames";
    return false;
  }
  if (!sameResources(*previous_, *current_) || !resident(core, *previous_) ||
      !resident(core, *current_)) {
    why = "world resource residency changed";
    return false;
  }
  if (!sameDrawPolicy(previous_->draw, current_->draw)) {
    why = "world draw policy changed";
    return false;
  }
  return world_source_pair::compatible(previous_->source, current_->source, why);
}

bool History::camerasMatch(const SpyroPairedFrame &previous,
                           const SpyroPairedFrame &current) const {
  return previous_ && current_ && cameraMatches(*previous_, previous) &&
         cameraMatches(*current_, current);
}

bool History::emit(Core &core, RenderQueue &target, double t) const {
  const char *why = nullptr;
  if (!compatible(core, why)) {
    return false;
  }
  const auto recipe = world_scene::sample(previous_->source, current_->source, t);
  // Both endpoints and every interior sample draw into the CURRENT owned destination.
  const auto plan = world_scene_submitter::prepare(current_->draw, target, kProducerKey, recipe);
  lucent::debug(
      "worldtemporal",
      "world sample frame={} t={} recipe={} reason={} plan={} faces={} candidates={} rejected={}",
      serial_,
      t,
      static_cast<unsigned>(recipe.status),
      recipe.refusal,
      static_cast<unsigned>(plan.status),
      recipe.faces.size(),
      recipe.candidates,
      recipe.rejected);
  ProducerScope producer(&core.rsub.producerScope, kProducerKey, "world:static");
  return world_scene_submitter::emit(&core, target, kProducerKey, recipe, plan);
}

} // namespace spyro::world_temporal
