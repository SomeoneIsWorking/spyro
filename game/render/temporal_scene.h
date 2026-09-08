#pragma once

#include <cstdint>
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

private:
  std::unique_ptr<RenderQueue> sink_;
};
class TemporalSceneSource;

// Captures eligibility after the complete logic-frame scene has been produced. Unowned scene
// producers remain in the captured frame until they provide their own temporal source.
void spyro_temporal_scene_begin(
    Core &core, uint64_t scene, bool pairedScene, bool reference, bool active);
void spyro_temporal_scene_prepare(Core &core);
std::unique_ptr<TemporalSceneSource> spyro_temporal_scene_source(Game &game);
