// Spyro's picture owner: native scenes compose producers and submit one render queue per frame.
#pragma once
#include "paired_actor.h"
#include <cstddef>
#include <cstdint>
class Core;

namespace spyro::render {

struct GteOffsetSample {
  std::uint32_t ofx = 0;
  std::uint32_t ofy = 0;
};

// Optional read-only sink for the native screen-sprite queue's GTE offset boundary.
class SpriteQueueOffsetObserver {
public:
  virtual ~SpriteQueueOffsetObserver() = default;
  virtual void beginSpriteQueue(Core &core, GteOffsetSample entry) = 0;
  virtual void
  spriteActorWrite(std::uint32_t actor, GteOffsetSample before, GteOffsetSample after) = 0;
  virtual void endSpriteQueue(GteOffsetSample exit) = 0;
};

// Stage selectors whose reached recipes have native owners.
inline constexpr std::uint32_t kStageField = 0u;
inline constexpr std::uint32_t kStageLevelTransition = 1u;
inline constexpr std::uint32_t kStagePauseMenu = 2u;
inline constexpr std::uint32_t kStageInventoryMenu = 3u;
inline constexpr std::uint32_t kStageRespawn = 4u;
inline constexpr std::uint32_t kStageGameOver = 5u;
inline constexpr std::uint32_t kStageOldDragon = 6u;
inline constexpr std::uint32_t kStageDragon = 8u;
// GS_EntranceAnimation. draw.c:2693 dispatches it to func_8001A050, the same producer as stage 1.
inline constexpr std::uint32_t kStageEntranceAnimation = 9u;
// GS_Fairy: draw.c:2696 dispatches it to func_8001D718, which fairy_menu_scene owns.
inline constexpr std::uint32_t kStageFairy = 11u;
inline constexpr std::uint32_t kStageFrontEnd = 13u;
inline constexpr std::uint32_t kStageCutscene = 14u;

// The guest's per-frame render driver, called once per drawn frame from its main() 0x80012204 at
// 0x8001227C: it resets the OT/packet pool, dispatches on the stage selector, and ends in the
// display tail. It is the reference path.
inline constexpr std::uint32_t kFrameRenderDrv = 0x8001ED5Cu;

// One arm of the guest's render driver 0x8001ED5C. `handler` is 0 when the arm dispatches
// indirectly or picks between two handlers.
struct StageArm {
  std::uint32_t stage;
  std::uint32_t handler;
  const char *what;
};

// One layer of the FIELD (stage 0) arm, in the guest's own draw order. `gate` is 0 when
// unconditional; otherwise the layer runs when `[gate]` is non-zero exactly when `gateNonZero`.
struct FieldLayer {
  std::uint32_t fn;
  std::uint32_t gate;
  bool gateNonZero;
  const char *what;
};

// The scene a renderer is being asked to produce. `arm` is null when the stage selector is outside
// 0..15 — the guest's if-chain draws nothing for such a value, so a null arm is a real answer
// ("nothing to port here"), not a lookup failure.
struct Scene {
  std::uint32_t stage;
  const StageArm *arm;
};

// Who drew this frame's picture. The two owners differ in where the pixels came from, which decides
// both the present source and whether the native frame's own submission is meaningful.
enum class SceneOwner : std::uint8_t {
  NativeProducers,
  GuestArm,
};

// The render seam for one frame on one Core, constructed once per Core by the title's frame
// driver so paired producer history survives logic-frame boundaries without guest-memory state.
class FrameRenderer {
public:
  explicit FrameRenderer(Core *c,
                         spyro::render::SpriteQueueOffsetObserver *queueObserver = nullptr);

  // Announce the title's native-render policy after the framework installs RenderMode.
  static void installModeFromConfig(Core *c);

  void drawFrame();

  // The scene the game is drawing right now, from the guest's own stage selector.
  Scene classifyScene() const;

private:
  void referenceOtWalk() const;             // the guest's render driver, unmodified
  void prepareScene(const Scene &sc) const; // render-only state needed before nativeFrameBegin
  SceneOwner renderScene(const Scene &sc);  // the native picture — dispatches producers
  // The backlog owner, not a fatal. A stage with no native recipe is a gap in the port; it is
  // reported in full and handed to the guest renderer. An abort here would crash on every
  // unported player-reachable scene.
  void reportRefused(const Scene &sc, const char *why) const;
  // One refused scene handed to the guest renderer. Counts refusals so a scene the port never ports
  // cannot turn into one log line per displayed field.
  void drawSceneWithGuestFallback(const Scene &sc, const char *why);
  // …and its answer, for the composition sites that must return a scene owner from inside an `if`.
  SceneOwner guestFallback(const Scene &sc, const char *why) {
    drawSceneWithGuestFallback(sc, why);
    return SceneOwner::GuestArm;
  }
  void reportBacklog(const Scene &sc) const; // scene.cpp — the arm/layer detail

  // ── Producers ──
  // title_menu.cpp — stage 13's front-end sprite layer (guest 0x8007CEE4's picture). False = this
  // frame's menu mode has no producer, so the seam must not present without its menu.
  bool titleMenuRender(std::int32_t drawOfsX,
                       std::int32_t drawOfsY,
                       std::int32_t clipX0,
                       std::int32_t clipY0,
                       std::int32_t clipX1,
                       std::int32_t clipY1) const;
  bool spriteEmit(std::int32_t x,
                  std::int32_t y,
                  std::int32_t id,
                  std::uint32_t style,
                  std::int32_t drawOfsX,
                  std::int32_t drawOfsY,
                  std::int32_t clipX0,
                  std::int32_t clipY0,
                  std::int32_t clipX1,
                  std::int32_t clipY1,
                  const char *element,
                  std::size_t index) const;
  // sprite_queue.cpp — native screen-space class of RasterizeSpritePrimQueue 0x80022A2C.
  // Also owns stage-13/mode-3's text-actor construction and invokes its paired-actor pass.
  // False means one of those producers refused its current input.
  bool stage13Mode3Render() const;

  Core *mC;
  spyro::render::SpriteQueueOffsetObserver *mQueueObserver;
  // The DRAWENV this frame is being drawn with, set by drawFrame()'s call to nativeFrameBegin() on
  // the native leg only. 0 on the reference leg, where the guest's own driver owns the env.
  std::uint32_t mEnv = 0;
  // The unported-scene account, and the reason it is state rather than a log call per frame: the
  // guest fallback answers a refused scene on every field it is on screen. One report on entry, one
  // on exit, and the frame count between them.
  struct RefusedSceneRun {
    std::uint32_t stage = 0;
    const char *why = nullptr;
    std::uint64_t frames = 0;
    bool reported = false;
  };
  RefusedSceneRun mRefusedScene;
};

} // namespace spyro::render
