// world_animation_oracle.cpp — is the native phase-1 animation the SAME animation the guest runs?
//
// `PSXPORT_WORLD_ANIMATION_ORACLE=1` answers that against the retained body, on real frames, with
// no exclusion list to argue about.
//
// The trick is that the guest's animation RETIRES itself: a channel it applies gets its stamp byte
// written back to -1, so a second pass over the same sector animates nothing and only renders. That
// gives an exact A/B with no need to separate "animation writes" from "renderer writes" by address:
//
//   A. from the captured RAM, run the retained body  -> animation + render
//   B. from the same captured RAM, run the NATIVE animation, then the retained body -> render only
//
// If the native animation is the guest's animation, A and B are byte-identical across all of guest
// RAM — same sector arrays going into phase 2, therefore the same packets, ordering table and pool.
// A difference is a real defect and the first differing address names where. Anything the native
// side gets wrong shows up: a missed channel leaves stale arrays, a spurious write leaves extra
// state, and an off-by-one in the blend changes the geometry that phase 2 projects.
//
// The run always continues on the GUEST's result, so arming this can never let a divergence leak
// into the picture being measured.
#include "cfg.h"
#include "core.h"
#include "field_environment_recipe.h"
#include "recomp_iface.h"
#include "world_scene_builder.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <lucent/log.h>
#include <string_view>
#include <vector>

namespace {

constexpr uint32_t kWorldRenderer = 0x800258F0u;

struct Oracle {
  bool armed = false;
  uint32_t calls = 0;    // every live call, whether or not it was compared
  uint32_t compared = 0; // calls that actually ran both legs
  uint32_t agreed = 0;   // ...of which byte-identical
  uint32_t refused = 0;  // ...of which the native animation would not decode
  uint32_t channels = 0; // animation channels the native side applied, over compared calls
  uint32_t direct = 0;
  uint32_t blended = 0;
  uint32_t idle = 0; // compared calls where no channel was live at all
  uint32_t firstBadCall = 0;
  uint32_t firstBadAddress = 0;
  uint32_t worstBytes = 0;
  const char *firstRefusal = "none";
  uint32_t limit = 0;
  bool mutate = false;
} s;

std::vector<uint8_t> snapshotRam(const Core *c) {
  return std::vector<uint8_t>(c->ram, c->ram + sizeof(c->ram));
}

void restoreRam(Core *c, const std::vector<uint8_t> &image) {
  std::memcpy(c->ram, image.data(), image.size());
}

void runBody(Core *c, void (*self)(Core *)) {
  const RecompRegistry *R = psxport_recomp();
  R->shard_set_override(kWorldRenderer, nullptr);
  R->main_dispatch(c, kWorldRenderer);
  R->shard_set_override(kWorldRenderer, self);
}

void hook(Core *c);

void hook(Core *c) {
  s.calls++;
  if (s.calls > s.limit) {
    runBody(c, hook);
    return;
  }
  const int32_t selection = (int32_t)c->r[4];
  const std::vector<uint8_t> before = snapshotRam(c);
  uint32_t registers[32];
  std::memcpy(registers, c->r, sizeof(registers));

  runBody(c, hook);
  const std::vector<uint8_t> guest = snapshotRam(c);

  restoreRam(c, before);
  std::memcpy(c->r, registers, sizeof(registers));
  const auto animation = spyro::world_scene::animate(c, selection);
  // NEGATIVE CONTROL, shipped rather than done once by hand: perturb a single byte of what the
  // animation just wrote. A comparison that still reports IDENTICAL under this is not looking at
  // the animation at all, and the run says so instead of passing.
  if (animation.ok && s.mutate && animation.writes > 0u) {
    const uint32_t probe = animation.lastAddress;
    c->mem_w8(probe, (uint8_t)(c->mem_r8(probe) ^ 0xffu));
    lucent::info("worldanim",
                 "call {}: NEGATIVE CONTROL armed — flipped 0x{:08X}, which the animation itself "
                 "wrote. The comparison below must report a difference.",
                 s.calls,
                 probe);
  }
  if (!animation.ok) {
    s.refused++;
    if (s.firstRefusal == std::string_view("none").data() && s.refused == 1u) {
      s.firstRefusal = animation.refusal;
    }
    lucent::error("worldanim",
                  "call {} selection {}: the native animation REFUSED ({}) where the guest body "
                  "animated normally — comparison skipped, run continues on the guest result",
                  s.calls,
                  selection,
                  animation.refusal);
    restoreRam(c, guest);
    return;
  }
  runBody(c, hook);

  s.compared++;
  s.channels += animation.channels;
  s.direct += animation.direct;
  s.blended += animation.blended;
  if (animation.channels == 0u) {
    s.idle++;
  }
  uint32_t differing = 0;
  uint32_t firstAddress = 0;
  for (size_t i = 0; i < guest.size(); ++i) {
    if (c->ram[i] != guest[i]) {
      if (differing == 0u) {
        firstAddress = 0x80000000u + (uint32_t)i;
      }
      differing++;
    }
  }
  if (differing == 0u) {
    s.agreed++;
    lucent::debug("worldanim",
                  "call {} selection {}: IDENTICAL guest RAM after {} channel(s) "
                  "({} direct, {} blended, {} write(s))",
                  s.calls,
                  selection,
                  animation.channels,
                  animation.direct,
                  animation.blended,
                  animation.writes);
  } else {
    if (s.firstBadCall == 0u) {
      s.firstBadCall = s.calls;
      s.firstBadAddress = firstAddress;
    }
    if (differing > s.worstBytes) {
      s.worstBytes = differing;
    }
    lucent::error("worldanim",
                  "call {} selection {}: {} byte(s) of guest RAM DIFFER, first at 0x{:08X}, after "
                  "{} channel(s) ({} direct, {} blended)",
                  s.calls,
                  selection,
                  differing,
                  firstAddress,
                  animation.channels,
                  animation.direct,
                  animation.blended);
  }
  restoreRam(c, guest);
}

} // namespace

void spyro_world_animation_oracle_finish();

// The reference leg cannot reach the field — it fail-fasts at the title overlay's guest VSync tail
// by design — so the live comparison above only ever sees the title's world call. The frame that
// actually matters is a captured one, and the same A/B works verbatim against a RAM image:
// PSXPORT_WORLD_ANIMATION_ORACLE_SNAPSHOT=<snap.bin>[,<selection>] replaces guest RAM with the
// image and runs both legs on it. Default selection is the one the FIELD environment wrapper
// derives.
void spyro_world_animation_oracle_snapshot(Core *c) {
  const char *spec = cfg_str("PSXPORT_WORLD_ANIMATION_ORACLE_SNAPSHOT");
  if (spec == nullptr || spec[0] == '\0' || c == nullptr) {
    return;
  }
  char path[512];
  std::snprintf(path, sizeof path, "%s", spec);
  int32_t selection = -1;
  bool explicitSelection = false;
  if (char *comma = std::strchr(path, ',')) {
    *comma = '\0';
    selection = (int32_t)std::strtol(comma + 1, nullptr, 0);
    explicitSelection = true;
  }
  std::FILE *file = std::fopen(path, "rb");
  if (file == nullptr) {
    lucent::error("worldanim", "snapshot {}: cannot open — NOTHING was compared", path);
    std::exit(2);
  }
  const size_t read = std::fread(c->ram, 1u, sizeof(c->ram), file);
  std::fclose(file);
  if (read != sizeof(c->ram)) {
    lucent::error("worldanim",
                  "snapshot {}: read {} of {} byte(s) — a short image is not a frame, NOTHING was "
                  "compared",
                  path,
                  read,
                  sizeof(c->ram));
    std::exit(2);
  }
  if (!explicitSelection) {
    selection = spyro::field_environment::derive(
                    {.cameraOcclusionGroup =
                         (int32_t)c->mem_r32(spyro::field_environment::kCameraOcclusionGroup),
                     .occlusionGroupCount =
                         (int32_t)c->mem_r32(spyro::field_environment::kOcclusionGroupCount),
                     .stage = c->mem_r32(spyro::field_environment::kStageSelector)})
                    .worldSelection;
  }
  s.armed = true;
  s.limit = 1u;
  s.mutate = cfg_on("PSXPORT_WORLD_ANIMATION_ORACLE_MUTATE") != 0;
  lucent::info("worldanim",
               "snapshot {}: comparing the native phase-1 animation against the retained body on "
               "this captured frame at selection {}",
               path,
               selection);
  c->r[4] = (uint32_t)selection;
  hook(c);
  spyro_world_animation_oracle_finish();
  std::exit(s.compared == 1u && s.agreed == 1u ? 0 : 1);
}

void spyro_world_animation_oracle_install() {
  if (!cfg_on("PSXPORT_WORLD_ANIMATION_ORACLE")) {
    return;
  }
  // The census, the native body and this oracle all want the single override slot at 0x800258F0.
  // Deciding that by registration order is how a probe ends up silently measuring something else,
  // so refuse instead.
  if (cfg_on("PSXPORT_NATIVE_WORLD") || cfg_str("PSXPORT_WORLD_CENSUS")) {
    lucent::error("worldanim",
                  "PSXPORT_WORLD_ANIMATION_ORACLE=1 needs the single override slot at 0x{:08X}, "
                  "which PSXPORT_NATIVE_WORLD / PSXPORT_WORLD_CENSUS also claim. NOT arming. "
                  "Pick one.",
                  kWorldRenderer);
    return;
  }
  s.limit = (uint32_t)cfg_int("PSXPORT_WORLD_ANIMATION_ORACLE_CALLS", 64);
  s.mutate = cfg_on("PSXPORT_WORLD_ANIMATION_ORACLE_MUTATE") != 0;
  s.armed = true;
  psxport_recomp()->shard_set_override(kWorldRenderer, hook);
  lucent::info("worldanim",
               "ARMED at 0x{:08X}: comparing the native phase-1 animation against the retained "
               "body over the first {} call(s). Each compared call runs the body twice from the "
               "same RAM — once as the guest, once after the native animation has retired the "
               "channels — and requires the two results to be byte-identical.",
               kWorldRenderer,
               s.limit);
}

void spyro_world_animation_oracle_finish() {
  if (!s.armed) {
    return;
  }
  // A run that never reached a world call must say so in the same shape as one that did, or an
  // untested build reads exactly like a passing one.
  if (s.compared == 0u) {
    lucent::error("worldanim",
                  "run-end: 0 of {} call(s) compared ({} refused). THIS RUN PROVES NOTHING about "
                  "the native animation — it never reached a comparable world call.",
                  s.calls,
                  s.refused);
    return;
  }
  lucent::info("worldanim",
               "run-end: {} of {} call(s) compared — {} byte-identical, {} DIFFERING, {} refused. "
               "Channels applied: {} ({} direct, {} blended); {} compared call(s) had no live "
               "channel at all and so test only that the native path leaves state alone.",
               s.compared,
               s.calls,
               s.agreed,
               s.compared - s.agreed,
               s.refused,
               s.channels,
               s.direct,
               s.blended,
               s.idle);
  if (s.channels == 0u) {
    lucent::error("worldanim",
                  "run-end: every compared call had ZERO live animation channels, so the blended "
                  "and direct forms were never exercised. This is not a pass.");
  }
  if (s.agreed != s.compared) {
    lucent::error("worldanim",
                  "run-end: first divergence at call {} address 0x{:08X}; worst call differed in "
                  "{} byte(s). First refusal: {}",
                  s.firstBadCall,
                  s.firstBadAddress,
                  s.worstBytes,
                  s.firstRefusal);
  }
}
