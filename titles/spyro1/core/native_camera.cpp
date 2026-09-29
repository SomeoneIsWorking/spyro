#include "native_camera.h"

#include "guest_gp.h"
#include "native_execution.h"

#include <cstdint>

namespace spyro1::native {
namespace {

// g_Camera (camera.h), base 0x80076DD0: the rotation is three halfwords at +0x4C..+0x50, and the
// spherical inputs the body reads are five words at +0x84..+0x94.
constexpr std::uint32_t kCameraRotationX = 0x80076E1Cu;
constexpr std::uint32_t kCameraRotationY = 0x80076E1Eu;
constexpr std::uint32_t kCameraRotationZ = 0x80076E20u;
constexpr std::uint32_t kSphereOffsetAzimuth = 0x80076E54u;
constexpr std::uint32_t kSphereOffsetElevation = 0x80076E58u;
constexpr std::uint32_t kSphereOffsetRadius = 0x80076E5Cu;
constexpr std::uint32_t kSimulationAzimuth = 0x80076E60u;
constexpr std::uint32_t kSimulationElevation = 0x80076E64u;

// ── 0x800342F8 — write the camera's rotation from its spherical coordinates and offsets.
//     v0 = [0x80076E64] + [0x80076E58] ; andi v0,0xFFF ; sh v0,0x6E1E(at)      rotation.y
//     sh a0,0x6E1C(at), a0 = [0x80076E54]                                     rotation.x
//     v0 = 0x800 - [0x80076E5C] - [0x80076E60] ; andi 0xFFF ; sh 0x6E20(at)   rotation.z
// Every field is a 32-bit load and a 16-bit store, so the stored angle is the low half of the
// computed word, and only y and z are masked: rotation.x takes the sphere offset azimuth's low 16
// bits UNMASKED, the one store here that is not a 12-bit angle. A body that tidied it with the same
// 0xFFF would diverge on any camera whose azimuth is not already in range.
//
// v0 exits holding rotation.z and v1 the offset radius the first subtraction consumed.
void cameraRotationFromSphere(Core *c) {
  const std::uint32_t simulationElevation = c->mem_r32(kSimulationElevation);
  const std::uint32_t offsetElevation = c->mem_r32(kSphereOffsetElevation);
  const std::uint32_t offsetAzimuth = c->mem_r32(kSphereOffsetAzimuth);
  const std::uint32_t elevation = (simulationElevation + offsetElevation) & 0xFFFu;
  const std::uint32_t offsetRadius = c->mem_r32(kSphereOffsetRadius);
  c->mem_w16(kCameraRotationY, static_cast<std::uint16_t>(elevation));
  c->mem_w16(kCameraRotationX, static_cast<std::uint16_t>(offsetAzimuth));
  const std::uint32_t simulationAzimuth = c->mem_r32(kSimulationAzimuth);
  const std::uint32_t azimuth = (0x800u - offsetRadius - simulationAzimuth) & 0xFFFu;
  c->mem_w16(kCameraRotationZ, static_cast<std::uint16_t>(azimuth));
  c->r[2] = azimuth;
  c->r[3] = offsetRadius;
}

constexpr std::uint32_t kShoulderRotationSpeed = kGp + 0x6C0u; // 0x80075924
constexpr std::uint32_t kSpyroControlFlags = 0x80078C4Cu;
constexpr std::uint32_t kCameraSpyroOffCenterFrames = 0x80076E98u;
constexpr std::uint32_t kPadHeld = 0x80077380u;
constexpr std::uint32_t kSkipCameraCenter = 0x1000u;
constexpr std::uint32_t kPadR2 = 0x2u;
constexpr std::uint32_t kPadL2 = 0x1u;
constexpr std::uint32_t kShoulderSpeedRight = 0xFFFFFC00u;
constexpr std::uint32_t kShoulderSpeedLeft = 0x400u;

// ── 0x80035F58 — the L2/R2 shoulder-camera rotation speed. The speed global is gp-relative
//     (`sw $zero, 0x6c0($gp)`) and is cleared first, so both early exits leave it at zero and only
//     the button paths store a speed. Every branch here has an `addiu` delay slot, so v0 never
//     carries out what the branch tested: a skip exit hands back the word its `lw` loaded, R2
//     hands back -1024, and the L2 slot runs on BOTH arms, so "neither held" hands back 1024
//     having stored nothing. v1 holds the pad word only where the button tests are reached.
void cameraShoulderRotationInput(Core *c) {
  c->mem_w32(kShoulderRotationSpeed, 0u);
  const std::uint32_t controlFlags = c->mem_r32(kSpyroControlFlags);
  c->r[2] = controlFlags & kSkipCameraCenter;
  if (c->r[2] != 0u) {
    return;
  }
  const std::uint32_t offCenterFrames = c->mem_r32(kCameraSpyroOffCenterFrames);
  c->r[2] = offCenterFrames;
  if (offCenterFrames != 0u) {
    return;
  }
  const std::uint32_t held = c->mem_r32(kPadHeld);
  c->r[3] = held;
  if ((held & kPadR2) != 0u) {
    c->r[2] = kShoulderSpeedRight;
    c->mem_w32(kShoulderRotationSpeed, kShoulderSpeedRight);
    return;
  }
  const bool leftHeld = (held & kPadL2) != 0u;
  c->r[2] = kShoulderSpeedLeft;
  if (leftHeld) {
    c->mem_w32(kShoulderRotationSpeed, kShoulderSpeedLeft);
  }
}

} // namespace

void registerCameraOverrides(Core &core) {
  spyro::installNativeOverride(
      core, 0x800342F8u, "camera_rotation_from_sphere", cameraRotationFromSphere);
  spyro::installNativeOverride(
      core, 0x80035F58u, "camera_shoulder_rotation_input", cameraShoulderRotationInput);
}

} // namespace spyro1::native
