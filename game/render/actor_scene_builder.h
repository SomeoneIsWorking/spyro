#pragma once

#include "actor_recipe_capture.h"
#include "moby_shadow_list.h"
#include "sector_visibility.h"
#include "wide_clip_plan.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

class Core;

namespace spyro::actor_scene {

enum class Status : uint8_t {
  Ready,
  InvalidMobyArray,
  UnterminatedMobyArray,
  RecordCapacityExceeded,
  RecordCaptureRefused,
  InvalidShadowCursor,
};

struct Census {
  uint32_t scanned = 0;
  uint32_t queued = 0;
  uint32_t culled = 0;
  uint32_t coarseCulled = 0;
  uint32_t viewCulled = 0;
  uint32_t invalidModel = 0;
};

struct Shadow {
  uint32_t moby = 0;
  uint32_t modelByte = 0;
};

// Retail's +0x51 ("was drawn") store for one list entry: 0x8001F158 clears it on entry
// (0x8001F1FC) and sets it once the entry survives every plane (0x8001F4CC). Gameplay reads it —
// 0x80051FEC admits a Moby to the update list on it — so it always carries the 4:3 answer.
struct WasDrawn {
  uint32_t moby = 0;
  uint8_t value = 0;
};

struct Frame {
  // What the port DRAWS: records passing the drawn plane, and the shadow entries it would stage.
  std::vector<actor_recipe_capture::Record> records;
  moby_shadow_list::List drawnShadows;
  // What the GUEST sees, committed by `commit`: retail's 4:3 shadow list and +0x51 stores.
  std::vector<Shadow> shadows;
  std::vector<WasDrawn> wasDrawn;
  Census census{};
  uint32_t shadowCursor = 0;
};

// One plane test's outcome. `horizontal` is the point where 0x8001F158/0x800208FC stage a shadow:
// after the depth and horizontal planes, BEFORE the vertical one.
struct Visibility {
  bool horizontal = false;
  bool visible = false;
  uint32_t flags = 0; // 0x40000000 wholly inside, 0x80000000 needs clipping; zero when not visible
};

// Both answers from ONE view. `guest` is retail's own 512-px plane; `drawn` widens only the
// horizontal plane, with the shared rule world sectors use (wide::drawnHorizontalInside).
struct Answers {
  Visibility guest;
  Visibility drawn;
};

Answers
classify_view(std::array<int32_t, 3> view, uint32_t modelRadius, int32_t radius, int32_t drawWidth);

// What the port draws beside what the guest decides. `sectors` is the drawn sector table the world
// submission published (a superset of D_800771C8); `width` is the drawn horizontal plane. At 4:3
// both are the guest's own, and the pass is retail's exactly.
struct DrawnScope {
  sector_visibility::Table sectors{};
  int32_t width = wide::kNativeClipWidth;
};

// Shared semantic half of the two retail Moby builders. Both 0x8001F158 and
// 0x800208FC transform the same Moby/model state into the same 0x38-byte
// record shape; their source-list ownership is different. Keeping this one
// implementation prevents their culling and matrix formulas from drifting.
// Returns whether the port DRAWS the Moby (the record then carries the drawn plane's flags);
// `answers` always holds both answers, and `source.descriptor`/`source.tz` are valid whenever a
// plane was evaluated at all.
bool build_source_record(Core *c,
                         uint32_t moby,
                         int32_t drawWidth,
                         actor_recipe_capture::SourceRecord &source,
                         Census &census,
                         Answers &answers);

// The shadow-list entry retail appends for `moby`: its model's AnimationFrame::m_Shadow byte,
// descriptor-relative. Empty when that byte is not addressable.
std::optional<moby_shadow_list::Entry> shadow_entry(Core *c, uint32_t moby, uint32_t descriptor);

// Where the regular-actor pass takes its Moby pointers from. FIELD reproduces 0x800521C0's own
// classification of the level array inline, because that is what fills the draw list it would then
// walk. The dragon cutscene 0x8001CFDC instead writes an explicit terminated pointer list into
// g_SonyImage.u.m_Draw.m_Moby and calls 0x8001F158 straight over it, so the category filter that
// belongs to 0x800521C0 must NOT be applied to those entries — retail already decided them.
struct Source {
  enum class Kind : uint8_t { LevelArray, ExplicitList };
  Kind kind = Kind::LevelArray;
  uint32_t list = 0; // guest address of the null-terminated pointer array, ExplicitList only
};

// Builds the regular-actor records and the shadow-list entries produced by the same retail
// culling pass. The frame is inert until commit succeeds in the owning submitter. The guest's own
// list membership is decided from D_800771C8, exactly as 0x800521C0 decided it; the port draws the
// members of `drawn.sectors` that pass the `drawn.width` plane.
Status build_frame(Core *c, Frame &frame, const DrawnScope &drawn, Source source = {});
// Publishes the guest half: +0x51 stores, the 4:3 shadow list and its cursor.
void commit(Core *c, const Frame &frame);

// 0x8001F344 and 0x8001F350 guard the shadow-list append with two `bgez` branches that both SKIP:
// the Moby's m_ShadowDistance must be negative, and its view depth must be nearer than the staging
// limit. The depth is a positive quantity, so the sign belongs on the limit rather than on the
// depth; negating it instead makes the pair unsatisfiable and silently stages no shadow at all.
// The limit is a parameter because the three passes use different ones: 0x8001F158 and 0x800208FC
// both stage against 0x1200, while 0x80022A2C passes its own comparison — see the note at the
// shaded call site, which is deliberately left as retail wrote it.
bool stages_shadow(int32_t shadowWord, int32_t viewZ, int32_t limit);
inline constexpr int32_t kShadowStagingDepth = 0x1200;

// Builds the regular-actor semantic records directly from the level Moby array, camera, model
// table, and animation state. It replaces 0x800521C0 + 0x8001F158 without running either guest body
// or materializing their temporary guest lists. The historical record-only helper draws exactly
// what the guest would, from D_800771C8 at the native width.
Status build_records(Core *c, std::vector<actor_recipe_capture::Record> &records, Census &census);
const char *status_name(Status status);

} // namespace spyro::actor_scene
