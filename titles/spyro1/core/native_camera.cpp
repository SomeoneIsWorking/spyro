#include "native_camera.h"

#include "guest_globals.h"
#include "guest_gp.h"
#include "native_execution.h"

#include <cstdint>

namespace spyro1::native {
namespace {

using spyro::guest::kCameraRotationX;
using spyro::guest::kCameraRotationY;
using spyro::guest::kCameraRotationZ;
using spyro::guest::kPlayerControlFlags;
using spyro::guest::kSpyro;

// g_Camera (camera.h), base 0x80076DD0: the spherical inputs the body reads are five words at
// +0x84..+0x94, and the rotation it writes is the three halfwords named above.
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
  const std::uint32_t controlFlags = c->mem_r32(kPlayerControlFlags);
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

// g_Camera.m_Sphere's FIRST row, which is kCameraSphereRows below: the leaf below converts these
// three words, m_Coords, in the same azimuth/elevation/radius order that row's angle fields use.
constexpr std::uint32_t kCameraSphereAzimuth = 0x80076E48u;
constexpr std::uint32_t kCameraSphereElevation = 0x80076E4Cu;
constexpr std::uint32_t kCameraSphereRadius = 0x80076E50u;

constexpr std::uint32_t kCosine = 0x80016CB0u; // `jal` at 0x80034228 and 0x80034238
constexpr std::uint32_t kSine = 0x80016C58u;   // `jal` at 0x80034280 and 0x800342B8

constexpr char kCameraSphericalToCartesian[] = "camera_spherical_to_cartesian";

// The guest's `mult` / `mflo` / `sra 12`: a SIGNED 64-bit product whose LOW word alone is shifted
// back down, with hi and lo left holding what the last multiply put there.
std::uint32_t fixedMultiply(Core *c, std::uint32_t value, std::uint32_t factor) {
  const std::int64_t product = static_cast<std::int64_t>(static_cast<std::int32_t>(value)) *
                               static_cast<std::int64_t>(static_cast<std::int32_t>(factor));
  const std::uint32_t low = static_cast<std::uint32_t>(product & 0xFFFFFFFFu);
  c->lo = low;
  c->hi = static_cast<std::uint32_t>(static_cast<std::uint64_t>(product) >> 32);
  return static_cast<std::uint32_t>(static_cast<std::int32_t>(low) >> 12);
}

// ── 0x80034204 — write the cartesian point the camera's first spherical row names, into the
//     Vector3D at a0: x = r*cos(e)*cos(a), y = -(r*cos(e))*sin(a), z = r*sin(e).
// All three inputs are RELOADED from RAM immediately before the instruction that consumes them —
// the body caches nothing, so the x and y terms each recompute radius*cos(e) from a fresh radius
// load and their own `cos(elevation)` call — and the five trigonometry calls are five separate
// calls in a fixed order: cos(e), cos(a), cos(e), sin(a), sin(e). The delay slots are why the order
// matters, since $s0 takes the FIRST call's v0 at the second `jal`. The y term also negates the
// ALREADY SHIFTED radius*cos(e) with `negu`, not the 64-bit product.
//
// v0 exits holding z, the last value the body computed, and v1 the radius word the final `lw`
// loaded — the y term's negated product is overwritten by it and never survives to the exit.
void sphericalToCartesian(Core *c) {
  const std::uint32_t out = c->r[4];

  psx::cpu::callGuestNow(
      *c, kCameraSphericalToCartesian, kCosine, c->mem_r32(kCameraSphereElevation));
  const std::uint32_t xCosElevation = c->r[2];
  psx::cpu::callGuestNow(
      *c, kCameraSphericalToCartesian, kCosine, c->mem_r32(kCameraSphereAzimuth));
  const std::uint32_t cosAzimuth = c->r[2];
  const std::uint32_t xScaledRadius =
      fixedMultiply(c, c->mem_r32(kCameraSphereRadius), xCosElevation);
  c->mem_w32(out, fixedMultiply(c, xScaledRadius, cosAzimuth));

  psx::cpu::callGuestNow(
      *c, kCameraSphericalToCartesian, kCosine, c->mem_r32(kCameraSphereElevation));
  const std::uint32_t yCosElevation = c->r[2];
  psx::cpu::callGuestNow(*c, kCameraSphericalToCartesian, kSine, c->mem_r32(kCameraSphereAzimuth));
  const std::uint32_t sinAzimuth = c->r[2];
  const std::uint32_t yScaledRadius =
      fixedMultiply(c, c->mem_r32(kCameraSphereRadius), yCosElevation);
  c->mem_w32(out + 4u, fixedMultiply(c, 0u - yScaledRadius, sinAzimuth));

  psx::cpu::callGuestNow(
      *c, kCameraSphericalToCartesian, kSine, c->mem_r32(kCameraSphereElevation));
  const std::uint32_t sinElevation = c->r[2];
  c->r[2] = fixedMultiply(c, c->mem_r32(kCameraSphereRadius), sinElevation);
  c->mem_w32(out + 8u, c->r[2]);
  c->r[3] = c->mem_r32(kCameraSphereRadius);
}

// The guest helpers the collision body calls, named for what the decompilation's own listing says
// each one does (external/spyro-1/asm/math.s, asm/collision.s and src/camera.c). They are `jal`
// targets rather than the `lui`-plus-immediate data addresses above, so
// tools/override_constants.py re-derives each one from the `jal` that reaches it.
constexpr std::uint32_t kVecSub = 0x8001778Cu;                       // `jal` at 0x800344C0
constexpr std::uint32_t kVecMagnitude = 0x800171FCu;                 // `jal` at 0x800344CC
constexpr std::uint32_t kVecCopy = 0x80017700u;                      // `jal` at 0x800344E0
constexpr std::uint32_t kVecAdd = 0x80017758u;                       // `jal` at 0x80034428
constexpr std::uint32_t kVecRefineMagnitude = 0x8001729Cu;           // `jal` at 0x80033F40
constexpr std::uint32_t kAtan2 = 0x80016AB4u;                        // `jal` at 0x80033F74
constexpr std::uint32_t kRayBetweenPointsIsClear = 0x80033E40u;      // `jal` at 0x8003475C
constexpr std::uint32_t kUpdateSphericalCoords = 0x80033F08u;        // `jal` at 0x80034440
constexpr std::uint32_t kResetCameraToSphericalPreset = 0x80034358u; // `jal` at 0x800347E4
constexpr std::uint32_t kApplySphericalPreset = 0x80034198u;         // `jal` at 0x80034364
constexpr std::uint32_t kSphericalToCartesian = 0x80034204u;         // `jal` at 0x80034414
constexpr std::uint32_t kCameraRotationFromSphere = 0x800342F8u;     // `jal` at 0x80034450
constexpr std::uint32_t kSphereCollisionCheck = 0x8004BE4Cu;         // `jal` at 0x80034604
constexpr std::uint32_t kRotateVectorByCamera = 0x80017AA4u;         // `jal` at 0x80034B94
constexpr std::uint32_t kVecScaleToLength = 0x800175B8u;             // `jal` at 0x80033E80
constexpr std::uint32_t kSegmentHitsWorld = 0x8004AE38u;             // `jal` at 0x80033EB8

// What each `jal` inside the ray test below wrote into $ra. The segment test spills $ra beside the
// register file it spills to a global the collision and render paths read, so a host call that left
// the caller's own $ra there would publish a return address retail never had; the vector helpers
// only spill theirs into their own frames, and are given their own anyway.
constexpr std::uint32_t kAfterVecSub = 0x80033E68u;         // the `jal` at 0x80033E60
constexpr std::uint32_t kAfterVecMagnitude = 0x80033E74u;   // the `jal` at 0x80033E6C
constexpr std::uint32_t kAfterVecScale = 0x80033E88u;       // the `jal` at 0x80033E80
constexpr std::uint32_t kAfterVecCopyStart = 0x80033E98u;   // the `jal` at 0x80033E90
constexpr std::uint32_t kAfterVecAdd = 0x80033EB4u;         // the `jal` at 0x80033EAC
constexpr std::uint32_t kAfterSegmentTest = 0x80033EC0u;    // the `jal` at 0x80033EB8
constexpr std::uint32_t kAfterVecCopyAdvance = 0x80033ED4u; // the `jal` at 0x80033ECC

// The ray test's own 0x60-byte frame, holding its three working vectors at the retail's own
// offsets, and the two numbers the walk is built from: a step of 1024 units, and one step per 1024
// units of length. The frame has to be the retail's own because the segment test spills $sp, and
// the two vector addresses it spills in $s1 and $s2 are those offsets.
constexpr std::uint32_t kRayFrameBytes = 0x60u;
constexpr std::uint32_t kRayStepVectorOffset = 0x10u;
constexpr std::uint32_t kRayStartOffset = 0x20u;
constexpr std::uint32_t kRayEndOffset = 0x30u;
constexpr std::uint32_t kRayStepUnits = 0x400u;
constexpr std::int32_t kRayStepShift = 10;

constexpr char kRayBetweenPointsIsClearOwner[] = "ray_between_points_is_clear";

// The five `jal`s one pass of the row loop makes. The three passes are three copies of the same
// instructions at three addresses, so the site belongs to the pass and not to the function; the
// addresses are the `jal`s at 0x800348DC/0x800348EC/0x800348FC/0x80034918/0x80034928, the second
// pass's at 0x800349CC/0x800349DC/0x800349EC/0x800349FC/0x80034A0C and the third's at
// 0x80034AB0/0x80034AC0/0x80034AD0/0x80034AE0/0x80034AF0.
struct RowCallSites {
  std::uint32_t sphericalToCartesian;
  std::uint32_t vecAdd;
  std::uint32_t sphereCollision;
  std::uint32_t rayClearDestinationFirst;
  std::uint32_t rayClearDestinationLast;
};
constexpr RowCallSites kRowCallSites[3] = {
    {0x800348DCu, 0x800348ECu, 0x800348FCu, 0x80034918u, 0x80034928u},
    {0x800349CCu, 0x800349DCu, 0x800349ECu, 0x800349FCu, 0x80034A0Cu},
    {0x80034AB0u, 0x80034AC0u, 0x80034AD0u, 0x80034AE0u, 0x80034AF0u},
};

// g_Camera (external/spyro-1/include/camera.h), base 0x80076DD0, and the three globals outside it
// this body touches. Each is the `lui`+`addiu` pair the retail code builds.
//
// m_LastSimulation is the six-word copy the preset reset reloads the row from, and unk_0xA8 is the
// second six-word group it only ever zeroes; both are three angles plus their three offsets.
constexpr std::uint32_t kCameraPosition = 0x80076DF8u;       // m_Position
constexpr std::uint32_t kCameraDestination = 0x80076E04u;    // m_DestinationPosition
constexpr std::uint32_t kCameraState = 0x80076E28u;          // m_State
constexpr std::uint32_t kCameraLastSimulation = 0x80076E30u; // m_LastSimulation
constexpr std::uint32_t kCameraSphereRows = 0x80076E48u;     // m_Sphere: five 0x18-byte rows
constexpr std::uint32_t kCameraSimulation = 0x80076E60u;     // m_Simulation, the six-word snapshot
constexpr std::uint32_t kCameraClearedGroup = 0x80076E78u;   // unk_0xA8, the six words zeroed
constexpr std::uint32_t kCameraCollisionCounter = 0x80076E94u;    // unk_0xC4
constexpr std::uint32_t kCameraOffCenterFrames = 0x80076E98u;     // m_SpyroOffCenterFrames
constexpr std::uint32_t kCameraForcedToDestination = 0x80076E9Cu; // unk_0xCC
constexpr std::uint32_t kCameraFocusPointer = 0x80076EA0u;        // m_Focus, loaded as an address
constexpr std::uint32_t kSimulationRadius = 0x80076E68u;          // m_Simulation.m_Coords.z
constexpr std::uint32_t kSimulationOffsetX = 0x80076E6Cu;         // m_Simulation.m_Offset.x
constexpr std::uint32_t kSimulationOffsetY = 0x80076E70u;         // m_Simulation.m_Offset.y
constexpr std::uint32_t kSimulationOffsetZ = 0x80076E74u;         // m_Simulation.m_Offset.z
constexpr std::uint32_t kCameraSphericalPreset = 0x80076EA8u;     // m_SphericalPreset
constexpr std::uint32_t kCameraAzimuthChanged = 0x80076EB8u;      // unk_0xE8
constexpr std::uint32_t kCollisionPoint = 0x80076B80u;            // g_CollisionPoint
constexpr std::uint32_t kSpyroColorFilterGreen = 0x80078A7Fu;     // g_Spyro.m_colorFilter.m_green
constexpr std::uint32_t kSpyroShadowUnknown1C = 0x8007AA2Cu;      // D_8007AA10.unk_1c
constexpr std::uint32_t kCameraForceFlag = kGp + 0x454u;          // D_800756B8
constexpr std::uint32_t kSphericalPresets = 0x8006CAB4u;          // D_8006CAB4[5]
constexpr std::uint32_t kRowAngleOffsets = 0x8006C82Cu;           // D_8006C82C[5][3]

// The pair the preset reset clears last. src/camera.c files both under the camera's screen shake
// and names neither, so they are named for the order this body reaches them in.
constexpr std::uint32_t kScreenShakeFirst = 0x800756DCu;  // D_800756DC
constexpr std::uint32_t kScreenShakeSecond = 0x8007590Cu; // D_8007590C

constexpr char kCameraCollisionUpdate[] = "camera_collision_update";
constexpr char kCameraSphericalPresetReset[] = "camera_spherical_preset_reset";
constexpr char kCameraSphericalFollow[] = "camera_spherical_follow";

constexpr std::uint32_t kAngleMask = 0xFFFu;
constexpr std::uint32_t kAngleFold = 0x1000u;
constexpr std::uint32_t kAngleUpperHalf = 0x801u;
constexpr std::uint32_t kHalfTurn = 0x800u;
// A Cartesian component at or below this magnitude, on both axes, is "the camera is standing on the
// focus point" and the body falls back to the sphere row's own angle instead of the new one.
constexpr std::uint32_t kOnTheFocusPoint = 0x81u;
// The retail body's 0x28-byte frame: it holds one working Vector3D at +0x10 and $ra at +0x20. An
// override has no frame of its own, so it runs with the same lowered $sp and the same offsets,
// which puts the vector above the frame pointer where no callee frame pushed below it can reach.
constexpr std::uint32_t kFollowFrameBytes = 0x28u;
constexpr std::uint32_t kFollowVectorOffset = 0x10u;
constexpr std::uint32_t kRowCount = 5u;
constexpr std::uint32_t kRowStride = 0x18u;
constexpr std::uint32_t kSavedSimulationWords = 6u;
constexpr std::uint32_t kSphericalGroupWords = 6u;
constexpr std::uint32_t kSphereProbeRadius = 0x100u;
constexpr std::uint32_t kSpyroProbeDepth = 0x134u;
constexpr std::uint32_t kColorFilterLimit = 0x7Fu;
constexpr std::uint32_t kColorFilterFlash = 2u;
constexpr std::uint32_t kForcedToDestination = 1u;
constexpr std::uint32_t kAcceptedRowMarker = 5u;
constexpr std::uint32_t kCameraStateAlternative = 0x80000009u;
constexpr std::uint32_t kOnScreenXBase = 0x21u;
constexpr std::uint32_t kOnScreenXSpan = 0x1BFu;
constexpr std::uint32_t kOnScreenYBase = 0x19u;
constexpr std::uint32_t kOnScreenYSpan = 0xBFu;
// `slti`/`slt` compare these signed, which is why they are signed here too.
constexpr std::int32_t kWalkProbeLimit = 6;
constexpr std::int32_t kWalkStartsBelowLength = 0x2001;
constexpr std::int32_t kOffCenterLimit = 0x1F;
constexpr std::int32_t kTinyVectorLimit = 0x20;

// The guest's twelve-bit angle fold, applied to a whole 32-bit difference and never to the operands
// separately: mask, then read bit 11 as the sign.
std::int32_t foldAngle(std::uint32_t difference) {
  const std::uint32_t folded = difference & kAngleMask;
  return static_cast<std::int32_t>(folded < kAngleUpperHalf ? folded : folded - kAngleFold);
}

// The `bgez`/`negu`/`slti` triplet the body runs on one value, whether that value is a folded angle
// or a raw Cartesian component: the magnitude, never the value.
std::uint32_t magnitudeOf(std::int32_t angle) {
  const std::uint32_t bits = static_cast<std::uint32_t>(angle);
  return angle < 0 ? 0u - bits : bits;
}

bool isOnTheFocusPoint(std::uint32_t component) {
  return magnitudeOf(static_cast<std::int32_t>(component)) < kOnTheFocusPoint;
}

// ── 0x80033F08 — turn a Cartesian camera position into the camera's spherical coordinates, and
//     report whether the azimuth turned with it. The radius and elevation come from the offset
//     between the position and g_Camera.m_Focus; the azimuth is the one the body KEEPS when the
//     movement is a real turn, and otherwise the sphere row's own angle. The body takes the slower
//     elevation read (a magnitude refined twice, then an arctangent) on BOTH the radius and the
//     azimuth, and every caller stores the answer in g_Camera.unk_0xE8.
//     $v0 carries that answer out through the body's last `move $v0, $a1`, and $v1 the RAW
//     twelve-bit Z offset: the tail stores its signed form and hands back the unsigned one.
// MEASURED on route artisans-walk (319 of 319 sampled calls matching, and a poison store at the
// body's first statement is caught on all 319, so the instrument does report this class of
// difference): a poison on the turned arm and on the on-focus arm is caught on NONE of them, so
// every sampled call leaves through the neither arm. The turned arm (the half-turn fold, the
// elevation rewrite and the $v0 = 1 exit) and BOTH sphere-row fallbacks therefore follow the retail
// disassembly alone — unexercised rather than unchecked by accident.
void cameraSphericalFollow(Core *c) {
  const std::uint32_t entrySp = c->r[29];
  c->r[29] -= kFollowFrameBytes;
  const std::uint32_t offset = c->r[29] + kFollowVectorOffset;
  psx::cpu::callGuestNow(
      *c, kCameraSphericalFollow, kVecSub, offset, c->r[4], c->mem_r32(kCameraFocusPointer));
  psx::cpu::callGuestNow(*c, kCameraSphericalFollow, kVecMagnitude, offset, 1u);
  c->mem_w32(kSimulationRadius, c->r[2]);
  psx::cpu::callGuestNow(*c, kCameraSphericalFollow, kVecRefineMagnitude, offset, c->r[2], 1u);
  c->mem_w32(kSimulationRadius, c->r[2]);
  psx::cpu::callGuestNow(*c, kCameraSphericalFollow, kVecMagnitude, offset, 0u);
  psx::cpu::callGuestNow(*c, kCameraSphericalFollow, kVecRefineMagnitude, offset, c->r[2], 0u);
  psx::cpu::callGuestNow(*c, kCameraSphericalFollow, kAtan2, c->r[2], c->mem_r32(offset + 8u), 1u);
  const std::uint32_t elevation = c->r[2];
  c->mem_w32(kSimulationElevation, elevation);
  // The delay slot of this `jal` negates the argument the body had already loaded, so the second
  // arctangent sees the mirrored Y component rather than a fresh load of it.
  psx::cpu::callGuestNow(
      *c, kCameraSphericalFollow, kAtan2, c->mem_r32(offset), 0u - c->mem_r32(offset + 4u), 1u);
  const std::uint32_t azimuth = c->r[2];
  const std::uint32_t previousAzimuth = c->mem_r32(kSimulationAzimuth);
  const std::uint32_t rawStep = azimuth - previousAzimuth;
  bool turned = magnitudeOf(foldAngle(rawStep + kHalfTurn)) < magnitudeOf(foldAngle(rawStep));
  if (turned) {
    turned = c->mem_r32(kCameraState) == kCameraStateAlternative ||
             c->mem_r32(kCameraAzimuthChanged) != 0u;
  }
  const bool onFocus =
      isOnTheFocusPoint(c->mem_r32(offset)) && isOnTheFocusPoint(c->mem_r32(offset + 4u));
  if (turned) {
    c->mem_w32(kSimulationAzimuth,
               onFocus ? c->mem_r32(kCameraSphereRows) : ((azimuth + kHalfTurn) & kAngleMask));
    c->mem_w32(kSimulationElevation, (kHalfTurn - elevation) & kAngleMask);
    c->r[2] = 1u;
  } else {
    c->mem_w32(kSimulationAzimuth, onFocus ? c->mem_r32(kCameraSphereRows) : azimuth);
    c->r[2] = 0u;
  }
  // The tail re-reads the rotation as SIGNED halfwords and writes all three offsets against the
  // spherical coordinates this body has just settled.
  const std::uint32_t rotationY = static_cast<std::uint32_t>(c->mem_r16s(kCameraRotationY));
  const std::uint32_t rotationZ = static_cast<std::uint32_t>(c->mem_r16s(kCameraRotationZ));
  const std::uint32_t zStep = (kHalfTurn - c->mem_r32(kSimulationAzimuth) - rotationZ) & kAngleMask;
  c->mem_w32(kSimulationOffsetX, static_cast<std::uint32_t>(c->mem_r16s(kCameraRotationX)));
  c->mem_w32(kSimulationOffsetY, static_cast<std::uint32_t>(foldAngle(rotationY - elevation)));
  c->mem_w32(kSimulationOffsetZ, static_cast<std::uint32_t>(foldAngle(zStep)));
  c->r[3] = zStep;
  c->r[29] = entrySp;
}

// The retail body's own 0x68-byte frame, which the differential ignores as dead stack. An override
// has no frame of its own, so it runs with the same lowered $sp and keeps its two working vectors
// and its six-word snapshot at the retail's own offsets: that puts them ABOVE the frame pointer,
// where no callee frame pushed below it can reach, and inside the ignored window.
constexpr std::uint32_t kFrameBytes = 0x68u;
constexpr std::uint32_t kPresetFrameBytes = 0x20u; // the preset reset's own prologue
constexpr std::uint32_t kLiveSavedCount = 7u;      // $s0-$s6; $s7 the body never writes
constexpr std::uint32_t kStepVectorOffset = 0x10u;
constexpr std::uint32_t kWalkVectorOffset = 0x20u;
constexpr std::uint32_t kSnapshotOffset = 0x30u;

struct CameraScratch {
  std::uint32_t walk;     // the moving camera position, at the retail's own frame offset
  std::uint32_t snapshot; // m_Simulation as it stood before the spherical passes
};

// The retail body keeps six values live in $s0-$s6 and runs with $sp already lowered by its own
// 0x68-byte frame. Every callee it calls spills all of them, and the sphere-collision helper's save
// area at 0x80077DD8 SURVIVES the call, so the differential sees those registers as ordinary RAM:
// an override that held them in C++ locals would differ there while behaving identically. The body
// therefore aliases the real registers, and this guard puts the caller's values back on every exit,
// which is exactly what the epilogue's loads do.
//
// $ra is in that list for the same reason, and only since 2026-10-01: the body reaches its callees
// through spyro::callGuestJumpedFrom, which leaves `$ra` holding the callee's own `jal` address
// (docs/issues/0150), and the epilogue restores it from 0x64($sp) on every exit. Without the
// restore the override exited with `$ra` at its last `jal` — measured as call 2 of the attract
// route differing in `register ra`, original 0x80037314 against native 0x80034774.
class SpilledRegisters {
public:
  SpilledRegisters(Core *core, std::uint32_t entrySp)
      : core_(core), entrySp_(entrySp), entryRa_(core->r[31]) {
    for (std::uint32_t index = 0; index < kLiveSavedCount; ++index) {
      entry_[index] = core->r[16 + index];
    }
  }
  SpilledRegisters(const SpilledRegisters &) = delete;
  SpilledRegisters &operator=(const SpilledRegisters &) = delete;
  ~SpilledRegisters() {
    for (std::uint32_t index = 0; index < kLiveSavedCount; ++index) {
      core_->r[16 + index] = entry_[index];
    }
    core_->r[29] = entrySp_;
    core_->r[31] = entryRa_;
  }

private:
  Core *core_;
  std::uint32_t entrySp_;
  std::uint32_t entryRa_;
  std::uint32_t entry_[kLiveSavedCount];
};

// One 0x18-byte spherical row: the camera struct word it starts at, and the per-row angle offsets.
struct SphericalRow {
  std::uint32_t address;
  std::uint32_t offsets;
};

// The guest's `div` is a signed 32-bit divide. The step count is 2 or more on the only path that
// reaches it, so neither of the divide-by-zero and overflow `break` traps the body guards can fire.
std::uint32_t divideSigned(std::uint32_t value, std::int32_t divisor) {
  return static_cast<std::uint32_t>(static_cast<std::int32_t>(value) / divisor);
}

// One iteration of the three passes the body makes over the five rows: add `shift` times the row's
// angle offset to each of its three angles, rebuild the cartesian destination, and test it. Only
// the first pass records a surviving row's bit, and only the later passes have a mask to read.
// `accepted` is $s5 itself, because the guest reads that register in the two later passes.
// Returns true when a candidate ray hits, which is the body's "accept this row" exit.
bool testSphericalRow(Core *c,
                      const SphericalRow &row,
                      const RowCallSites &sites,
                      std::uint32_t shift,
                      std::uint32_t recordBit,
                      std::uint32_t *accepted) {
  c->r[3] = c->mem_r32(row.offsets) << shift;
  c->r[2] = c->mem_r32(row.address + 0x18u) + c->r[3];
  c->r[2] &= kAngleMask;
  c->mem_w32(row.address, c->r[2]);
  c->r[3] = c->mem_r32(row.offsets + 4u) << shift;
  c->r[2] = c->mem_r32(row.address + 0x1Cu) + c->r[3];
  c->r[2] &= kAngleMask;
  c->mem_w32(row.address + 4u, c->r[2]);
  c->r[3] = c->mem_r32(row.offsets + 8u) << shift;
  c->r[2] = c->mem_r32(row.address + 0x20u) + c->r[3];
  c->mem_w32(row.address + 8u, c->r[2]);
  spyro::callGuestJumpedFrom(*c,
                             kCameraCollisionUpdate,
                             sites.sphericalToCartesian,
                             kSphericalToCartesian,
                             kCameraDestination);
  spyro::callGuestJumpedFrom(*c,
                             kCameraCollisionUpdate,
                             sites.vecAdd,
                             kVecAdd,
                             kCameraDestination,
                             kCameraDestination,
                             c->mem_r32(row.address + 0x58u));
  spyro::callGuestJumpedFrom(*c,
                             kCameraCollisionUpdate,
                             sites.sphereCollision,
                             kSphereCollisionCheck,
                             kCameraDestination,
                             kSphereProbeRadius,
                             kSphereProbeRadius);
  if (c->r[2] != 0u) {
    return false;
  }
  if (recordBit != 0u) {
    c->r[2] = recordBit;
    *accepted |= recordBit;
  }
  spyro::callGuestJumpedFrom(*c,
                             kCameraCollisionUpdate,
                             sites.rayClearDestinationFirst,
                             kRayBetweenPointsIsClear,
                             kCameraDestination,
                             c->r[29] + kWalkVectorOffset);
  if (c->r[2] == 0u) {
    return false;
  }
  spyro::callGuestJumpedFrom(*c,
                             kCameraCollisionUpdate,
                             sites.rayClearDestinationLast,
                             kRayBetweenPointsIsClear,
                             c->r[29] + kWalkVectorOffset,
                             kCameraDestination);
  return c->r[2] != 0u;
}

// 0x80034BE4 — the row the body stopped on is accepted: stamp 5 into its +0x4C word, refresh the
// spherical coordinates from the destination the row produced, copy the row's angles into the
// matching last-simulation row, and put back the six-word snapshot taken before the passes began.
void acceptSphericalRow(Core *c, const SphericalRow &row, const CameraScratch &work) {
  c->r[2] = kAcceptedRowMarker;
  c->mem_w32(row.address + 0x4Cu, kAcceptedRowMarker);
  spyro::callGuestJumpedFrom(
      *c, kCameraCollisionUpdate, 0x80034BE8u, kUpdateSphericalCoords, kCameraDestination);
  c->r[2] = c->mem_r32(row.address + 0x18u);
  c->r[3] = c->mem_r32(row.address + 0x1Cu);
  c->r[4] = c->mem_r32(row.address + 0x20u);
  c->mem_w32(row.address - 0x18u, c->r[2]);
  c->mem_w32(row.address - 0x14u, c->r[3]);
  c->mem_w32(row.address - 0x10u, c->r[4]);
  c->r[2] = c->mem_r32(work.snapshot);
  c->r[3] = c->mem_r32(work.snapshot + 4u);
  c->mem_w32(row.address + 0x18u, c->r[2]);
  c->mem_w32(row.address + 0x1Cu, c->r[3]);
  c->r[4] = c->mem_r32(work.snapshot + 8u);
  c->r[5] = c->mem_r32(work.snapshot + 12u);
  c->mem_w32(row.address + 0x20u, c->r[4]);
  c->mem_w32(row.address + 0x24u, c->r[5]);
  c->r[2] = c->mem_r32(work.snapshot + 16u);
  c->r[3] = c->mem_r32(work.snapshot + 20u);
  c->mem_w32(row.address + 0x28u, c->r[2]);
  c->mem_w32(row.address + 0x2Cu, c->r[3]);
}

// 0x80034B18 — the ray between the camera and Spyro is clear in both directions, so the camera
// counts another off-centre frame. The collision counter is first ticked down when the camera is
// being re-centred, and the frame counter is reset rather than incremented when the camera state
// is negative, or when Spyro is drawn inside the screen rectangle and the camera was close enough.
// v0 enters as 1, off the delay slot of the branch that got here.
void settleOffCenterFrame(Core *c, std::uint32_t far, const CameraScratch &work) {
  c->mem_w32(kCameraForceFlag, c->r[2]);
  const std::uint32_t counter = c->mem_r32(kCameraCollisionCounter);
  if (counter != 0u) {
    c->r[3] = c->mem_r32(kCameraState);
    if (c->r[3] != 0u && c->r[3] != kCameraStateAlternative) {
      c->r[2] = counter - 1u;
      c->mem_w32(kCameraCollisionCounter, c->r[2]);
    } else {
      // The unchanged-azimuth arm branches PAST the store, so the counter keeps whatever a callee
      // left in it; storing the zero read here would overwrite that.
      c->r[2] = c->mem_r32(kCameraAzimuthChanged);
      if (c->r[2] != 0u) {
        c->r[2] = counter - 1u;
        c->mem_w32(kCameraCollisionCounter, c->r[2]);
      }
    }
  }
  c->r[2] = c->mem_r32(kCameraState);
  bool centred = static_cast<std::int32_t>(c->r[2]) < 0;
  if (!centred) {
    spyro::callGuestJumpedFrom(
        *c, kCameraCollisionUpdate, 0x80034B94u, kRotateVectorByCamera, work.walk, kSpyro);
    c->r[2] = c->mem_r32(work.walk) - kOnScreenXBase;
    c->r[2] = c->r[2] < kOnScreenXSpan ? 1u : 0u;
    if (c->r[2] != 0u) {
      c->r[2] = c->mem_r32(work.walk + 4u) - kOnScreenYBase;
      c->r[2] = c->r[2] < kOnScreenYSpan ? 1u : 0u;
      centred = c->r[2] != 0u && far == 0u;
    }
  }
  if (centred) {
    c->mem_w32(kCameraOffCenterFrames, 0);
    return;
  }
  c->r[3] = kCameraOffCenterFrames;
  c->r[2] = c->mem_r32(kCameraOffCenterFrames) + 1u;
  c->mem_w32(c->r[3], c->r[2]);
}

// 0x80033E40 — is the straight line between two points clear of world geometry? The offset from the
//     first point to the second is rescaled to a 1024-unit step, the number of steps is the
//     ORIGINAL length shifted down ten, and each step is tested against the world: the first hit
//     answers 0 and a clear line answers 1. v0 carries that 0 or 1 on every path — the hit path out
//     of the delay slot of its own branch — and v1 carries whatever the last guest call left,
//     because the body never writes it.
//
// The body keeps the step vector, the ray start and the ray end in its own 0x60-byte frame and
// holds the first point, the ray start, the ray end and the step count in $s0-$s3, so the frame and
// those four registers are reproduced at the retail's own offsets. The step count is read in the
// delay slot of the rescale call, and $s0 is zeroed in the delay slot of the test that skips the
// walk, so both are set before the call that would otherwise have run first.
//
// MEASURED on routes artisans-walk and portal-level (740 calls, 106 of them shadowed on
// artisans-walk, all matching): every call returned CLEAR, with a step count of 1, 2 or 3. The
// walk itself, the advance, the loop's back edge and the clear answer are therefore covered, and
// the two exits no route here reaches are NOT: the hit answer, whose v0 = 0 comes out of the
// delay slot of the branch that leaves the walk, and the zero-step answer, which skips the walk
// entirely. Both are the two-instruction tails above, and the walk they leave is the one measured.
void rayBetweenPointsIsClear(Core *c) {
  const std::uint32_t first = c->r[4];
  const std::uint32_t second = c->r[5];
  const std::uint32_t entrySp = c->r[29];
  const std::uint32_t entryRa = c->r[31];
  const std::uint32_t entryS0 = c->r[16];
  const std::uint32_t entryS1 = c->r[17];
  const std::uint32_t entryS2 = c->r[18];
  const std::uint32_t entryS3 = c->r[19];
  const std::uint32_t frame = entrySp - kRayFrameBytes;
  const std::uint32_t stepVector = frame + kRayStepVectorOffset;
  const std::uint32_t rayStart = frame + kRayStartOffset;
  const std::uint32_t rayEnd = frame + kRayEndOffset;
  c->r[29] = frame;
  c->r[31] = kAfterVecSub;
  psx::cpu::callGuestNow(*c, kRayBetweenPointsIsClearOwner, kVecSub, stepVector, second, first);
  c->r[31] = kAfterVecMagnitude;
  psx::cpu::callGuestNow(*c, kRayBetweenPointsIsClearOwner, kVecMagnitude, stepVector, 1u);
  const std::uint32_t magnitude = c->r[2];
  c->r[31] = kAfterVecScale;
  c->r[19] = static_cast<std::uint32_t>(static_cast<std::int32_t>(magnitude) >> kRayStepShift);
  psx::cpu::callGuestNow(
      *c, kRayBetweenPointsIsClearOwner, kVecScaleToLength, stepVector, magnitude, kRayStepUnits);
  c->r[17] = rayStart;
  c->r[31] = kAfterVecCopyStart;
  psx::cpu::callGuestNow(*c, kRayBetweenPointsIsClearOwner, kVecCopy, rayStart, first);
  c->r[16] = 0u;
  const std::int32_t steps = static_cast<std::int32_t>(c->r[19]);
  bool clear = true;
  for (std::uint32_t step = 0; static_cast<std::int32_t>(step) < steps; ++step) {
    c->r[18] = rayEnd;
    c->r[31] = kAfterVecAdd;
    psx::cpu::callGuestNow(
        *c, kRayBetweenPointsIsClearOwner, kVecAdd, rayEnd, rayStart, stepVector);
    c->r[16] = step;
    c->r[29] = frame;
    c->r[31] = kAfterSegmentTest;
    psx::cpu::callGuestNow(
        *c, kRayBetweenPointsIsClearOwner, kSegmentHitsWorld, rayStart, rayEnd, c->r[6], c->r[7]);
    if (c->r[2] != 0u) {
      clear = false;
      break;
    }
    c->r[31] = kAfterVecCopyAdvance;
    psx::cpu::callGuestNow(*c, kRayBetweenPointsIsClearOwner, kVecCopy, rayStart, rayEnd);
  }
  c->r[2] = clear ? 1u : 0u;
  c->r[16] = entryS0;
  c->r[17] = entryS1;
  c->r[18] = entryS2;
  c->r[19] = entryS3;
  c->r[29] = entrySp;
  c->r[31] = entryRa;
}

// 0x80034480 — the camera's collision resolution and spherical follow, run once a frame. It
// divides the camera-to-Spyro vector into a number of steps, walks the destination out along them
// probing for a sphere hit, falls back to the five authored spherical presets when the walk leaves
// the camera blocked, and keeps the off-centre frame counter in step. v0 exits carrying whatever
// the last guest call left in it, so each branch reproduces the register the retail body held
// rather than a result of its own. It runs with the body's own $sp frame and its six live $s
// values in the real registers, because every callee it makes spills both.
//
// MEASURED on route artisans-walk (53 of 53 sampled calls matching): every sampled call leaves
// through settleOffCenterFrame, so the differential covers the walk and the off-centre exit and
// NOTHING from the colour filter onwards. A deliberate poison store at the colour filter, and
// another at the snapshot-and-spherical-passes section, are caught at neither, while the same
// store at the body's first statement is caught on all 53 calls: the instrument does report this
// class of difference, so those two regions are unexercised rather than unchecked-by-accident.
// They follow the retail disassembly alone: $s0 names one row for a whole pass and only $s2 walks,
// and the accept mask is $s5 itself, because the later passes read that register.
void resolveCameraCollision(Core *c) {
  const SpilledRegisters entry(c, c->r[29]);
  c->r[29] -= kFrameBytes;
  const std::uint32_t stepVector = c->r[29] + kStepVectorOffset;
  const std::uint32_t walkVector = c->r[29] + kWalkVectorOffset;
  const std::uint32_t snapshot = c->r[29] + kSnapshotOffset;
  const CameraScratch work{walkVector, snapshot};
  // $s0-$s6 alias the real registers: the guest holds live values in them across every call it
  // makes, so each callee spills them and a body that kept them in locals would differ in the
  // spill area even while behaving identically.
  std::uint32_t &s0 = c->r[16];
  std::uint32_t &s1 = c->r[17];
  std::uint32_t &s2 = c->r[18];
  std::uint32_t &s3 = c->r[19];
  std::uint32_t &s4 = c->r[20];
  std::uint32_t &s5 = c->r[21];
  std::uint32_t &s6 = c->r[22];

  s0 = walkVector;
  s3 = kCameraDestination;
  s4 = 0u;
  s5 = kCameraPosition;
  spyro::callGuestJumpedFrom(
      *c, kCameraCollisionUpdate, 0x800344C0u, kVecSub, walkVector, kCameraDestination, kSpyro);
  spyro::callGuestJumpedFrom(
      *c, kCameraCollisionUpdate, 0x800344CCu, kVecMagnitude, walkVector, 1u);
  s6 = static_cast<std::int32_t>(c->r[2]) >= kWalkStartsBelowLength
           ? 1u
           : 0u; // the "camera is far" flag
  spyro::callGuestJumpedFrom(
      *c, kCameraCollisionUpdate, 0x800344E0u, kVecCopy, stepVector, kCameraDestination);
  spyro::callGuestJumpedFrom(*c,
                             kCameraCollisionUpdate,
                             0x800344F4u,
                             kVecSub,
                             walkVector,
                             kCameraDestination,
                             kCameraPosition);
  spyro::callGuestJumpedFrom(
      *c, kCameraCollisionUpdate, 0x80034500u, kVecMagnitude, walkVector, 1u);
  std::int32_t length = static_cast<std::int32_t>(c->r[2]);
  if (length < 0) {
    length += 0xFF;
  }
  c->r[2] = static_cast<std::uint32_t>(length >> 8);
  s2 = static_cast<std::uint32_t>((length >> 8) + 1);
  const std::int32_t steps = static_cast<std::int32_t>(s2);
  c->r[2] = steps < 2 ? 1u : 0u;
  if (c->r[2] == 0u) {
    c->r[4] = divideSigned(c->mem_r32(walkVector), steps);
    c->r[3] = divideSigned(c->mem_r32(walkVector + 4u), steps);
    c->r[2] = divideSigned(c->mem_r32(walkVector + 8u), steps);
    c->mem_w32(walkVector, c->r[4]);
    c->mem_w32(walkVector + 4u, c->r[3]);
    c->mem_w32(walkVector + 8u, c->r[2]);
  }
  spyro::callGuestJumpedFrom(
      *c, kCameraCollisionUpdate, 0x800345D4u, kVecCopy, kCameraDestination, kCameraPosition);

  s0 = 0u;
  while (static_cast<std::int32_t>(s0) < steps) {
    spyro::callGuestJumpedFrom(*c,
                               kCameraCollisionUpdate,
                               0x800345F0u,
                               kVecAdd,
                               kCameraDestination,
                               kCameraDestination,
                               walkVector);
    s1 = 0u;
    for (;;) {
      spyro::callGuestJumpedFrom(*c,
                                 kCameraCollisionUpdate,
                                 0x80034604u,
                                 kSphereCollisionCheck,
                                 kCameraDestination,
                                 kSphereProbeRadius,
                                 kSphereProbeRadius);
      if (c->r[2] == 0u) {
        break;
      }
      spyro::callGuestJumpedFrom(
          *c, kCameraCollisionUpdate, 0x8003461Cu, kVecCopy, kCameraDestination, kCollisionPoint);
      s1 += 1u;
      s4 = 1u; // the delay slot of the row test, so it runs on both arms
      if (static_cast<std::int32_t>(s1) >= kWalkProbeLimit) {
        break;
      }
    }
    s0 += 1u;
    if (static_cast<std::int32_t>(s1) == kWalkProbeLimit) {
      spyro::callGuestJumpedFrom(
          *c, kCameraCollisionUpdate, 0x80034644u, kVecCopy, kCameraDestination, stepVector);
      s0 = static_cast<std::uint32_t>(steps + 1);
    }
  }

  s0 = kCameraForcedToDestination;
  c->mem_w32(s0, 0u); // the delay slot of the hits < 6 test
  c->r[2] = static_cast<std::int32_t>(s1) < kWalkProbeLimit ? 1u : 0u;
  if (c->r[2] != 0u) {
    if (s4 == 0u) {
      spyro::callGuestJumpedFrom(
          *c, kCameraCollisionUpdate, 0x80034710u, kVecCopy, kCameraPosition, kCameraDestination);
      c->r[2] = kForcedToDestination;
      c->mem_w32(s0, kForcedToDestination);
    } else {
      spyro::callGuestJumpedFrom(*c,
                                 kCameraCollisionUpdate,
                                 0x80034684u,
                                 kVecSub,
                                 stepVector,
                                 kCameraDestination,
                                 kCameraPosition);
      for (std::uint32_t component = 0; component < 3u; ++component) {
        const std::uint32_t address = stepVector + component * 4u;
        std::uint32_t value = c->mem_r32(address);
        if (static_cast<std::int32_t>(value) < 0) {
          value = 0u - value;
        }
        c->r[2] = static_cast<std::int32_t>(value) < kTinyVectorLimit ? 1u : 0u;
        if (c->r[2] != 0u) {
          c->mem_w32(address, 0);
        }
      }
      spyro::callGuestJumpedFrom(*c,
                                 kCameraCollisionUpdate,
                                 0x800346FCu,
                                 kVecAdd,
                                 kCameraPosition,
                                 kCameraPosition,
                                 stepVector);
    }
  }

  s2 = kCameraPosition;
  spyro::callGuestJumpedFrom(
      *c, kCameraCollisionUpdate, 0x80034728u, kUpdateSphericalCoords, kCameraPosition);
  c->mem_w32(kCameraAzimuthChanged, c->r[2]);
  spyro::callGuestJumpedFrom(*c, kCameraCollisionUpdate, 0x80034744u, kVecCopy, walkVector, kSpyro);
  c->mem_w32(walkVector + 8u, c->mem_r32(walkVector + 8u) - kSpyroProbeDepth);
  spyro::callGuestJumpedFrom(*c,
                             kCameraCollisionUpdate,
                             0x8003475Cu,
                             kRayBetweenPointsIsClear,
                             kCameraPosition,
                             walkVector);
  if (c->r[2] != 0u) {
    spyro::callGuestJumpedFrom(*c,
                               kCameraCollisionUpdate,
                               0x8003476Cu,
                               kRayBetweenPointsIsClear,
                               walkVector,
                               kCameraPosition);
    const bool blocked = c->r[2] != 0u;
    c->r[2] = kForcedToDestination; // the delay slot, so the taken path always carries 1
    if (blocked) {
      settleOffCenterFrame(c, s6, work);
      return;
    }
  }

  c->r[2] = c->mem_r8(kSpyroColorFilterGreen);
  c->mem_w32(kCameraForceFlag, 0);
  c->r[2] = c->r[2] < kColorFilterLimit ? 1u : 0u;
  if (c->r[2] != 0u) {
    c->r[2] = kColorFilterFlash;
    c->mem_w8(kSpyroColorFilterGreen, kColorFilterFlash);
  }
  c->r[2] = c->mem_r32(kCameraOffCenterFrames);
  c->mem_w32(kSpyroShadowUnknown1C, 0);
  c->r[2] = c->r[2] + 1u;
  c->mem_w32(kCameraOffCenterFrames, c->r[2]);
  c->r[2] = static_cast<std::int32_t>(c->r[2]) < kOffCenterLimit ? 1u : 0u;
  s5 = 0u; // the delay slot of the off-centre test, so it runs on both arms
  if (c->r[2] == 0u) {
    s4 = kCameraSphericalPreset;
    s3 = kCameraDestination;
    s2 = walkVector;
    s0 = 0u;
    s1 = 0u; // once, before the loop: the preset index stays live across every callee
    for (std::uint32_t preset = 0; preset < kRowCount; ++preset) {
      const std::uint32_t presetAddress = kSphericalPresets + s0;
      c->mem_w32(s4, presetAddress); // the reset's own delay slot
      spyro::callGuestJumpedFrom(
          *c, kCameraCollisionUpdate, 0x800347E4u, kResetCameraToSphericalPreset, presetAddress);
      spyro::callGuestJumpedFrom(*c,
                                 kCameraCollisionUpdate,
                                 0x800347F4u,
                                 kSphereCollisionCheck,
                                 s3,
                                 kSphereProbeRadius,
                                 kSphereProbeRadius);
      if (c->r[2] == 0u) {
        spyro::callGuestJumpedFrom(
            *c, kCameraCollisionUpdate, 0x80034804u, kRayBetweenPointsIsClear, s3, s2);
        if (c->r[2] != 0u) {
          spyro::callGuestJumpedFrom(
              *c, kCameraCollisionUpdate, 0x80034814u, kRayBetweenPointsIsClear, s2, s3);
          if (c->r[2] != 0u) {
            return;
          }
        }
      }
      s1 += 1u; // both delay slots run on the loop's two arms
      s0 += kRowStride;
    }
  }

  for (std::uint32_t word = 0; word < kSavedSimulationWords; ++word) {
    c->mem_w32(snapshot + word * 4u, c->mem_r32(kCameraSimulation + word * 4u));
  }
  c->r[2] = kCameraSimulation;

  s3 = kCameraDestination;
  s4 = walkVector;
  for (std::uint32_t pass = 0; pass < 3u; ++pass) {
    // $s0 names ONE row for the whole pass — the retail loop advances only $s2, so every row of a
    // pass reads and writes the first row's angles and takes its own angle offsets. Reading the
    // per-row angles instead would accumulate a different camera and write five rows retail never
    // touches.
    s0 = kCameraSphereRows;
    s2 = 0u;
    for (std::uint32_t row = 0; row < kRowCount; ++row) {
      s1 = row;
      if (pass != 0u) {
        c->r[2] = static_cast<std::uint32_t>(static_cast<std::int32_t>(s5) >> row) & 1u;
        if (c->r[2] == 0u) {
          s2 += kRowStride;
          continue;
        }
      }
      const SphericalRow candidate{kCameraSphereRows, kRowAngleOffsets + s2};
      if (testSphericalRow(
              c, candidate, kRowCallSites[pass], pass, pass == 0u ? 1u << row : 0u, &s5)) {
        acceptSphericalRow(c, candidate, work);
        return;
      }
      s2 += kRowStride;
      c->r[2] = 0u; // the `slti $v0, $s1, 5` that ends the row
    }
    if (s5 == 0u) {
      return;
    }
  }
}

// 0x80034358 — snap the camera onto one of the five authored spherical presets, the address its
// argument names. The preset is applied first with the caller's own $a0, which the retail body
// never replaces before that `jal`; then the camera's first spherical row is reloaded from the
// last simulation, the second six-word group and the two follow-up counters are cleared, the
// destination is rebuilt from the row, offset by the focus, copied to the position and re-solved,
// and the shake pair goes to zero. Nothing branches, so the order below is the disassembly's.
//
// $s0 and $s1 name the row and the destination across every call this body makes, and the prologue
// lowers $sp by 0x20 before the first of them, so both are reproduced through the real registers:
// the callees see the retail's saved-register file, which the fixed spill area at 0x80077DD8 turns
// into ordinary RAM the differential compares. v0/v1 leave holding whatever the rotation update
// leaves, and it is the last call, so neither register is assigned here.
//
// MEASURED on route artisans-walk: 1 of 1 sampled calls matching. The body has no branch at all,
// so that one call exercises every store and every callee it makes — but it is one call, and the
// preset fallback that reaches it is itself rare: resolveCameraCollision's own note records that
// every sampled call of 0x80034480 leaves before the preset loop. Treat this as a straight-line
// transcription checked once, not as a well-sampled differential.
void resetCameraToSphericalPreset(Core *c) {
  const SpilledRegisters entry(c, c->r[29]);
  c->r[29] -= kPresetFrameBytes;
  std::uint32_t &s0 = c->r[16];
  std::uint32_t &s1 = c->r[17];

  // The retail body's first `jal` has no argument setup at all, so the preset pointer the caller
  // left in $a0 is still the operand and $a1-$a3 are whatever that caller passed. Both are handed
  // over unchanged, because a callee that reads a register the body never wrote must see the
  // caller's value rather than a zero this override invented.
  psx::cpu::callGuestNow(
      *c, kCameraSphericalPresetReset, kApplySphericalPreset, c->r[4], c->r[5], c->r[6], c->r[7]);
  s0 = kCameraSphereRows;
  s1 = kCameraDestination;
  for (std::uint32_t word = 0; word < kSphericalGroupWords; ++word) {
    c->mem_w32(kCameraClearedGroup + word * 4u, 0u);
  }
  c->mem_w32(kCameraCollisionCounter, 0u);
  c->mem_w32(kCameraOffCenterFrames, 0u);
  for (std::uint32_t word = 0; word < kSphericalGroupWords; ++word) {
    c->mem_w32(kCameraSphereRows + word * 4u, c->mem_r32(kCameraLastSimulation + word * 4u));
  }
  psx::cpu::callGuestNow(*c, kCameraSphericalPresetReset, kSphericalToCartesian, s1);
  psx::cpu::callGuestNow(
      *c, kCameraSphericalPresetReset, kVecAdd, s1, s1, c->mem_r32(kCameraFocusPointer));
  s0 = kCameraPosition;
  psx::cpu::callGuestNow(*c, kCameraSphericalPresetReset, kVecCopy, s0, s1);
  psx::cpu::callGuestNow(*c, kCameraSphericalPresetReset, kUpdateSphericalCoords, s0);
  c->mem_w32(kCameraAzimuthChanged, c->r[2]);
  // The rotation update reads no argument at all, so the three words handed over here are inert;
  // $a0 is the one the retail body's last delay slot set, and $a1/$a2 are whatever the two calls
  // above left behind. Naming them keeps this call's shape identical to the `jal` it replaces.
  psx::cpu::callGuestNow(*c,
                         kCameraSphericalPresetReset,
                         kCameraRotationFromSphere,
                         s0,
                         s1,
                         c->mem_r32(kCameraFocusPointer));
  c->mem_w32(kScreenShakeFirst, 0u);
  c->mem_w32(kScreenShakeSecond, 0u);
}

} // namespace

void registerCameraOverrides(Core &core) {
  spyro::installNativeOverride(
      core, 0x800342F8u, "camera_rotation_from_sphere", cameraRotationFromSphere);
  spyro::installNativeOverride(core, 0x80033F08u, "camera_spherical_follow", cameraSphericalFollow);
  spyro::installNativeOverride(
      core, 0x80035F58u, "camera_shoulder_rotation_input", cameraShoulderRotationInput);
  spyro::installNativeOverride(
      core, 0x80033E40u, "ray_between_points_is_clear", rayBetweenPointsIsClear);
  spyro::installNativeOverride(
      core, 0x80034204u, "camera_spherical_to_cartesian", sphericalToCartesian);
  spyro::installNativeOverride(
      core, 0x80034480u, "camera_collision_update", resolveCameraCollision);
  spyro::installNativeOverride(
      core, 0x80034358u, "camera_spherical_preset_reset", resetCameraToSphericalPreset);
}

} // namespace spyro1::native
