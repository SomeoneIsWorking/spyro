#include "field_environment_oracle.h"

#include "cfg.h"
#include "core.h"
#include "field_environment_recipe.h"
#include "rec_decls.h"
#include "recomp_iface.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <lucent/log.h>

namespace {

constexpr uint32_t kEnvironmentLayer = 0x8002b9ccu;
constexpr uint32_t kWorldRenderer = 0x800258f0u;

struct PendingCall {
  spyro::field_environment::Invocation expected{};
  uint32_t worldEntries = 0;
  bool active = false;
};

PendingCall s_pending{};
uint64_t s_calls = 0;
uint64_t s_matches = 0;
uint64_t s_foreignWorldCalls = 0;
bool s_armed = false;

[[noreturn]] void fail(const char *reason,
                       int32_t expectedSelection = 0,
                       int32_t observedSelection = 0,
                       uint32_t expectedDistance = 0,
                       uint32_t observedDistance = 0,
                       uint32_t nonzeroWorkBytes = 0) {
  lucent::error("fieldenvoracle",
                "FAIL {} expected selection={} distance=0x{:X}; observed selection={} "
                "distance=0x{:X}; nonzero work bytes={}",
                reason,
                expectedSelection,
                expectedDistance,
                observedSelection,
                observedDistance,
                nonzeroWorkBytes);
  std::abort();
}

spyro::field_environment::State readState(Core *core) {
  return {.cameraOcclusionGroup =
              (int32_t)core->mem_r32(spyro::field_environment::kCameraOcclusionGroup),
          .occlusionGroupCount =
              (int32_t)core->mem_r32(spyro::field_environment::kOcclusionGroupCount),
          .stage = core->mem_r32(spyro::field_environment::kStageSelector)};
}

void worldEntry(Core *core) {
  if (!s_pending.active) {
    ++s_foreignWorldCalls;
    gen_func_800258F0(core);
    return;
  }
  ++s_pending.worldEntries;
  uint32_t nonzero = 0;
  for (uint32_t i = 0; i < spyro::field_environment::kEdgeWorkAreaSize; ++i) {
    nonzero += core->mem_r8(spyro::field_environment::kEdgeWorkArea + i) != 0u;
  }
  const spyro::field_environment::ObservedBoundary observed = {
      .worldSelection = (int32_t)core->r[4],
      .cullingDistance = core->mem_r32(spyro::field_environment::kCullingDistance),
      .nonzeroWorkBytes = nonzero};
  if (!spyro::field_environment::matches(s_pending.expected, observed)) {
    fail("retail world-call boundary differs from semantic recipe",
         s_pending.expected.worldSelection,
         observed.worldSelection,
         s_pending.expected.cullingDistance,
         observed.cullingDistance,
         observed.nonzeroWorkBytes);
  }
  ++s_matches;
  gen_func_800258F0(core);
}

void environmentLayer(Core *core) {
  if (s_pending.active) {
    fail("reentrant environment layer");
  }
  s_pending = {.expected = spyro::field_environment::derive(readState(core)),
               .worldEntries = 0,
               .active = true};
  ++s_calls;
  gen_func_8002B9CC(core);
  const uint32_t entries = s_pending.worldEntries;
  s_pending.active = false;
  if (entries != 1u) {
    fail("retail environment layer did not call world exactly once");
  }
}

} // namespace

void spyro_register_field_environment_oracle() {
  if (!cfg_on("PSXPORT_FIELD_ENVIRONMENT_ORACLE")) {
    return;
  }
  const char *renderPath = cfg_str("PSXPORT_RENDER_PATH");
  if (!renderPath || std::strcmp(renderPath, "gte") != 0) {
    lucent::error("fieldenvoracle",
                  "REFUSED: the retail-call oracle requires PSXPORT_RENDER_PATH=gte; it must not "
                  "replace a native or widescreen world owner");
    // gate.py's independent native producer-census subprocess inherits the
    // caller's environment. This oracle has no claim in that process; an
    // explicit refusal preserves its native owner while the reference process
    // remains acceptance-gated by finish().
    return;
  }
  if (cfg_on("PSXPORT_NATIVE_WORLD") || cfg_on("PSXPORT_WORLD_CENSUS") ||
      cfg_str("PSXPORT_NDIFF_IDENTITY") || cfg_str("PSXPORT_INTERP_FN") ||
      cfg_str("PSXPORT_MUTE_FN")) {
    lucent::error("fieldenvoracle",
                  "REFUSED: another diagnostic/native owner also claims world renderer "
                  "0x{:08X}; choose one instrument",
                  kWorldRenderer);
    std::abort();
  }
  s_armed = true;
  psxport_recomp()->shard_set_override(kEnvironmentLayer, environmentLayer);
  psxport_recomp()->shard_set_override(kWorldRenderer, worldEntry);
  lucent::info("fieldenvoracle",
               "ARMED retail FIELD environment comparison at 0x{:08X}: exact selection, "
               "culling distance, zeroed 0x{:X}-byte edge-work area, and one world call per "
               "layer invocation",
               kEnvironmentLayer,
               spyro::field_environment::kEdgeWorkAreaSize);
}

void spyro_field_environment_oracle_finish() {
  if (!s_armed) {
    return;
  }
  if (!s_calls || s_matches != s_calls) {
    fail("run ended without a complete positive corpus");
  }
  lucent::info("fieldenvoracle",
               "PASS {} / {} FIELD environment call(s): semantic invocation matched retail; "
               "foreign world calls={}",
               s_matches,
               s_calls,
               s_foreignWorldCalls);
}
