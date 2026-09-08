#include "temporal_scene.h"

#include "core.h"
#include "fx_paired_actor.h"
#include "render_queue.h"
#include "spyro_context.h"
#include "temporal_scene_source.h"

namespace {

bool ownsPairedActor(const RqItem &item) {
  return item.layer == RQ_WORLD && item.has_xyf && item.painter_object == 0x80023AC4u;
}

class SpyroTemporalScene final : public TemporalSceneSource {
public:
  bool eligible(const Core &core) const override {
    return spyro_context(core).pairedActor.temporal_eligible;
  }

  bool owns(const RqItem &item) const override {
    return ownsPairedActor(item);
  }

  void reconstruct(Core &core, float t) override {
    spyro_paired_actor_fps60_world_pass(&core, t);
  }

  void rotate(Core &core) override {
    spyro_paired_actor_fps60_rotate(&core);
  }
};

} // namespace

void spyro_temporal_scene_prepare(Core &core) {
  auto &paired = spyro_paired_actor_state(&core);
  paired.temporal_eligible = false;
  if (!paired.was_fps60_active || !paired.endpoints_compatible) {
    return;
  }
  // Eligibility belongs to the complete captured source. A valid endpoint may emit no faces
  // even though an interior presentation of the same source is visible.
  paired.temporal_eligible = spyro_paired_actor_fps60_eligible(paired);
}

std::unique_ptr<TemporalSceneSource> spyro_temporal_scene_source() {
  return std::make_unique<SpyroTemporalScene>();
}
