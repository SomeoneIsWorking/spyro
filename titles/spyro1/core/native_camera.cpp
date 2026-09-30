#include "native_camera.h"

#include "guest_call.h"
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

// The guest helpers the collision body calls, named for what the decompilation's own listing says
// each one does (external/spyro-1/asm/math.s, asm/collision.s and src/camera.c). They are `jal`
// targets rather than the `lui`-plus-immediate data addresses above, so
// tools/override_constants.py re-derives each one from the `jal` that reaches it.
constexpr std::uint32_t kVecSub = 0x8001778Cu;                       // `jal` at 0x800344C0
constexpr std::uint32_t kVecMagnitude = 0x800171FCu;                 // `jal` at 0x800344CC
constexpr std::uint32_t kVecCopy = 0x80017700u;                      // `jal` at 0x800344E0
constexpr std::uint32_t kVecAdd = 0x80017758u;                       // `jal` at 0x80034428
constexpr std::uint32_t kRayBetweenPointsIsClear = 0x80033E40u;      // `jal` at 0x8003475C
constexpr std::uint32_t kUpdateSphericalCoords = 0x80033F08u;        // `jal` at 0x80034440
constexpr std::uint32_t kResetCameraToSphericalPreset = 0x80034358u; // `jal` at 0x800347E4
constexpr std::uint32_t kSphericalToCartesian = 0x80034204u;         // `jal` at 0x80034414
constexpr std::uint32_t kSphereCollisionCheck = 0x8004BE4Cu;         // `jal` at 0x80034604
constexpr std::uint32_t kRotateVectorByCamera = 0x80017AA4u;         // `jal` at 0x80034B94

// g_Camera (external/spyro-1/include/camera.h), base 0x80076DD0, and the three globals outside it
// this body touches. Each is the `lui`+`addiu` pair the retail code builds.
constexpr std::uint32_t kCameraPosition = 0x80076DF8u;    // m_Position
constexpr std::uint32_t kCameraDestination = 0x80076E04u; // m_DestinationPosition
constexpr std::uint32_t kCameraState = 0x80076E28u;       // m_State
constexpr std::uint32_t kCameraSphereRows = 0x80076E48u;  // m_Sphere: five 0x18-byte rows
constexpr std::uint32_t kCameraSimulation = 0x80076E60u;  // m_Simulation, the six-word snapshot
constexpr std::uint32_t kCameraCollisionCounter = 0x80076E94u;    // unk_0xC4
constexpr std::uint32_t kCameraOffCenterFrames = 0x80076E98u;     // m_SpyroOffCenterFrames
constexpr std::uint32_t kCameraForcedToDestination = 0x80076E9Cu; // unk_0xCC
constexpr std::uint32_t kCameraSphericalPreset = 0x80076EA8u;     // m_SphericalPreset
constexpr std::uint32_t kCameraAzimuthChanged = 0x80076EB8u;      // unk_0xE8
constexpr std::uint32_t kCollisionPoint = 0x80076B80u;            // g_CollisionPoint
constexpr std::uint32_t kSpyroColorFilterGreen = 0x80078A7Fu;     // g_Spyro.m_colorFilter.m_green
constexpr std::uint32_t kSpyroShadowUnknown1C = 0x8007AA2Cu;      // D_8007AA10.unk_1c
constexpr std::uint32_t kCameraForceFlag = kGp + 0x454u;          // D_800756B8
constexpr std::uint32_t kSphericalPresets = 0x8006CAB4u;          // D_8006CAB4[5]
constexpr std::uint32_t kRowAngleOffsets = 0x8006C82Cu;           // D_8006C82C[5][3]

constexpr char kCameraCollisionUpdate[] = "camera_collision_update";

constexpr std::uint32_t kAngleMask = 0xFFFu;
constexpr std::uint32_t kRowCount = 5u;
constexpr std::uint32_t kRowStride = 0x18u;
constexpr std::uint32_t kSavedSimulationWords = 6u;
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

// The retail body's own 0x68-byte frame, which the differential ignores as dead stack. An override
// has no frame of its own, so it runs with the same lowered $sp and keeps its two working vectors
// and its six-word snapshot at the retail's own offsets: that puts them ABOVE the frame pointer,
// where no callee frame pushed below it can reach, and inside the ignored window.
constexpr std::uint32_t kFrameBytes = 0x68u;
constexpr std::uint32_t kLiveSavedCount = 7u; // $s0-$s6; $s7 the body never writes
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
// which is exactly what the epilogue's seven loads do.
class SpilledRegisters {
public:
  SpilledRegisters(Core *core, std::uint32_t entrySp) : core_(core), entrySp_(entrySp) {
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
  }

private:
  Core *core_;
  std::uint32_t entrySp_;
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
  psx::cpu::callGuestNow(*c, kCameraCollisionUpdate, kSphericalToCartesian, kCameraDestination);
  psx::cpu::callGuestNow(*c,
                         kCameraCollisionUpdate,
                         kVecAdd,
                         kCameraDestination,
                         kCameraDestination,
                         c->mem_r32(row.address + 0x58u));
  psx::cpu::callGuestNow(*c,
                         kCameraCollisionUpdate,
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
  psx::cpu::callGuestNow(*c,
                         kCameraCollisionUpdate,
                         kRayBetweenPointsIsClear,
                         kCameraDestination,
                         c->r[29] + kWalkVectorOffset);
  if (c->r[2] == 0u) {
    return false;
  }
  psx::cpu::callGuestNow(*c,
                         kCameraCollisionUpdate,
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
  psx::cpu::callGuestNow(*c, kCameraCollisionUpdate, kUpdateSphericalCoords, kCameraDestination);
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
    psx::cpu::callGuestNow(*c, kCameraCollisionUpdate, kRotateVectorByCamera, work.walk, kSpyro);
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
  psx::cpu::callGuestNow(
      *c, kCameraCollisionUpdate, kVecSub, walkVector, kCameraDestination, kSpyro);
  psx::cpu::callGuestNow(*c, kCameraCollisionUpdate, kVecMagnitude, walkVector, 1u);
  s6 = static_cast<std::int32_t>(c->r[2]) >= kWalkStartsBelowLength
           ? 1u
           : 0u; // the "camera is far" flag
  psx::cpu::callGuestNow(*c, kCameraCollisionUpdate, kVecCopy, stepVector, kCameraDestination);
  psx::cpu::callGuestNow(
      *c, kCameraCollisionUpdate, kVecSub, walkVector, kCameraDestination, kCameraPosition);
  psx::cpu::callGuestNow(*c, kCameraCollisionUpdate, kVecMagnitude, walkVector, 1u);
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
  psx::cpu::callGuestNow(*c, kCameraCollisionUpdate, kVecCopy, kCameraDestination, kCameraPosition);

  s0 = 0u;
  while (static_cast<std::int32_t>(s0) < steps) {
    psx::cpu::callGuestNow(
        *c, kCameraCollisionUpdate, kVecAdd, kCameraDestination, kCameraDestination, walkVector);
    s1 = 0u;
    for (;;) {
      psx::cpu::callGuestNow(*c,
                             kCameraCollisionUpdate,
                             kSphereCollisionCheck,
                             kCameraDestination,
                             kSphereProbeRadius,
                             kSphereProbeRadius);
      if (c->r[2] == 0u) {
        break;
      }
      psx::cpu::callGuestNow(
          *c, kCameraCollisionUpdate, kVecCopy, kCameraDestination, kCollisionPoint);
      s1 += 1u;
      s4 = 1u; // the delay slot of the row test, so it runs on both arms
      if (static_cast<std::int32_t>(s1) >= kWalkProbeLimit) {
        break;
      }
    }
    s0 += 1u;
    if (static_cast<std::int32_t>(s1) == kWalkProbeLimit) {
      psx::cpu::callGuestNow(*c, kCameraCollisionUpdate, kVecCopy, kCameraDestination, stepVector);
      s0 = static_cast<std::uint32_t>(steps + 1);
    }
  }

  s0 = kCameraForcedToDestination;
  c->mem_w32(s0, 0u); // the delay slot of the hits < 6 test
  c->r[2] = static_cast<std::int32_t>(s1) < kWalkProbeLimit ? 1u : 0u;
  if (c->r[2] != 0u) {
    if (s4 == 0u) {
      psx::cpu::callGuestNow(
          *c, kCameraCollisionUpdate, kVecCopy, kCameraPosition, kCameraDestination);
      c->r[2] = kForcedToDestination;
      c->mem_w32(s0, kForcedToDestination);
    } else {
      psx::cpu::callGuestNow(
          *c, kCameraCollisionUpdate, kVecSub, stepVector, kCameraDestination, kCameraPosition);
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
      psx::cpu::callGuestNow(
          *c, kCameraCollisionUpdate, kVecAdd, kCameraPosition, kCameraPosition, stepVector);
    }
  }

  s2 = kCameraPosition;
  psx::cpu::callGuestNow(*c, kCameraCollisionUpdate, kUpdateSphericalCoords, kCameraPosition);
  c->mem_w32(kCameraAzimuthChanged, c->r[2]);
  psx::cpu::callGuestNow(*c, kCameraCollisionUpdate, kVecCopy, walkVector, kSpyro);
  c->mem_w32(walkVector + 8u, c->mem_r32(walkVector + 8u) - kSpyroProbeDepth);
  psx::cpu::callGuestNow(
      *c, kCameraCollisionUpdate, kRayBetweenPointsIsClear, kCameraPosition, walkVector);
  if (c->r[2] != 0u) {
    psx::cpu::callGuestNow(
        *c, kCameraCollisionUpdate, kRayBetweenPointsIsClear, walkVector, kCameraPosition);
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
      psx::cpu::callGuestNow(
          *c, kCameraCollisionUpdate, kResetCameraToSphericalPreset, presetAddress);
      psx::cpu::callGuestNow(*c,
                             kCameraCollisionUpdate,
                             kSphereCollisionCheck,
                             s3,
                             kSphereProbeRadius,
                             kSphereProbeRadius);
      if (c->r[2] == 0u) {
        psx::cpu::callGuestNow(*c, kCameraCollisionUpdate, kRayBetweenPointsIsClear, s3, s2);
        if (c->r[2] != 0u) {
          psx::cpu::callGuestNow(*c, kCameraCollisionUpdate, kRayBetweenPointsIsClear, s2, s3);
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
      if (testSphericalRow(c, candidate, pass, pass == 0u ? 1u << row : 0u, &s5)) {
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

} // namespace

void registerCameraOverrides(Core &core) {
  spyro::installNativeOverride(
      core, 0x800342F8u, "camera_rotation_from_sphere", cameraRotationFromSphere);
  spyro::installNativeOverride(
      core, 0x80035F58u, "camera_shoulder_rotation_input", cameraShoulderRotationInput);
  spyro::installNativeOverride(
      core, 0x80034480u, "camera_collision_update", resolveCameraCollision);
}

} // namespace spyro1::native
