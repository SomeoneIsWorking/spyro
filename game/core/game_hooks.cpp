// Residual compatibility callbacks for framework operations that do not yet have typed runtime
// methods. Context lifecycle, boot, and override registration belong to SpyroRuntime.
#include "boot_skip.h"
#include "core.h"
#include "fx_paired_actor.h"
#include "game_iface.h"
#include "legacy_game_interface.h"
#include "spyro_game.h"
#include <cstring>
#include <lucent/log.h>

// rec_dispatch — the substrate's address->recompiled-function router (core.h, extern "C").
extern "C" void rec_dispatch(Core *c, uint32_t addr);

// Spyro's scene renderer (0x80022A2C) reads these globals before walking any objects: five packed
// GTE rotation words at 0x80076DD0 and the camera's world position at 0x80076DF8. It subtracts the
// camera position from each world position and then applies the matrix, so the equivalent affine
// view transform is R * world + (-(R * cameraPosition) / 4096). These are persistent game camera
// globals, not a readback of whichever per-object matrix happens to be in the GTE.
static constexpr uint32_t kSceneRotation = 0x80076DD0u;
static constexpr uint32_t kCameraPosition = 0x80076DF8u;

static void spyro_decode_scene_cam(const uint32_t packed[5],
                                   const int32_t camera[3],
                                   float R[3][3],
                                   float T[3]) {
  R[0][0] = (int16_t)packed[0];
  R[0][1] = (int16_t)(packed[0] >> 16);
  R[0][2] = (int16_t)packed[1];
  R[1][0] = (int16_t)(packed[1] >> 16);
  R[1][1] = (int16_t)packed[2];
  R[1][2] = (int16_t)(packed[2] >> 16);
  R[2][0] = (int16_t)packed[3];
  R[2][1] = (int16_t)(packed[3] >> 16);
  R[2][2] = (int16_t)packed[4];
  for (int row = 0; row < 3; row++) {
    const double dot = (double)R[row][0] * camera[0] + (double)R[row][1] * camera[1] +
                       (double)R[row][2] * camera[2];
    T[row] = (float)(-dot / 4096.0);
  }
}

static void spyro_fps60ReadSceneCam(Core *c, float R[3][3], float T[3]) {
  uint32_t packed[5];
  int32_t camera[3];
  for (int i = 0; i < 5; i++) {
    packed[i] = c->mem_r32(kSceneRotation + (uint32_t)i * 4u);
  }
  for (int i = 0; i < 3; i++) {
    camera[i] = (int32_t)c->mem_r32(kCameraPosition + (uint32_t)i * 4u);
  }
  spyro_decode_scene_cam(packed, camera, R, T);
}

static int spyro_selftestGame(const char *which, const char *) {
  if (std::strcmp(which, "pairedpose") == 0) {
    return spyro_paired_actor_selftest();
  }
  if (std::strcmp(which, "terrainrecipe") == 0) {
    return spyro_native_terrain_selftest();
  }
  if (std::strcmp(which, "actorchainrecipe") == 0) {
    return spyro_actor_chain_oracle_selftest();
  }
  if (std::strcmp(which, "bootskip") == 0) {
    return spyro_boot_skip_selftest();
  }
  if (std::strcmp(which, "scenecam") != 0) {
    return 2;
  }

  float R[3][3], T[3];
  const uint32_t identity[5] = {0x00001000u, 0u, 0x00001000u, 0u, 0x00001000u};
  const int32_t p0[3] = {10, 20, 30};
  spyro_decode_scene_cam(identity, p0, R, T);
  int checks = 0;
  auto expect = [&](bool pass, const char *what) {
    checks++;
    if (!pass) {
      lucent::error("selftest", "FAIL(scenecam): {}", what);
    }
    return pass;
  };
  bool ok = true;
  ok &= expect(R[0][0] == 4096 && R[1][1] == 4096 && R[2][2] == 4096, "identity diagonal unpack");
  ok &= expect(R[0][1] == 0 && R[0][2] == 0 && R[1][0] == 0 && R[1][2] == 0 && R[2][0] == 0 &&
                   R[2][1] == 0,
               "identity off-diagonal unpack");
  ok &= expect(T[0] == -10 && T[1] == -20 && T[2] == -30, "identity camera-position translation");

  // +90 degrees around Z: R*(2,3,5)=(-3,2,5), hence view T=(3,-2,-5).
  const uint32_t rot_z[5] = {0xF0000000u, 0x10000000u, 0u, 0u, 0x00001000u};
  const int32_t p1[3] = {2, 3, 5};
  spyro_decode_scene_cam(rot_z, p1, R, T);
  ok &= expect(R[0][1] == -4096 && R[1][0] == 4096 && R[2][2] == 4096,
               "signed packed rotation unpack");
  ok &= expect(T[0] == 3 && T[1] == -2 && T[2] == -5, "rotated camera-position translation sign");

  if (ok) {
    lucent::info("selftest", "PASS(scenecam): {} checks", checks);
  }
  return ok ? 0 : 1;
}

// Bind by name. A positional table silently shifted when psxport inserted a warp hook because
// the surrounding null callbacks were type-compatible; the first later non-null callback merely
// made that old defect visible. Unlisted hooks are value-initialised to null, so this is also the
// exact inventory of framework callbacks Spyro has actually stood up.
static const GameHooks g_spyro_compatibility_hooks = {
    .fps60WorldPass = spyro_paired_actor_fps60_world_pass,
    .fps60TemporalRotate = spyro_paired_actor_fps60_rotate,
    .selftestGame = spyro_selftestGame,
    .fps60ReadSceneCam = spyro_fps60ReadSceneCam,
};

namespace spyro::legacy {

const GameHooks &compatibilityHooks() {
  return g_spyro_compatibility_hooks;
}

} // namespace spyro::legacy
