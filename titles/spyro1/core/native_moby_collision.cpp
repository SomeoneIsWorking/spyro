#include "native_moby_collision.h"

#include "game.h"
#include "guest_globals.h"
#include "native_execution.h"

#include <lucent/log.h>

#include <cstdint>

namespace spyro1::native {
namespace {

// kModelSoundTables is the shape table the moby shape walks (`lui 0x8007 ; addiu 0x6378`);
// kRegisterSpillArea is the save area this body and func_8004AE38 spill their callee-saved
// registers into.
using spyro::guest::kModelSoundTables;
using spyro::guest::kRegisterSpillArea;

// The GTE register numbers this body touches, from external/spyro-1's own asm/c2regs.inc.
constexpr std::uint32_t kGteVxy0 = 0u;
constexpr std::uint32_t kGteVz0 = 1u;
constexpr std::uint32_t kGteIr0 = 8u;
constexpr std::uint32_t kGteIr1 = 9u;
constexpr std::uint32_t kGteIr2 = 10u;
constexpr std::uint32_t kGteIr3 = 11u;
constexpr std::uint32_t kGteMac1 = 25u;
constexpr std::uint32_t kGteMac2 = 26u;
constexpr std::uint32_t kGteMac3 = 27u;
constexpr std::uint32_t kGteLzcs = 30u;
constexpr std::uint32_t kGteLzcr = 31u;
constexpr std::uint32_t kGteRot11Rot12 = 0u;
constexpr std::uint32_t kGteRot13Rot21 = 1u;
constexpr std::uint32_t kGteRot22Rot23 = 2u;
constexpr std::uint32_t kGteRot31Rot32 = 3u;
constexpr std::uint32_t kGteRot33 = 4u;
constexpr std::uint32_t kGteTransX = 5u;
constexpr std::uint32_t kGteTransY = 6u;
constexpr std::uint32_t kGteTransZ = 7u;

// The three GTE commands the body issues, as the guest's own instruction words.
constexpr std::uint32_t kGteSqr = 0x4AA00428u;
constexpr std::uint32_t kGteRtps = 0x4A180001u;
constexpr std::uint32_t kGteGpf = 0x4B90003Du;

// g_MobyCollisionChain: the cell table the candidate list indexes with the position's 13-bit cell
// coordinates (`lui 0x8007 ; addiu 0x5778`).
constexpr std::uint32_t kMobyCollisionChain = 0x80075778u;
// The candidate list the body assembles in scratch expansion RAM before walking it.
constexpr std::uint32_t kCandidateList = 0x1F800000u;
// The cell-edge bit that decides whether the neighbouring cells join the candidate list, and the
// bounds that turn a wrapping neighbour into the -1 which drops it.
constexpr std::uint32_t kCellEdge = 0x1000u;
constexpr std::uint32_t kCellColumnLimit = 0x20u;
constexpr std::uint32_t kCellRowStride = 0x20u;
constexpr std::uint32_t kCellRowLimit = 0x400u;

// The shape types the dispatch reaches, from the computed jump at 0x8004E5B0, which lands at
// 0x8004E5B8 + ((word & 0xFF00) >> 5) — eight bytes per type byte. Bytes 0..3 are the four `j`
// trampolines the compiler laid down, one per case, and bytes 4, 0x16, 0x2C and 0x4F are the four
// case bodies that same table indexes into directly. The four trampolines are NOT interchangeable
// with each other: byte 1 is the slab case, not the point case, however neatly the four sit side by
// side. Any other byte lands the jump inside a case, which is unreachable guest state rather than a
// behaviour to reproduce, so the walk ends the record's shape list instead.
constexpr std::uint32_t kShapePointJump = 0x0000u;
constexpr std::uint32_t kShapePoint = 0x0400u;
constexpr std::uint32_t kShapeSlabJump = 0x0100u;
constexpr std::uint32_t kShapeSlab = 0x1600u;
constexpr std::uint32_t kShapeBoxJump = 0x0200u;
constexpr std::uint32_t kShapeBox = 0x2C00u;
constexpr std::uint32_t kShapeMobyJump = 0x0300u;
constexpr std::uint32_t kShapeMoby = 0x4F00u;

// What the body carries through the walk: the position it resolves, the value $v1 leaves holding,
// and the two result registers the epilogue chooses between.
struct Walker {
  Core *c = nullptr;
  std::uint32_t pos = 0u;       // $a0, and $gp for the resolved-position store
  std::uint32_t radius = 0u;    // $a1
  std::uint32_t damageOut = 0u; // $a2: hit flags accumulate here, 0 when the caller passed none
  std::uint32_t damageIn = 0u;  // $a3: ORed into the record of every shape that hits
  std::uint32_t ignore = 0u;    // the 5th argument, at entry sp+0x10: the moby to skip
  std::uint32_t flags = 0u;     // the 6th argument, at entry sp+0x14
  std::uint32_t x = 0u;         // s7/t8/t9: the position being resolved
  std::uint32_t y = 0u;
  std::uint32_t z = 0u;
  std::uint32_t v1 = 0u;
  std::uint32_t firstHit = 0u; // s2: the record of the shape that hit
  std::uint32_t resolved = 0u; // s1: the record the resolve loop settled on
};

// The record's position minus the position being resolved: what the GTE squares.
struct Offset {
  std::uint32_t x;
  std::uint32_t y;
  std::uint32_t z;
};

// The bounding-sphere separation, live across the whole shape loop exactly as the guest holds it in
// $a2/$a3/$t1 after its SQR.
struct Separation {
  std::uint32_t mac1;
  std::uint32_t mac2;
  std::uint32_t squared;
};

// What one shape's test produced. A miss takes the next shape; a hit carries the four values the
// resolve loop starts from, plus where the next shape record begins.
struct ShapeOutcome {
  bool hit = false;
  std::uint32_t shape = 0u;
  std::uint32_t step = 0u;
  std::uint32_t target = 0u;
  std::uint32_t normalX = 0u;
  std::uint32_t normalY = 0u;
  std::uint32_t normalZ = 0u;
};

// DIAGNOSTIC, REMOVED BEFORE LANDING: the GTE control state this override INHERITS at entry.
//
// The guest's `rtps` at 0x8004E550 runs on whatever rotation the caller chain left in the control
// registers: `find_op.py 0x8004E3C8 0x8004EB70 lwc2 swc2` matched 0, and `mtc2`/`mfc2` matched only
// the three separation writes and the three RTPS reads, so nothing here loads a matrix. The control
// reader is Core's own `gte_read_ctrl` (runtime/psx/core.h:242), so the native can ask the same
// question the guest is answering rather than assuming identity.
//
// The census reports its DENOMINATOR on every run, because "no entry was non-identity" and "nothing
// was counted" are otherwise the same line of evidence, and it caps the printed examples while
// still counting every one of them.
constexpr std::uint32_t kEntryMatrixMaxExamples = 8u;

struct EntryMatrixCensus {
  std::uint32_t calls = 0u;
  std::uint32_t nonIdentityRotation = 0u;
  std::uint32_t zeroTranslation = 0u;
  std::uint32_t printed = 0u;
  ~EntryMatrixCensus() {
    lucent::info("moby-entry-matrix",
                 "calls={} non_identity_rotation={} zero_translation={} printed_examples={} of "
                 "at most {}",
                 calls,
                 nonIdentityRotation,
                 zeroTranslation,
                 printed,
                 kEntryMatrixMaxExamples);
  }
};

EntryMatrixCensus gEntryMatrixCensus{};

void recordEntryMatrix() {
  const std::uint32_t rot11 = gte_read_ctrl(kGteRot11Rot12);
  const std::uint32_t rot13 = gte_read_ctrl(kGteRot13Rot21);
  const std::uint32_t rot22 = gte_read_ctrl(kGteRot22Rot23);
  const std::uint32_t rot31 = gte_read_ctrl(kGteRot31Rot32);
  const std::uint32_t rot33 = gte_read_ctrl(kGteRot33);
  const std::uint32_t trX = gte_read_ctrl(kGteTransX);
  const std::uint32_t trY = gte_read_ctrl(kGteTransY);
  const std::uint32_t trZ = gte_read_ctrl(kGteTransZ);
  ++gEntryMatrixCensus.calls;
  // Identity is 0x00010000 in the two packed rotation words and 0 in the three single ones.
  const bool identity =
      rot11 == 0x00010000u && rot13 == 0u && rot22 == 0u && rot31 == 0u && rot33 == 0x10000u;
  if (!identity) {
    ++gEntryMatrixCensus.nonIdentityRotation;
  }
  if ((trX | trY | trZ) == 0u) {
    ++gEntryMatrixCensus.zeroTranslation;
  }
  if (!identity && gEntryMatrixCensus.printed < kEntryMatrixMaxExamples) {
    ++gEntryMatrixCensus.printed;
    lucent::info("moby-entry-matrix",
                 "entry call {} rotation R11R12={} R13R21={} R22R23={} R31R32={} R33={} "
                 "TR=({}, {}, {})",
                 gEntryMatrixCensus.calls,
                 rot11,
                 rot13,
                 rot22,
                 rot31,
                 rot33,
                 trX,
                 trY,
                 trZ);
  }
}

enum class Step {
  kNextShape,
  kNextRecord,
  kDone,
};

bool isNegative(std::uint32_t value) {
  return (value & 0x80000000u) != 0u;
}

// `mult`'s low word, with the 64-bit product left in hi/lo the way the guest leaves it. MIPS `mult`
// is a SIGNED multiply, so the high word has to come from the signed product; the low words agree
// either way, which is how a body can get the value right and the register state wrong.
std::uint32_t multiplyLow(Core *c, std::uint32_t a, std::uint32_t b) {
  const std::int64_t product = static_cast<std::int64_t>(static_cast<std::int32_t>(a)) *
                               static_cast<std::int64_t>(static_cast<std::int32_t>(b));
  c->lo = static_cast<std::uint32_t>(product);
  c->hi = static_cast<std::uint32_t>(static_cast<std::uint64_t>(product) >> 32);
  return c->lo;
}

// The point shape at 0x8004E5D8: the shape word's high half is a y extent, added to the caller's
// radius and squared, and the whole separation has to be inside it. 8-byte shape records.
ShapeOutcome testPointShape(Core *c,
                            const Walker &w,
                            std::uint32_t shape,
                            std::uint32_t word,
                            const Separation &sep,
                            const Offset &off) {
  const std::uint32_t extent = (word >> 16) + w.radius;
  const std::uint32_t step = multiplyLow(c, extent, extent);
  ShapeOutcome outcome{};
  outcome.shape = shape + 4u; // the `bgez` delay slot, which runs on the miss arm as well
  outcome.step = step;
  outcome.target = sep.squared;
  outcome.normalX = off.x;
  outcome.normalY = off.y;
  outcome.normalZ = off.z;
  outcome.hit = isNegative(sep.squared - step);
  return outcome;
}

// The slab shape at 0x8004E668: the same y extent, plus a second shape word whose high half is a z
// extent measured against the record's own separation. 12-byte shape records.
ShapeOutcome testSlabShape(Core *c,
                           const Walker &w,
                           std::uint32_t shape,
                           std::uint32_t word,
                           const Separation &sep,
                           const Offset &off) {
  const std::uint32_t packed = c->mem_r32(shape);
  const std::uint32_t extent = (word >> 16) + w.radius;
  const std::uint32_t step = multiplyLow(c, extent, extent);
  const std::uint32_t zExtent =
      (static_cast<std::uint32_t>(static_cast<std::int32_t>(packed) >> 16)) + off.z;
  const std::uint32_t target = multiplyLow(c, zExtent, zExtent) + sep.mac1 + sep.mac2;
  ShapeOutcome outcome{};
  outcome.shape = shape + 8u; // the `bgez` delay slot
  outcome.step = step;
  outcome.target = target;
  outcome.normalX = off.x;
  outcome.normalY = off.y;
  outcome.normalZ = zExtent;
  outcome.hit = isNegative(target - step);
  return outcome;
}

// The box shape at 0x8004E718: the y slab first, then the record's own later words, chosen by the
// sign of the shifted y extent, decide which x and z extents apply. 20-byte shape records.
ShapeOutcome testBoxShape(Core *c,
                          const Walker &w,
                          std::uint32_t shape,
                          std::uint32_t word,
                          const Separation &sep,
                          const Offset &off) {
  const std::uint32_t packed = c->mem_r32(shape);
  shape += 0x10u; // the `blez` delay slot, which runs on the miss arm as well
  const std::uint32_t extent = (word >> 16) + w.radius;
  const std::uint32_t step = multiplyLow(c, extent, extent);
  ShapeOutcome outcome{};
  outcome.shape = shape;
  outcome.normalX = off.x;
  outcome.normalY = off.y;
  if (static_cast<std::int32_t>(step - sep.mac1 - sep.mac2) <= 0) {
    return outcome; // `blez`: the separation is already within the y slab
  }
  const std::uint32_t yExtent =
      (static_cast<std::uint32_t>(static_cast<std::int32_t>(packed) >> 16)) + off.z;
  const std::uint32_t xExtent =
      static_cast<std::uint32_t>(static_cast<std::int32_t>(packed << 16) >> 16) + off.z;
  // Inside the z band the y extent square is the step and the normal is zero. Outside it the
  // `bltz`'s delay slot picks which of the record's later words is the y one: 4 selects the word
  // before the x extent, and a negative extent keeps its own value.
  if (!isNegative(yExtent | (0u - xExtent))) {
    outcome.hit = true;
    outcome.step = step;
    outcome.target = sep.mac1 + sep.mac2;
    outcome.normalZ = 0u;
    return outcome;
  }
  const std::uint32_t slot = isNegative(yExtent) ? yExtent : 4u;
  const std::uint32_t y = static_cast<std::uint32_t>(c->mem_r16s(slot + shape - 0x10u)) + off.z;
  const std::uint32_t target = multiplyLow(c, y, y) + sep.mac1 + sep.mac2;
  const std::uint32_t xHalf = c->mem_r32(shape - 8u) + w.radius;
  const std::uint32_t xStep = multiplyLow(c, xHalf, xHalf);
  outcome.normalZ = y;
  outcome.target = target;
  outcome.step = xStep;
  outcome.hit = isNegative(target - xStep);
  return outcome;
}

// The moby shape at 0x8004E830: the record's own transform goes into the GTE, one of the model's
// vertices is projected with RTPS, and the separation in that projected frame is squared against
// the y extent. 12-byte shape records.
ShapeOutcome testMobyShape(Core *c,
                           const Walker &w,
                           std::uint32_t record,
                           std::uint32_t shape,
                           std::uint32_t word,
                           const Offset &off) {
  gte_write_ctrl(kGteRot11Rot12, c->mem_r32(record + 0x20u));
  gte_write_ctrl(kGteRot13Rot21, c->mem_r32(record + 0x24u));
  gte_write_ctrl(kGteRot22Rot23, c->mem_r32(record + 0x28u));
  gte_write_ctrl(kGteRot31Rot32, c->mem_r32(record + 0x2Cu));
  gte_write_ctrl(kGteRot33, c->mem_r32(record + 0x30u));
  gte_write_ctrl(kGteTransX, 0u);
  gte_write_ctrl(kGteTransY, 0u);
  gte_write_ctrl(kGteTransZ, 0u);
  const std::uint32_t model = c->mem_r32(kModelSoundTables + (c->mem_r16(record + 0x36u) * 4u));
  const std::uint32_t shapes = model + (c->mem_r8(record + 0x3Cu) * 4u);
  const std::uint32_t node = c->mem_r32(shapes + 0x38u);
  const std::uint32_t bounds = node + (c->mem_r8(record + 0x3Eu) * 8u);
  const std::uint32_t shift = c->mem_r8(node + 0x5u) & 31u;
  // The vertex index is bit 31 of the word: a 21-bit unsigned value shifted up one bit, or the
  // 16-bit index from the shape record added to it.
  const std::uint32_t index = c->mem_r16(shape) * 4u;
  const std::uint32_t packed =
      c->mem_r32(((((c->mem_r32(bounds + 0x24u) << 11) >> 11) | 0x80000000u) + index));
  const std::uint32_t y = (static_cast<std::uint32_t>(static_cast<std::int32_t>(packed) >> 21))
                          << shift;
  const std::uint32_t z = static_cast<std::uint32_t>(static_cast<std::int32_t>(packed << 10) >> 21)
                          << shift;
  const std::uint32_t x = static_cast<std::uint32_t>(static_cast<std::int32_t>(packed << 20) >> 19)
                          << shift;
  gte_write_data(kGteVz0, y);
  gte_write_data(kGteVxy0, (x << 16) + z);
  gte_op(c, kGteRtps);
  // RTPS leaves IR1..IR3 alone, so these three reads are still the separation the record started
  // with: the guest's mtc2 pair wrote the record's own position here, and this arithmetic is what
  // its three `mfc2`s recover.
  const std::uint32_t normalX = gte_read_data(kGteIr3) + off.x;
  const std::uint32_t normalY = off.y - gte_read_data(kGteIr1);
  const std::uint32_t normalZ = off.z - gte_read_data(kGteIr2);
  gte_write_data(kGteIr1, normalX);
  gte_write_data(kGteIr2, normalY);
  gte_write_data(kGteIr3, normalZ);
  gte_op(c, kGteSqr);
  const std::uint32_t extent = (word >> 16) + w.radius;
  const std::uint32_t step = multiplyLow(c, extent, extent);
  const std::uint32_t squared =
      gte_read_data(kGteMac1) + gte_read_data(kGteMac2) + gte_read_data(kGteMac3);
  ShapeOutcome outcome{};
  outcome.shape = shape + 8u; // the `bgez` delay slot
  outcome.step = step;
  outcome.target = squared;
  outcome.normalX = normalX;
  outcome.normalY = normalY;
  outcome.normalZ = normalZ;
  outcome.hit = isNegative(squared - step);
  return outcome;
}

// The resolve loop at 0x8004E9D8: step out of the shape along the GTE direction of the current
// separation — the step itself normalised through the LZ pair and the magnitude table — until a
// step no longer fits inside the separation, then write the accumulated position back through $gp.
void resolveOutOfShape(Walker &w, std::uint32_t record, const ShapeOutcome &outcome) {
  Core *const c = w.c;
  const std::uint32_t step = outcome.step;
  std::uint32_t target = outcome.target;
  std::uint32_t normalY = outcome.normalY;
  std::uint32_t normalZ = outcome.normalZ;
  for (;;) {
    w.resolved = record; // s1, rewritten on every pass
    gte_write_data(kGteLzcs, step);
    // The `bgtz`'s delay slot runs either way, so the exponent is the same on both arms and the two
    // arms after it are the only writers of the fallback denominator and of $a1.
    const std::int32_t exponent = static_cast<std::int32_t>(gte_read_data(kGteLzcr)) - 2;
    if (static_cast<std::int32_t>(target) <= 0) {
      target = 1u;
      normalZ = 0xFFFFFFFFu;
    }
    const std::uint32_t scaled = step << (static_cast<std::uint32_t>(exponent) & 31u);
    const std::int32_t overshoot = exponent - 16;
    std::uint32_t denominator;
    if (overshoot >= 0) {
      denominator = target << (static_cast<std::uint32_t>(overshoot) & 31u);
    } else {
      denominator = target >> (static_cast<std::uint32_t>(-overshoot) & 31u);
      if (static_cast<std::int32_t>(denominator) <= 0) {
        denominator = 1u; // the `j`'s delay slot
      }
    }
    cpu_div(c, scaled, denominator);
    const std::uint32_t quotient = c->lo;
    gte_write_data(kGteLzcs, quotient);
    std::uint32_t reach = quotient;
    if (quotient != 0u) {
      // `addi $t0, $zero, -2 ; and $t0, $a2, $t0` keeps LZCR's sign bit, and `addi $a3, $zero, 0x1F
      // ; sub $a3, $a3, $t0 ; sra $a3, $a3, 1` is an ARITHMETIC halving: a negative LZCR -- which
      // is what a negative quotient's LZCS produces -- gives -1, and the table entry is then
      // shifted left by all but its low bit. A logical halving here would hand back a shift count
      // near 2^31 and write the far vertex somewhere the guest never puts it.
      const std::int32_t leading = static_cast<std::int32_t>(gte_read_data(kGteLzcr) & 0xFFFFFFFEu);
      const std::int32_t halfShift = (0x1F - leading) >> 1;
      const std::int32_t below = leading - 0x18;
      const std::uint32_t tableOffset =
          below < 0 ? static_cast<std::uint32_t>(static_cast<std::int32_t>(quotient) >>
                                                 (0x18 - static_cast<std::uint32_t>(leading)))
                    : quotient << (static_cast<std::uint32_t>(below) & 31u);
      const std::uint32_t entry =
          c->mem_r16s(spyro::guest::kMagnitudeTable + ((tableOffset - 0x40u) << 1));
      reach = (entry << (static_cast<std::uint32_t>(halfShift) & 31u)) >> 12;
    }
    target = reach + 1u;
    gte_write_data(kGteIr1, w.v1);
    gte_write_data(kGteIr2, normalY);
    gte_write_data(kGteIr3, normalZ);
    gte_write_data(kGteIr0, target);
    gte_op(c, kGteGpf);
    const std::uint32_t mac1 = gte_read_data(kGteMac1);
    const std::uint32_t mac2 = gte_read_data(kGteMac2);
    const std::uint32_t mac3 = gte_read_data(kGteMac3);
    const std::uint32_t dirX = static_cast<std::uint32_t>(static_cast<std::int32_t>(mac1) >> 8);
    const std::uint32_t dirY = static_cast<std::uint32_t>(static_cast<std::int32_t>(mac2) >> 8);
    const std::uint32_t dirZ = static_cast<std::uint32_t>(static_cast<std::int32_t>(mac3) >> 8);
    gte_write_data(kGteIr1, dirX);
    gte_write_data(kGteIr2, dirY);
    gte_write_data(kGteIr3, dirZ);
    gte_op(c, kGteSqr);
    const std::uint32_t separation =
        gte_read_data(kGteMac1) + gte_read_data(kGteMac2) + gte_read_data(kGteMac3);
    w.x = w.x + w.v1;
    w.y = w.y + normalY;
    w.z = w.z + normalZ;
    w.x = w.x - dirX;
    w.y = w.y - dirY;
    w.z = w.z - dirZ;
    if (!isNegative(separation - step)) {
      c->mem_w32(w.pos + 0u, w.x);
      c->mem_w32(w.pos + 4u, w.y);
      c->mem_w32(w.pos + 8u, w.z);
      return;
    }
    target = separation;
    w.v1 = dirX;
    normalY = dirY;
    normalZ = dirZ;
  }
}

// The hit tail every shape shares (0x8004E5F8, 0x8004E6A4, 0x8004E7BC, 0x8004E95C): OR the caller's
// flag word into the record unless the shape word's bit 2 forbids it, OR the shape word into the
// damage word when the caller passed one, then take the next shape (bit 1 of the shape word, or bit
// 1 of the argument mask), resolve the first hit only (bit 0 of the argument mask), or finish.
Step shapeTail(Walker &w,
               std::uint32_t record,
               std::uint32_t shape,
               std::uint32_t word,
               const ShapeOutcome &outcome) {
  Core *const c = w.c;
  if ((word & 4u) == 0u) {
    c->mem_w32(record + 0x18u, c->mem_r32(record + 0x18u) | w.damageIn);
  }
  if (w.damageOut != 0u) {
    c->mem_w32(w.damageOut, c->mem_r32(w.damageOut) | c->mem_r32(shape - 4u));
  }
  if ((word & 2u) != 0u) {
    return Step::kNextShape;
  }
  if ((w.flags & 1u) != 0u) {
    w.v1 = outcome.normalX; // the `j`'s delay slot, on the resolve arm only
    resolveOutOfShape(w, record, outcome);
    return Step::kDone;
  }
  if ((w.flags & 2u) != 0u) {
    return Step::kNextShape;
  }
  return Step::kDone;
}

// One record at 0x8004E52C: the bounding-sphere reject, then the shape loop, then whatever the hit
// tail asks for. $v1 holds the record's x separation from the `sub` onwards, which is what the
// epilogue hands back when the shape list runs out.
Step walkRecord(Walker &w, std::uint32_t record) {
  Core *const c = w.c;
  Offset off{};
  off.x = w.v1 - w.x;
  w.v1 = off.x;
  off.y = c->mem_r32(record + 0x10u) - w.y;
  off.z = c->mem_r32(record + 0x14u) - w.z;
  gte_write_data(kGteIr1, off.x);
  gte_write_data(kGteIr2, off.y);
  gte_write_data(kGteIr3, off.z);
  std::uint32_t shape = c->mem_r32(record + 0x8u);
  gte_op(c, kGteSqr);
  Separation sep{};
  sep.mac1 = gte_read_data(kGteMac1);
  sep.mac2 = gte_read_data(kGteMac2);
  sep.squared = sep.mac1 + sep.mac2 + gte_read_data(kGteMac3);
  if (shape == 0u) {
    return Step::kNextRecord; // `beqz`: the record carries no shape data at all
  }
  const std::uint32_t reach = static_cast<std::uint32_t>(c->mem_r16(shape)) + w.radius;
  shape += 4u; // the `beqz`'s delay slot
  if (!isNegative(sep.squared - multiplyLow(c, reach, reach))) {
    return Step::kNextRecord; // `bgez`: outside the bounding sphere
  }
  for (std::uint32_t word = 0u;;) {
    // The shape list ends on bit 0 of the word just read, and the `bnez`'s delay slot has already
    // loaded the next word by the time that test runs.
    if ((word & 1u) != 0u) {
      return Step::kNextRecord;
    }
    word = c->mem_r32(shape);
    shape += 4u; // the dispatch's delay slot
    ShapeOutcome outcome{};
    switch (word & 0xFF00u) {
    case kShapePointJump:
    case kShapePoint:
      outcome = testPointShape(c, w, shape, word, sep, off);
      break;
    case kShapeSlabJump:
    case kShapeSlab:
      outcome = testSlabShape(c, w, shape, word, sep, off);
      break;
    case kShapeBoxJump:
    case kShapeBox:
      outcome = testBoxShape(c, w, shape, word, sep, off);
      break;
    case kShapeMobyJump:
    case kShapeMoby:
      outcome = testMobyShape(c, w, record, shape, word, off);
      break;
    default:
      return Step::kNextRecord; // an unmapped type byte ends the shape list
    }
    shape = outcome.shape;
    if (!outcome.hit) {
      continue;
    }
    w.firstHit = record;
    const Step tail = shapeTail(w, record, shape, word, outcome);
    if (tail == Step::kNextShape) {
      continue;
    }
    return tail;
  }
}

// The candidate list at 0x8004E494: the cell the position falls in, plus the cell before it and the
// cell after it — or just the cell after, when the position sits on the cell's high edge — each an
// entry of g_MobyCollisionChain, zero-terminated. A neighbour that would wrap out of the table
// becomes -1 and the `bltz` tests drop it, which is also what the first cell's predecessor is.
//
// The neighbour is the cell BEFORE by default, not zero: the `beqz` that skips the "on an edge"
// arm has `addi a1, v1, -1` in its delay slot, and a delay slot runs whether or not the branch is
// taken. Reading that arm as leaving the `andi` result in place drops the predecessor cell, which
// the differential reported on call 1 as a list nine words (36 bytes) out.
void buildCandidateList(Walker &w) {
  Core *const c = w.c;
  const std::uint32_t base = c->mem_r32(kMobyCollisionChain);
  const std::uint32_t column = w.x >> 13;
  const std::uint32_t row = (w.y >> 13) << 5;
  std::uint32_t list = kCandidateList;
  std::uint32_t nextColumn = column - 1u; // the `beqz`'s delay slot
  if ((w.x & kCellEdge) != 0u) {
    nextColumn = column + 1u;
    if (nextColumn == kCellColumnLimit) {
      nextColumn = 0xFFFFFFFFu;
    }
  }
  std::uint32_t nextRow = row - kCellRowStride; // the `beqz`'s delay slot
  if ((w.y & kCellEdge) != 0u) {
    nextRow = row + kCellRowStride;
    if (nextRow == kCellRowLimit) {
      nextRow = 0xFFFFFFFFu;
    }
  }
  c->mem_w32(list, base + ((column + row) << 2));
  list += 4u;
  if (!isNegative(nextColumn)) {
    c->mem_w32(list, base + ((nextColumn + row) << 2));
    list += 4u;
  }
  if (!isNegative(nextRow)) {
    c->mem_w32(list, base + ((column + nextRow) << 2));
    list += 4u;
  }
  if (!isNegative(nextColumn) && !isNegative(nextRow)) {
    c->mem_w32(list, base + ((nextColumn + nextRow) << 2));
    list += 4u;
  }
  c->mem_w32(list, 0u);
  w.v1 = column; // $v1, which an empty candidate list leaves as the return value's companion
}

// ── 0x8004E3C8 — the moby collision walk. $a0 is the position to resolve, $a1 the caller's radius,
//     $a2 the word hit flags are ORed into (0 when the caller passed none), $a3 the flag word ORed
//     into every record that hits, and the 5th/6th arguments at entry sp+0x10 and sp+0x14 are the
//     moby to skip and a bitmask whose bit 0 resolves the FIRST hitting shape only and whose bit 1
//     keeps scanning past a hit.
//
// It builds a candidate list in scratch expansion RAM from the position's 13-bit cell coordinates,
// walks every record in those cells, rejects each on its bounding sphere, then tests the record's
// shape records — dispatched through a computed jump on bits 8..15 of the shape word — and resolves
// the position out of whatever shape hit.
//
// v0 is that choice on EVERY exit: the epilogue's `beqz` delay slot writes $s2 and the instruction
// after it overwrites it with $s1 only when bit 0 of the mask is clear. v1 ends up holding the cell
// column when no record was ever reached, the last record's x separation when the shape list ran
// out, and whatever the resolve loop last computed when it ran.
//
// The prologue's spill to the shared save area at 0x80077DD8 IS reproduced, and it is not a no-op:
// the epilogue reloads the very same twelve words, so the area holds THIS call's registers once the
// call returns rather than the previous call's. Leaving that to the caller's leftovers was the
// first thing this override got wrong, and the differential caught it on call 1 of 231.
void mobyCollisionWalk(Core *c) {
  const std::uint32_t spilled[12] = {c->r[16],
                                     c->r[17],
                                     c->r[18],
                                     c->r[19],
                                     c->r[20],
                                     c->r[21],
                                     c->r[22],
                                     c->r[23],
                                     c->r[28],
                                     c->r[29],
                                     c->r[30],
                                     c->r[31]};
  for (std::uint32_t word = 0u; word < 12u; word++) {
    c->mem_w32(kRegisterSpillArea + (word * 4u), spilled[word]);
  }
  recordEntryMatrix();
  Walker w{};
  w.c = c;
  w.pos = c->r[4];
  w.radius = c->r[5];
  w.damageOut = c->r[6];
  w.damageIn = c->r[7];
  w.ignore = c->mem_r32(c->r[29] + 0x10u);
  w.flags = c->mem_r32(c->r[29] + 0x14u);
  w.x = c->mem_r32(w.pos + 0u);
  w.y = c->mem_r32(w.pos + 4u);
  w.z = c->mem_r32(w.pos + 8u);
  if (isNegative(w.x | w.y | w.z)) {
    // The sign test's delay slot clears v0 on both arms, and nothing has written v1 yet.
    c->r[2] = 0u;
    return;
  }
  buildCandidateList(w);
  bool done = false;
  std::uint32_t cell = kCandidateList;
  while (!done) {
    std::uint32_t record = c->mem_r32(cell);
    cell += 4u;
    if (record == 0u) {
      break; // the cell list is exhausted
    }
    record -= 4u; // the `beqz`'s delay slot
    for (;;) {
      record = c->mem_r32(record + 4u);
      // The chain's `beqz` delay slot reads this even when the chain has just ended, so an
      // exhausted chain leaves v1 holding the scratchpad word at 0xC.
      w.v1 = c->mem_r32(record + 0xCu);
      if (record == 0u) {
        break;
      }
      if (record == w.ignore) {
        continue; // the caller's moby, skipped after its y separation was read anyway
      }
      const Step step = walkRecord(w, record);
      if (step == Step::kDone) {
        done = true;
        break;
      }
    }
  }
  // 0x8004EB5C `beqz $at, 0x8004EB68` with `addi $v0,$s2,0` in its DELAY SLOT, so the $s2 value is
  // installed when the bit is CLEAR; 0x8004EB64 `addi $v0,$s1,0` runs only when it is set. The
  // select is the other way round from what it looks like, and inverting it swaps the returned
  // record on every call where the two differ.
  c->r[2] = (w.flags & 1u) == 0u ? w.firstHit : w.resolved;
  c->r[3] = w.v1;
}

} // namespace

void registerMobyCollisionOverrides(Core &core) {
  spyro::installNativeOverride(core, 0x8004E3C8u, "moby_collision_walk", mobyCollisionWalk);
}

} // namespace spyro1::native
