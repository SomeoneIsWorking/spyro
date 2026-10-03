#pragma once

class Core;

namespace spyro::render {
class SpriteQueueOffsetObserver;

// Native screen-class body of RasterizeSpritePrimQueue 0x80022A2C. Returns false when an actor or
// primitive variant has no native producer; the guest queue-exit GTE restore still occurs.
bool emitScreenQueue(Core &core, SpriteQueueOffsetObserver *observer = nullptr);

} // namespace spyro::render
