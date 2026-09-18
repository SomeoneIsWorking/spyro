#pragma once

#include <cstdint>
#include <functional>
#include <memory>

class Core;
class Game;
struct RenderQueue;

// Reusable isolated admission storage, one per game. Production source reconstruction and queue
// validation share this sink without allocating the full queue on every logic frame.
class SpyroTemporalSceneAdmission {
public:
  SpyroTemporalSceneAdmission();
  ~SpyroTemporalSceneAdmission();
  bool world(Core &core, bool paired);
  // The two actor intervals, preflighted the same way and for the same reason: a midpoint the
  // planner would refuse must be discovered before presentation depends on it, not during it. They
  // are admitted independently, so a refusal on one layer costs only that layer's in-between faces.
  bool actors(Core &core);
  bool secondaryActors(Core &core);

private:
  // Replays one source across the interval's endpoints and its midpoint into the isolated sink,
  // requiring each to survive the same painter planner presentation uses. `emit` reports whether
  // the source produced that sample at all.
  bool
  interval(Core &core, const char *label, const std::function<bool(RenderQueue &, float)> &emit);

  std::unique_ptr<RenderQueue> sink_;
};
class TemporalSceneSource;

// Captures eligibility after the complete logic-frame scene has been produced. Unowned scene
// producers remain in the captured frame until they provide their own temporal source.
void spyro_temporal_scene_begin(
    Core &core, uint64_t scene, bool pairedScene, bool reference, bool active);
void spyro_temporal_scene_prepare(Core &core);
std::unique_ptr<TemporalSceneSource> spyro_temporal_scene_source(Game &game);
