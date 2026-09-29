#include "native_camera.h"

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

} // namespace

void registerCameraOverrides(Core &core) {
  spyro::installNativeOverride(
      core, 0x800342F8u, "camera_rotation_from_sphere", cameraRotationFromSphere);
}

} // namespace spyro1::native
