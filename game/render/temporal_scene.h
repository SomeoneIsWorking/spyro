#pragma once

#include <memory>

class Core;
class TemporalSceneSource;

// Captures eligibility after the complete logic-frame scene has been produced. Unowned scene
// producers remain in the captured frame until they provide their own temporal source.
void spyro_temporal_scene_prepare(Core &core);
std::unique_ptr<TemporalSceneSource> spyro_temporal_scene_source();
