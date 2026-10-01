#include "spyro2_moby_visibility.h"

#include "core.h"
#include "native_execution.h"
#include "spyro2_moby_frustum.h"
#include "spyro2_moby_gte.h"
#include "spyro2_moby_rotation.h"
#include "spyro2_render_globals.h"
#include "spyro2_widescreen.h"

#include <array>

namespace spyro2 {
namespace {

namespace gte = moby_gte;
using moby_frustum::HorizontalSlope;
using moby_frustum::ViewPoint;
using moby_rotation::RotationWords;

// ── Guest globals, each named by the instruction that reaches it ─────────────────────────────────
constexpr std::uint32_t kFirstRecordWord = 0x80066F14u; // 800438A4: the level's moby array
constexpr std::uint32_t kDrawerParameter = 0x80066FA4u; // 800438B0: parked in VZ1 for the drawer
constexpr std::uint32_t kMeshSlotTable = 0x80068C94u;   // 800438E4 ($sp): per-mesh slot arrays
constexpr std::uint32_t kOverlayTable = 0x80062868u;    // 80043F0C: overlay byte per record kind
constexpr std::uint32_t kCloseCursorHome = 0x80068214u; // 800440D8: close-list cursor, at exit
constexpr std::uint32_t kExitV0 = 0x8006820Cu;          // 800440D0: v0 on return

// The scratch block's regions, as offsets below [render_globals::kScratchBaseWord]
// (800438B4..800438E0).
constexpr std::uint32_t kDrawerScratchBelow = 0x3000; // VXY1
constexpr std::uint32_t kCloseListBelow = 0x1C00;     // LO
constexpr std::uint32_t kDeferredBelow = 0x1400;      // CR8
constexpr std::uint32_t kDeferredEndBelow = 0x1010;   // CR9
constexpr std::uint32_t kRenderListBelow = 0x1000;    // fp
constexpr std::uint32_t kRenderListEndBelow = 0x10;   // HI

// ── Moby record (0x58 bytes) ───────────────────────────────────────────────────────────────────
constexpr std::uint32_t kRecordStride = 0x58;
namespace record {
constexpr std::uint32_t kX = 0x0C;
constexpr std::uint32_t kY = 0x10;
constexpr std::uint32_t kZ = 0x14;
constexpr std::uint32_t kCloseFlag = 0x1C; // negative: listed when near
constexpr std::uint32_t kMeshIndex = 0x36; // u16 index into kMeshSlotTable
constexpr std::uint32_t kPacked = 0x3C;    // slot byte, texture and overlay selectors
constexpr std::uint32_t kKind = 0x40;      // u8 index into kOverlayTable
constexpr std::uint32_t kAngles = 0x44;    // three angle bytes; top byte is the shade bias
constexpr std::uint32_t kFlag = 0x48;      // i8: negative skips, -1 ends the walk
constexpr std::uint32_t kReach = 0x4C;     // u16: i8 reach in its low byte
constexpr std::uint32_t kClass = 0x4D;     // u8 written: 0, 1 edge, 2 inside (high byte of kReach)
constexpr std::uint32_t kDepthBias = 0x4E; // u16: bias byte, GPF scale byte
constexpr std::uint32_t kGroup = 0x52;     // u8 visibility group, >= 0xFE always visible
constexpr std::uint32_t kFlags = 0x54;     // bit 30 interpolated angles, bit 28 mirrored
} // namespace record

// ── Render-list entry (0x44 bytes) ───────────────────────────────────────────────────────────────
constexpr std::uint32_t kEntryStride = 0x44;
namespace entry {
constexpr std::uint32_t kClass = 0x00;
constexpr std::uint32_t kOverlay = 0x01;
constexpr std::uint32_t kDepthBias = 0x02;
constexpr std::uint32_t kMesh = 0x04;
constexpr std::uint32_t kOverlayMesh = 0x08;
constexpr std::uint32_t kTexture = 0x0C;
constexpr std::uint32_t kOverlayTexture = 0x10;
constexpr std::uint32_t kAnimation = 0x14;
constexpr std::uint32_t kShade = 0x18;
constexpr std::uint32_t kDetail = 0x1A;
constexpr std::uint32_t kDetailLimit = 0x1B;
constexpr std::uint32_t kCentre = 0x1C; // x, y, z words
constexpr std::uint32_t kRotation = 0x28;
constexpr std::uint32_t kDetailDepth = 0x3A;
constexpr std::uint32_t kFlags = 0x3C;
constexpr std::uint32_t kRecord = 0x40;
} // namespace entry

// Mesh header fields.
constexpr std::uint32_t kMeshDetail = 0x02; // u16: centre shift (low), detail level (high)
constexpr std::uint32_t kMeshOverlayDetail = 0x03;
constexpr std::uint32_t kMeshRadius = 0x07;    // u8, x16
constexpr std::uint32_t kMeshSubSphere = 0x08; // packed i8 x, y, z and u8 radius, x8
constexpr std::uint32_t kMeshTextures = 0x24;
constexpr std::uint32_t kMeshSlotsAnimation = 0x34;
constexpr std::uint32_t kMeshCloseByte = 0x2B;
constexpr std::uint32_t kSlotMesh = 0x3C;

constexpr std::uint32_t kInterpolatedAngles = 1u << 30;
constexpr std::uint32_t kMirrored = 1u << 28;
constexpr std::int32_t kCloseDepth = 0x1200;
constexpr std::int32_t kFarDepth = 63000;
constexpr std::uint32_t kDeferredUnbounded = 0xFFFF;
constexpr std::uint32_t kNegativeTextureBits = 0xFC000000u;

constexpr std::int32_t asSigned(std::uint32_t value) {
  return static_cast<std::int32_t>(value);
}

constexpr std::uint32_t asWord(std::int32_t value) {
  return static_cast<std::uint32_t>(value);
}

// The register numbers retail saves, in save-area order.

// The horizontal slope the walk culls with: retail's unless the widescreen plan widens.
HorizontalSlope slopeFor(Core &core) {
  const WidescreenOwner &widescreen = WidescreenOwner::of(core);
  if (!widescreen.latched() || !widescreen.plan().widescreen()) {
    return moby_frustum::kRetailSlope;
  }
  const GuestProjectionPlan &plan = widescreen.plan();
  return moby_frustum::widenedSlope(plan.nativeProjectionExtent.width, plan.projectionExtent.width);
}

class MobyWalk {
public:
  MobyWalk(Core &core, HorizontalSlope slope) : core_(core), slope_(slope) {}

  void run();

private:
  enum class Step : std::uint8_t {
    Next,
    End,
  };

  // The record's mesh slot array and its packed selector word.
  struct MeshSlots {
    std::uint32_t slots = 0;
    std::uint32_t packed = 0;
    [[nodiscard]] std::uint32_t slot() const {
      return (packed & 0xFFu) * 4 + slots;
    }
  };

  Step visit(std::uint32_t rec);
  MeshSlots meshSlots(std::uint32_t rec);
  void defer(
      std::uint32_t rec, std::uint32_t x, std::uint32_t y, std::uint32_t tail, std::uint32_t slot);
  Step drawCandidate(std::uint32_t rec,
                     std::int32_t dx,
                     std::int32_t dy,
                     std::int32_t dz,
                     std::int32_t drawDistance,
                     const MeshSlots &slots);
  void listClose(std::uint32_t rec, std::int32_t depth, std::uint32_t mesh, const MeshSlots &slots);
  RotationWords orient(std::uint32_t rec);
  [[nodiscard]] bool subSphereRejects(std::uint32_t mesh);
  Step finishEntry(std::uint32_t rec, std::uint32_t mesh, const MeshSlots &slots);

  Core &core_;
  HorizontalSlope slope_;
  RotationWords camera_{};
  std::int32_t cameraX_ = 0;
  std::int32_t cameraY_ = 0;
  std::int32_t cameraZ_ = 0;
  std::uint32_t entry_ = 0;
  std::uint32_t listEnd_ = 0;
  std::uint32_t closeCursor_ = 0;
  // Retail's v1 at return is the last scratch value it held. The O32 contract treats v1 as a
  // result register, so the walk carries the same value to the same exit.
  std::uint32_t v1_ = 0;
};

void MobyWalk::run() {
  render_globals::spillBorrowedRegisters(core_);
  const std::uint32_t base = core_.mem_r32(render_globals::kScratchBaseWord);
  const std::uint32_t firstRecord = core_.mem_r32(kFirstRecordWord);
  gte_write_data(gte::kVxy1, base - kDrawerScratchBelow);
  gte_write_data(gte::kVz1, core_.mem_r32(kDrawerParameter));
  entry_ = base - kRenderListBelow;
  listEnd_ = base - kRenderListEndBelow;
  closeCursor_ = base - kCloseListBelow;
  gte_write_ctrl(gte::kDeferredCursor, base - kDeferredBelow);
  gte_write_ctrl(gte::kDeferredEnd, base - kDeferredEndBelow);
  cameraX_ = asSigned(core_.mem_r32(render_globals::kCameraPosition));
  cameraY_ = asSigned(core_.mem_r32(render_globals::kCameraPosition + 4));
  cameraZ_ = asSigned(core_.mem_r32(render_globals::kCameraPosition + 8));
  for (std::uint32_t i = 0; i < gte::kRotationWords; ++i) {
    camera_[i] = core_.mem_r32(render_globals::kCameraRotation + 4 * i);
  }

  std::uint32_t rec = firstRecord;
  while (visit(rec) == Step::Next) {
    rec += kRecordStride;
  }

  core_.mem_w32(kCloseCursorHome, closeCursor_);
  const std::uint32_t deferred = gte_read_ctrl(gte::kDeferredCursor);
  core_.mem_w32(entry_, 0);
  core_.mem_w32(deferred, 0);
  core_.r[2] = kExitV0;
  core_.r[3] = v1_;
}

MobyWalk::Step MobyWalk::visit(std::uint32_t rec) {
  const std::int32_t flag = core_.mem_r8s(rec + record::kFlag);
  if (flag < 0) {
    return flag == -1 ? Step::End : Step::Next;
  }
  const std::uint32_t reachWord = core_.mem_r16(rec + record::kReach);
  core_.mem_w8(rec + record::kClass, 0);
  // The reach byte is signed and in 256-unit steps. The previous frame's class (the high byte of
  // the same halfword, read before it was cleared) extends it by 256 or 512.
  const std::int32_t reach = static_cast<std::int8_t>(reachWord & 0xFFu) * 256;
  const std::int32_t drawDistance = reach + static_cast<std::int32_t>(reachWord & 0x300u);
  if (reach <= 0) {
    if (reach == 0) {
      return Step::Next;
    }
    const std::uint32_t z = core_.mem_r32(rec + record::kZ);
    v1_ = z;
    const MeshSlots mesh = meshSlots(rec);
    defer(rec,
          core_.mem_r32(rec + record::kX),
          core_.mem_r32(rec + record::kY),
          kDeferredUnbounded,
          mesh.slot());
    return Step::Next;
  }

  const std::int32_t dx = asSigned(core_.mem_r32(rec + record::kX) - asWord(cameraX_)) >> 2;
  if (dx + drawDistance <= 0 || dx - drawDistance >= 0) {
    return Step::Next;
  }
  const std::uint32_t rawZ = core_.mem_r32(rec + record::kZ);
  v1_ = rawZ;
  const std::int32_t dy = asSigned(asWord(cameraY_) - core_.mem_r32(rec + record::kY)) >> 2;
  if (dy + drawDistance <= 0) {
    return Step::Next;
  }
  v1_ = asWord(cameraZ_) - rawZ;
  if (dy - drawDistance >= 0) {
    return Step::Next;
  }
  const std::int32_t dz = asSigned(v1_) >> 2;
  v1_ = asWord(dz);
  if (dz + drawDistance <= 0 || dz - drawDistance >= 0) {
    return Step::Next;
  }
  const std::uint32_t group = core_.mem_r8(rec + record::kGroup);
  if (group < 0xFE && core_.mem_r8(render_globals::kVisibilityGroups + group) == 0) {
    return Step::Next;
  }
  const MeshSlots mesh = meshSlots(rec);
  if (asSigned(mesh.slots) > 0) {
    // The mesh is not resident: its slot word is not a pointer. Hand the moby on with its
    // camera-relative centre and reach instead of drawing it.
    defer(rec, asWord(dx), asWord(dy), asWord(drawDistance), mesh.slot());
    return Step::Next;
  }
  return drawCandidate(rec, dx, dy, dz, drawDistance, mesh);
}

MobyWalk::MeshSlots MobyWalk::meshSlots(std::uint32_t rec) {
  const std::uint32_t index = core_.mem_r16(rec + record::kMeshIndex);
  return MeshSlots{
      .slots = core_.mem_r32(kMeshSlotTable + index * 4),
      .packed = core_.mem_r32(rec + record::kPacked),
  };
}

// 8004415C..80044194. The record pointer is stored even when the buffer is full; the rest of the
// slot is written and the cursor advanced only when it is not.
void MobyWalk::defer(
    std::uint32_t rec, std::uint32_t x, std::uint32_t y, std::uint32_t tail, std::uint32_t slot) {
  const std::uint32_t cursor = gte_read_ctrl(gte::kDeferredCursor);
  const std::uint32_t end = gte_read_ctrl(gte::kDeferredEnd);
  core_.mem_w32(cursor, rec);
  if (cursor == end) {
    return;
  }
  core_.mem_w32(cursor + 4, (x << 16) + (y & 0xFFFFu));
  v1_ = (v1_ << 16) + tail;
  core_.mem_w32(cursor + 8, v1_);
  core_.mem_w32(cursor + 12, slot);
  gte_write_ctrl(gte::kDeferredCursor, cursor + 16);
}

// 800439F4..80043B04: rotate the centre into view space and cull its bounding sphere.
MobyWalk::Step MobyWalk::drawCandidate(std::uint32_t rec,
                                       std::int32_t dx,
                                       std::int32_t dy,
                                       std::int32_t dz,
                                       std::int32_t drawDistance,
                                       const MeshSlots &slots) {
  const std::uint32_t mesh = core_.mem_r32(slots.slot() + kSlotMesh);
  for (std::uint32_t i = 0; i < gte::kRotationWords; ++i) {
    gte_write_ctrl(gte::kRotation0 + i, camera_[i]);
  }
  const std::uint32_t radius = static_cast<std::uint32_t>(core_.mem_r8(mesh + kMeshRadius)) << 4;
  gte_write_data(gte::kIr3, asWord(dx));
  gte_write_data(gte::kIr1, asWord(dy));
  gte_write_data(gte::kIr2, asWord(dz));
  gte_op(&core_, gte::kRotateIr);
  const ViewPoint view{asSigned(gte_read_data(gte::kMac1)),
                       asSigned(gte_read_data(gte::kMac2)),
                       asSigned(gte_read_data(gte::kMac3))};
  v1_ = asWord(view.z);
  if (view.z - drawDistance >= 0 || moby_frustum::behindEye(view, radius)) {
    return Step::Next;
  }
  const moby_frustum::SphereMargins margins = moby_frustum::sphereMargins(radius);
  if (moby_frustum::outsideHorizontal(view, margins, slope_)) {
    return Step::Next;
  }
  listClose(rec, view.z, mesh, slots);
  if (moby_frustum::outsideVertical(view, margins)) {
    return Step::Next;
  }
  gte_write_ctrl(gte::kTranslationX, asWord(view.x));
  gte_write_ctrl(gte::kTranslationY, asWord(view.y));
  gte_write_ctrl(gte::kTranslationZ, asWord(view.z));
  const std::uint32_t mobyClass = moby_frustum::insideFrustum(view, margins, slope_) ? 2u : 1u;
  core_.mem_w8(rec + record::kClass, static_cast<std::uint8_t>(mobyClass));

  const RotationWords rotation = orient(rec);
  core_.mem_w32(entry_ + entry::kClass, mobyClass);
  for (std::uint32_t i = 0; i < gte::kRotationWords; ++i) {
    core_.mem_w32(entry_ + entry::kRotation + 4 * i, rotation[i]);
  }
  core_.mem_w32(entry_ + entry::kRecord, rec);
  if (mobyClass == 1 && subSphereRejects(mesh)) {
    return Step::Next;
  }
  return finishEntry(rec, mesh, slots);
}

// 80043A94..80043AD8: a moby flagged close and nearer than kCloseDepth is also listed with one
// byte of its mesh, whatever the vertical test then decides.
void MobyWalk::listClose(std::uint32_t rec,
                         std::int32_t depth,
                         std::uint32_t mesh,
                         const MeshSlots &slots) {
  if (asSigned(core_.mem_r32(rec + record::kCloseFlag)) >= 0 || depth - kCloseDepth >= 0) {
    return;
  }
  const std::uint32_t byte = core_.mem_r8(((slots.packed >> 13) & 0x7F8u) + mesh + kMeshCloseByte);
  core_.mem_w32(closeCursor_, rec);
  core_.mem_w32(closeCursor_ + 4, byte);
  closeCursor_ += 8;
}

// 80043B68..80043D7C and 8004419C..80044500: compose the moby's orientation onto the camera.
RotationWords MobyWalk::orient(std::uint32_t rec) {
  const std::uint32_t flags = core_.mem_r32(rec + record::kFlags);
  const std::uint32_t angles = core_.mem_r32(rec + record::kAngles);
  RotationWords rotation = camera_;
  v1_ = moby_rotation::kSineTable;
  core_.mem_w32(entry_ + entry::kFlags, flags);
  if ((flags & kInterpolatedAngles) == 0) {
    moby_rotation::composeTabled(core_, angles, rotation);
    return rotation;
  }
  if ((flags & kMirrored) == 0) {
    core_.mem_w32(entry_ + entry::kFlags, 0);
  }
  // Retail's multiplies need HI/LO, so it parks the close-list cursor and the list end in two
  // light-matrix registers for the duration and leaves them there.
  gte_write_ctrl(gte::kParkedCursor, closeCursor_);
  gte_write_ctrl(gte::kParkedListEnd, listEnd_);
  moby_rotation::composeInterpolated(core_, angles, flags, rotation);
  if ((flags & kMirrored) == 0) {
    v1_ = rotation[0] & 0xFFFFu;
    return rotation;
  }
  v1_ = rotation[3] & 0xFFFFu;
  moby_rotation::mirror(rotation);
  return rotation;
}

// 80043DA8..80043EE8: a class-1 moby whose mesh names a second sphere is culled on it too, and
// promoted to class 2 when that sphere is fully inside.
bool MobyWalk::subSphereRejects(std::uint32_t mesh) {
  const std::uint32_t sphere = core_.mem_r32(mesh + kMeshSubSphere);
  if (sphere == 0) {
    return false;
  }
  const auto signedByte = [sphere](std::uint32_t shift) {
    return static_cast<std::int32_t>(static_cast<std::int8_t>((sphere >> shift) & 0xFFu)) * 8;
  };
  gte_write_data(gte::kIr3, asWord(signedByte(24)));
  gte_write_data(gte::kIr1, asWord(-signedByte(16)));
  gte_write_data(gte::kIr2, asWord(-signedByte(8)));
  const std::uint32_t radius = (sphere & 0xFFu) << 3;
  gte_op(&core_, gte::kRotateIrPlusTr);
  const ViewPoint view{asSigned(gte_read_data(gte::kMac1)),
                       asSigned(gte_read_data(gte::kMac2)),
                       asSigned(gte_read_data(gte::kMac3))};
  if (moby_frustum::behindEye(view, radius)) {
    return true;
  }
  const moby_frustum::SphereMargins margins = moby_frustum::sphereMargins(radius);
  if (moby_frustum::outsideHorizontal(view, margins, slope_) ||
      moby_frustum::outsideVertical(view, margins)) {
    return true;
  }
  if (moby_frustum::insideFrustum(view, margins, slope_)) {
    core_.mem_w32(entry_ + entry::kClass, 2);
  }
  return false;
}

// 80043EEC..800440C8: mesh, overlay, shading, detail and the projected centre.
MobyWalk::Step
MobyWalk::finishEntry(std::uint32_t rec, std::uint32_t mesh, const MeshSlots &slots) {
  const std::int32_t cx = asSigned(gte_read_ctrl(gte::kTranslationX));
  const std::int32_t cy = asSigned(gte_read_ctrl(gte::kTranslationY));
  const std::int32_t cz = asSigned(gte_read_ctrl(gte::kTranslationZ));
  const std::uint32_t biasWord = core_.mem_r16(rec + record::kDepthBias);
  const std::uint32_t overlay = core_.mem_r8(kOverlayTable + core_.mem_r8(rec + record::kKind));
  const std::int32_t depthBias = static_cast<std::int32_t>((biasWord & 0xFFu) << 8) - cz;
  const std::uint32_t scale = biasWord >> 8;
  std::uint32_t texture = core_.mem_r32(((slots.packed >> 13) & 0x7F8u) + mesh + kMeshTextures);
  const std::uint32_t detailWord = core_.mem_r16(mesh + kMeshDetail);
  const std::uint32_t shift = detailWord & 0xFFu;
  std::uint32_t detail = detailWord >> 8;
  if (overlay != 0) {
    const std::uint32_t overlayMesh =
        core_.mem_r32(((slots.packed >> 6) & 0x3FCu) + slots.slots + kSlotMesh);
    const std::uint32_t overlayDetail = core_.mem_r8(overlayMesh + kMeshOverlayDetail);
    const std::uint32_t overlayTexture =
        core_.mem_r32(((slots.packed >> 21) & 0x7F8u) + overlayMesh + kMeshTextures);
    if (asSigned(overlayDetail - detail) > 0) {
      detail = overlayDetail;
    }
    core_.mem_w8(entry_ + entry::kOverlay, static_cast<std::uint8_t>(overlay));
    core_.mem_w32(entry_ + entry::kOverlayMesh, overlayMesh);
    core_.mem_w32(entry_ + entry::kOverlayTexture, overlayTexture);
  }
  core_.mem_w8(entry_ + entry::kDetail, static_cast<std::uint8_t>(detail));
  core_.mem_w8(entry_ + entry::kDetailLimit, static_cast<std::uint8_t>(shift + detail));

  const std::int32_t angles = asSigned(core_.mem_r32(rec + record::kAngles));
  const std::int32_t x4 = asSigned(asWord(cx) << 2);
  const std::int32_t y4 = asSigned(asWord(cy) << 2);
  const std::int32_t z4 = asSigned(asWord(cz) << 2);
  std::int32_t shade = (z4 >> 7) - (angles >> 24);
  core_.mem_w16(entry_ + entry::kDepthBias, static_cast<std::uint16_t>(depthBias));
  if (shade < 0) {
    shade = 0;
  }
  if (shade - 0x110 >= 0) {
    shade += 0x20;
    if (shade - 0x1C0 >= 0) {
      const std::uint32_t mark =
          core_.mem_r32(render_globals::kOrderingTable) + asWord(shade << 3) + 0x200;
      if (asSigned(core_.mem_r32(render_globals::kOrderingTableMark) - mark) < 0) {
        core_.mem_w32(render_globals::kOrderingTableMark, mark);
      }
    }
  }
  core_.mem_w32(entry_ + entry::kMesh, mesh);
  if (depthBias < 0) {
    texture |= kNegativeTextureBits;
  }
  core_.mem_w32(entry_ + entry::kTexture, texture);
  core_.mem_w16(entry_ + entry::kShade, static_cast<std::uint16_t>(shade));

  const std::uint32_t amount = shift & 31u;
  std::int32_t x = x4 >> amount;
  std::int32_t y = y4 >> amount;
  std::int32_t z = z4 >> amount;
  if (scale != 0) {
    gte_write_data(gte::kIr0, scale);
    gte_write_data(gte::kIr1, asWord(x));
    gte_write_data(gte::kIr2, asWord(y));
    gte_write_data(gte::kIr3, asWord(z));
    gte_op(&core_, gte::kScaleIr);
    z = asSigned(gte_read_data(gte::kMac3)) >> 5;
    y = asSigned(gte_read_data(gte::kMac2)) >> 5;
    x = asSigned(gte_read_data(gte::kMac1)) >> 5;
  }
  v1_ = asWord(z);
  if (z - kFarDepth > 0) {
    return Step::Next;
  }
  const std::int32_t detailDepth = asSigned(0x200u << (detail & 31u));
  if (asSigned(texture) >= 0) {
    const std::uint32_t animations = core_.mem_r32(slots.slots + kMeshSlotsAnimation);
    core_.mem_w32(entry_ + entry::kAnimation,
                  animations + core_.mem_r32(((texture >> 24) & 0x7Cu) + animations));
  }
  core_.mem_w16(entry_ + entry::kDetailDepth,
                static_cast<std::uint16_t>(asSigned(asWord(z) - asWord(detailDepth)) >> 1));
  core_.mem_w32(entry_ + entry::kCentre, asWord(x));
  core_.mem_w32(entry_ + entry::kCentre + 4, asWord(y));
  core_.mem_w32(entry_ + entry::kCentre + 8, asWord(z));
  entry_ += kEntryStride;
  return entry_ == listEnd_ ? Step::End : Step::Next;
}

void mobyVisibility(Core *core) {
  MobyWalk(*core, slopeFor(*core)).run();
}

} // namespace

void registerMobyVisibilityOverride(Core &core) {
  spyro::installNativeOverride(
      core, kMobyVisibilityEntry, "spyro2-moby-visibility", mobyVisibility);
}

} // namespace spyro2
