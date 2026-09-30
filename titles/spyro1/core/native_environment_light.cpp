#include "native_environment_light.h"

#include "native_execution.h"

#include <cstdint>

namespace spyro1::native {
namespace {

constexpr std::uint32_t kNodeLists = 0x80078560u;
constexpr std::uint32_t kMobyTable = 0x800785A8u;
constexpr std::uint32_t kNarrowRecords = 0x800785C0u;
constexpr std::uint32_t kWideRecords = 0x800785C4u;
// g_Environment + 0x2C, the TerrainCollision pointer, and the vertex block its +0x10 holds. BOTH
// arms of the blend reach it through the same two loads: `0x8002AE40 lw $a2,0x2C($a2)` is the
// delay slot of the arm test, so it runs whichever arm is taken, and `0x8002AE58` (software) and
// `0x8002AFF8` (GTE) are the same `lw $a2,0x10($a2)`. g_Environment + 0x10, the surface table, is
// a different pointer and must not be read here.
constexpr std::uint32_t kTerrainCollision = 0x800785D4u;
constexpr std::uint32_t kVertexBlock = 0x10u;
constexpr std::uint32_t kGteScratch = 0x1F800000u;
constexpr std::uint32_t kWideRecordSize = 0xA8u;
constexpr std::uint32_t kVertexStride = 0x0Cu;
constexpr std::uint32_t kGpf = 0x4B90003Du;
constexpr std::uint32_t kGpl = 0x4BA0003Eu;
constexpr std::uint32_t kIr0 = 8u;
constexpr std::uint32_t kIr1 = 9u;
constexpr std::uint32_t kIr2 = 10u;
constexpr std::uint32_t kIr3 = 11u;
constexpr std::uint32_t kMac1 = 25u;
constexpr std::uint32_t kMac2 = 26u;
constexpr std::uint32_t kMac3 = 27u;

// v0, v1, hi and lo cross the seven lists, because an empty list leaves them alone and the body
// returns whatever the last list that did work left behind. A list that runs with no due node at
// all therefore hands the NEXT list the values it inherited, which is why they are carried here.
struct Tail {
  std::uint32_t v0;
  std::uint32_t v1;
  std::uint32_t hi;
  std::uint32_t lo;
};

void multiply(std::uint32_t left, std::uint32_t right, Tail &tail) {
  const std::int64_t product = static_cast<std::int64_t>(static_cast<std::int32_t>(left)) *
                               static_cast<std::int64_t>(static_cast<std::int32_t>(right));
  tail.lo = static_cast<std::uint32_t>(product);
  tail.hi = static_cast<std::uint32_t>(static_cast<std::uint64_t>(product) >> 32);
}

// A packed vertex word carries three fields: bits 0..13 stay put, bits 14..23 become the middle
// component and bits 24..31 the top one. The middle and top components are extracted with
// `sll`+`sra` (SIGNED, unlike the `srl` the body uses elsewhere) and then both have the low field
// added, which is the rebiasing the GTE's own unpacking would do.
struct Word {
  std::int32_t low;
  std::int32_t middle;
  std::int32_t top;
};

Word splitWord(std::uint32_t packed) {
  const std::int32_t low = static_cast<std::int32_t>(packed & 0x3FFFu);
  const std::int32_t middle = (static_cast<std::int32_t>(packed << 9) >> 23) + low;
  const std::int32_t top = (static_cast<std::int32_t>(packed) >> 23) + low;
  return Word{low, middle, top};
}

// The reassembly every biased store performs: the reference component keeps its low 14 bits, one
// other component is differenced against it and shifted into the wide field, and the third is
// differenced, masked to the field width THAT SITE uses and shifted into the narrow field. The z
// slot of each of the GTE arm's three branches masks to eight bits and every other site masks to
// nine, so the widths are arguments rather than an assumption: getting one wrong writes a
// correct-looking colour with two field widths swapped, which no other check in the port would
// notice.
std::uint32_t biased(std::int32_t reference,
                     std::int32_t first,
                     std::int32_t second,
                     std::uint32_t mask,
                     unsigned firstShift,
                     unsigned secondShift) {
  const std::uint32_t base = static_cast<std::uint32_t>(reference) & 0x3FFFu;
  const std::uint32_t wide = static_cast<std::uint32_t>(first - reference) << firstShift;
  const std::uint32_t narrow = (static_cast<std::uint32_t>(second - reference) & mask)
                               << secondShift;
  return base | wide | narrow;
}

std::uint32_t signedShift(std::uint32_t value, unsigned amount) {
  return static_cast<std::uint32_t>(static_cast<std::int32_t>(value) >> amount);
}

// The tick test every list opens a node with, 0x8002A71C..0x8002A748. Bit 1 of the node's flag byte
// skips it entirely; otherwise the tick byte is charged against the frame delta in $a0, a node with
// ticks left is decremented and skipped, and a node whose tick is already spent walks its chain.
//
// THE STARTING DELTA IS THE WHOLE CHARGE, NOT WHAT IS LEFT OF IT. `0x8002A734 addi $t4,$t8,0` is
// the `bgtz $v1,0x8002A718` DELAY SLOT, so $t4 is reloaded with the caller's $a0 on BOTH paths of
// that branch — including the one that leaves the node alone — and the walk always starts from
// `$a0` itself. The `neg $t4,$v0` at 0x8002A740 is the dead half of that: the next pass overwrites
// it before anything reads it. A body that walks with `-left` starts every chain from a different
// number, and the chain's own `sub $v0,$v0,$t4` turns that into a different exit byte.
//
// v0 and v1 are set on BOTH exits: the body loads the flag byte AND the tick byte before it tests
// the flag, so a node skipped by bit 1 still leaves v0 holding its untouched tick rather than
// whatever the previous node left there — which is the whole of v0 when the last list ends on such
// a node.
bool nodeIsDue(Core *c,
               std::uint32_t node,
               std::uint32_t charge,
               std::uint32_t &flags,
               std::uint32_t &delta,
               Tail &tail) {
  flags = c->mem_r8(node + 1u);
  const std::uint32_t tick = c->mem_r8(node + 3u);
  tail.v0 = tick;
  tail.v1 = flags & 0x2u;
  if (tail.v1 != 0u) {
    return false;
  }
  const std::int32_t left = static_cast<std::int32_t>(tick) - static_cast<std::int32_t>(charge);
  tail.v0 = static_cast<std::uint32_t>(left);
  // 0x8002A86C `addiu $t4,$t8,0` is the delay slot of the flag test and loads the CHARGE, but
  // 0x8002A878 `neg $t4,$v0` is the delay slot of 0x8002A874 `blez $v0` and OVERWRITES it on the
  // way in: a delay slot runs on taken branches too. The walk therefore starts from the leftover
  // (charge - tick), not from the charge. Walking with the full charge shifts every chain's
  // `sub $v0,$v0,$t4` and lands the blended colours one byte off.
  const std::int32_t leftover = 0 - left;
  if (left > 0) {
    c->mem_w8(node + 3u, static_cast<std::uint8_t>(left));
    return false;
  }
  delta = static_cast<std::uint32_t>(leftover);
  return true;
}

// The 4-byte-record chain, walked by the first two lists. Each record is four bytes — step/flags,
// next index, spare, signed delta — and the walk writes val&3 to node+1 on every step, stops on bit
// 1 of val or on a positive step, and otherwise carries the negated step as the next delta while
// advancing the read address by val&1 to find the next index. The caller writes node+2 and node+3
// from `next` and `exitValue`, which is also what v0 and v1 hold on return.
//
// THE CHAIN BASE IS node+8 FOR THIS WALK AND node+0xC FOR THE 8-BYTE ONE, and the two are
// different instructions in the retail body: `0x8002A750 addi $t3,$t5,0x8` against
// `0x8002AA68 addi $t2,$t4,0xC`. The first list's copy of the walk (0x8002A74C..0x8002A7B4) and
// the second's (0x8002A8A0..0x8002A8E4) share that base and differ only in the second's running
// sum of the signed deltas, which only the second list's caller reads — so one walk serves both and
// list 0 computes a sum it does not use.
//
// THE TWO EXITS LEAVE DIFFERENT VALUES IN v0, and both stores come from that register. The bit-1
// exit arrives with v0 already shifted (`srl v0,v0,2` is the `bgtz` delay slot), so node+3 gets
// val>>2. The positive-step exit arrives with v0 holding the SUBTRACTION the test used, because
// that exit's delay slot is `neg t4,v0` and nothing shifts it back — so node+3 gets
// (val>>2) - delta, truncated to a byte, which is a different number on every walk that ends that
// way. Reading the two exits as the same value is invisible until a walk takes the second one.
struct ByteChain {
  std::uint32_t stop;
  std::uint32_t next;
  std::uint32_t exitValue;
  std::int32_t sum;
};

ByteChain walkByteChain(Core *c,
                        std::uint32_t node,
                        std::uint32_t index,
                        std::uint32_t flags,
                        std::uint32_t delta,
                        Tail &tail) {
  const std::uint32_t base = node + 0x8u;
  std::uint32_t entry = base + (index << 2) + (flags & 0x1u);
  std::uint32_t next = c->mem_r8(entry + 1u);
  std::int32_t sum = 0;
  for (;;) {
    const std::uint32_t value = c->mem_r8(entry);
    c->mem_w8(node + 1u, static_cast<std::uint8_t>(value & 0x3u));
    const std::uint32_t stepped = value >> 2;
    tail.v0 = stepped;
    tail.v1 = next;
    sum += static_cast<std::int32_t>(c->mem_r8s(entry + 3u));
    if ((value & 0x2u) != 0u) {
      return ByteChain{entry, next, stepped, sum};
    }
    const std::int32_t step = static_cast<std::int32_t>(stepped) - static_cast<std::int32_t>(delta);
    if (step > 0) {
      tail.v0 = static_cast<std::uint32_t>(step);
      return ByteChain{entry, next, tail.v0, sum};
    }
    delta = 0u - static_cast<std::uint32_t>(step);
    next = c->mem_r8(entry + (value & 0x1u) + 1u);
    entry = base + (next << 2);
  }
}

// THE ENTRY IS REBUILT FROM THE NEXT INDEX ON EVERY STEP, not carried forward. val&1 moves the
// address the next index is read from, but the loop head recomputes the entry as base + next*4, so
// the bit never survives into the next entry. Carrying it accumulates one byte per step and lands
// on a neighbouring entry, which for a chain whose neighbouring records share a value byte changes
// nothing except the index the body then stores — the kind of difference a value-only reading of
// the walk cannot see.

// The 8-byte-record chain, walked by the other five lists (the four moby taggers and the vertex
// colour one). The whole value byte reaches node+1 (there is no `& 3` here), node+3 takes the step
// byte whole, and the fields sit at +0/+1/+2 where the 4-byte chain's are +0/+1/+3. Unlike the
// 4-byte walk this one is NOT exercised by the Artisans route — no node of the five lists comes due
// there — so its base and its three field offsets are the listing's, unconfirmed by the
// differential, and a route that reaches a due node in one of those lists is what would settle
// them.
struct RecordChain {
  std::uint32_t stop;
  std::uint32_t next;
  std::uint32_t exitValue;
};

RecordChain walkRecordChain(Core *c,
                            std::uint32_t node,
                            std::uint32_t index,
                            std::uint32_t flags,
                            std::uint32_t delta,
                            Tail &tail) {
  const std::uint32_t base = node + 0xCu;
  std::uint32_t entry = base + (index << 3) + (flags & 0x1u);
  std::uint32_t next = c->mem_r8(entry + 2u);
  for (;;) {
    const std::uint32_t value = c->mem_r8(entry);
    const std::uint32_t raw = c->mem_r8(entry + 1u);
    c->mem_w8(node + 1u, static_cast<std::uint8_t>(value));
    tail.v0 = raw;
    tail.v1 = next;
    if ((value & 0x2u) != 0u) {
      return RecordChain{entry, next, raw};
    }
    const std::int32_t step = static_cast<std::int32_t>(raw) - static_cast<std::int32_t>(delta);
    if (step > 0) {
      tail.v0 = static_cast<std::uint32_t>(step);
      return RecordChain{entry, next, tail.v0};
    }
    delta = 0u - static_cast<std::uint32_t>(step);
    next = c->mem_r8(entry + (value & 0x1u) + 2u);
    entry = base + (next << 3);
  }
}

// The first list: 16 bytes of the narrow record table, then the whole 0xA8-byte record of the wide
// table, both from the index the walk stopped on to the node's own word at +4. The two table bases
// are the separate words at 0x800785C0 and 0x800785C4, and the body reads BOTH of them before it
// stores anything: a record that happens to cover them is copied from the bases it had already
// read, not from the bytes it has just written.
void copyNodeRecords(Core *c, std::uint32_t node, std::uint32_t charge, Tail &tail) {
  std::uint32_t flags = 0u;
  std::uint32_t delta = 0u;
  if (!nodeIsDue(c, node, charge, flags, delta, tail)) {
    return;
  }
  const ByteChain chain = walkByteChain(c, node, c->mem_r8(node + 2u), flags, delta, tail);
  c->mem_w8(node + 3u, static_cast<std::uint8_t>(chain.exitValue));
  c->mem_w8(node + 2u, static_cast<std::uint8_t>(chain.next));
  const std::uint32_t from = c->mem_r8(chain.stop + 3u);
  const std::uint32_t to = c->mem_r32(node + 4u);
  const std::uint32_t narrow = c->mem_r32(kNarrowRecords);
  const std::uint32_t wide = c->mem_r32(kWideRecords);
  const std::uint32_t narrowSource = narrow + (from << 4);
  const std::uint32_t narrowTarget = narrow + (to << 4);
  for (std::uint32_t offset = 0u; offset < 16u; offset += 4u) {
    c->mem_w32(narrowTarget + offset, c->mem_r32(narrowSource + offset));
  }
  multiply(kWideRecordSize, from, tail);
  const std::uint32_t wideSource = wide + tail.lo;
  multiply(kWideRecordSize, to, tail);
  const std::uint32_t wideTarget = wide + tail.lo;
  for (std::uint32_t offset = 0u; offset < kWideRecordSize; offset += 4u) {
    c->mem_w32(wideTarget + offset, c->mem_r32(wideSource + offset));
  }
  tail.v0 = wideTarget + kWideRecordSize;
  tail.v1 = wideSource + kWideRecordSize;
}

struct ByteRun {
  std::uint32_t offset;
  std::uint32_t count;
  std::uint32_t bias;
  bool masked;
};

// The wide record's angle fields come in five runs of four, and only the SECOND run is unmasked:
// the body shifts by one, adds 0x20 and masks for the first run, then re-reads the same shift and
// adds 0x10, 0x20 and 0x30 for the rest. Re-deriving the bias instead of reading it off the body
// would make the 0x29 run the masked one, which is a different byte in every record.
constexpr ByteRun kAngleRuns[] = {
    {0x19u, 4u, 0x20u, true},
    {0x29u, 8u, 0x00u, false},
    {0x49u, 8u, 0x10u, true},
    {0x69u, 8u, 0x20u, true},
    {0x89u, 8u, 0x30u, true},
};
constexpr std::uint32_t kAngleHalves[] = {0x09u, 0x0Du, 0x11u, 0x15u};

// The second list: the walk's summed signed deltas are added to the node's own halfword, masked to
// seven bits, stored back, and broadcast as a byte into both records — four fields of the narrow
// record and forty-two of the wide one.
void writeNodeAngle(Core *c, std::uint32_t node, std::uint32_t charge, Tail &tail) {
  std::uint32_t flags = 0u;
  std::uint32_t delta = 0u;
  if (!nodeIsDue(c, node, charge, flags, delta, tail)) {
    return;
  }
  const ByteChain chain = walkByteChain(c, node, c->mem_r8(node + 2u), flags, delta, tail);
  c->mem_w8(node + 3u, static_cast<std::uint8_t>(chain.exitValue));
  c->mem_w8(node + 2u, static_cast<std::uint8_t>(chain.next));
  const std::uint32_t index = c->mem_r16(node + 4u);
  multiply(kWideRecordSize, index, tail);
  const std::uint32_t narrow = c->mem_r32(kNarrowRecords) + (index << 4);
  const std::uint32_t wide = c->mem_r32(kWideRecords) + tail.lo;
  tail.v0 = wide;
  const std::uint32_t angle =
      (c->mem_r16(node + 6u) + static_cast<std::uint32_t>(chain.sum)) & 0x7Fu;
  tail.v1 = angle;
  c->mem_w16(node + 6u, static_cast<std::uint16_t>(angle));
  const std::uint32_t quarter = angle >> 2;
  for (std::uint32_t offset = 0x01u; offset <= 0x0Du; offset += 4u) {
    c->mem_w8(narrow + offset, static_cast<std::uint8_t>(quarter));
  }
  c->mem_w8(wide + 0x01u, static_cast<std::uint8_t>(quarter));
  c->mem_w8(wide + 0x05u, static_cast<std::uint8_t>(quarter));
  const std::uint32_t half = angle >> 1;
  for (const std::uint32_t offset : kAngleHalves) {
    c->mem_w8(wide + offset, static_cast<std::uint8_t>(half));
  }
  for (const ByteRun &run : kAngleRuns) {
    std::uint32_t value = half + run.bias;
    if (run.masked) {
      value &= 0x3Fu;
    }
    for (std::uint32_t i = 0u; i < run.count; ++i) {
      c->mem_w8(wide + run.offset + (i * 4u), static_cast<std::uint8_t>(value));
    }
  }
}

// The four middle lists: the same walk, then a moby out of the pointer table is stamped with this
// list's own index. Each of the four writes a different byte of the moby and nothing else, so the
// four bodies differ in one constant and are otherwise identical.
void tagNodeMoby(Core *c,
                 std::uint32_t node,
                 std::uint32_t charge,
                 std::uint32_t counter,
                 std::uint32_t field,
                 Tail &tail) {
  std::uint32_t flags = 0u;
  std::uint32_t delta = 0u;
  if (!nodeIsDue(c, node, charge, flags, delta, tail)) {
    return;
  }
  const RecordChain chain = walkRecordChain(c, node, c->mem_r8(node + 2u), flags, delta, tail);
  c->mem_w8(node + 3u, static_cast<std::uint8_t>(chain.exitValue));
  c->mem_w8(node + 2u, static_cast<std::uint8_t>(chain.next));
  const std::uint32_t index = c->mem_r16(node + 4u);
  tail.v0 = c->mem_r32(kMobyTable) + (index << 2);
  const std::uint32_t moby = c->mem_r32(tail.v0);
  c->mem_w8(moby + field, static_cast<std::uint8_t>(counter));
}

struct Triple {
  std::uint32_t x;
  std::uint32_t y;
  std::uint32_t z;
};

// One 4-byte component of the GTE arm: GPF accumulates the near vertex scaled by (0x1000 - weight)
// and GPL adds the far vertex scaled by the weight that is left, both through the hardware, so the
// saturation and flag behaviour is the hardware's rather than this port's arithmetic.
Triple blendComponent(Core *c,
                      std::uint32_t near,
                      std::uint32_t far,
                      std::uint32_t offset,
                      std::uint32_t keep,
                      std::uint32_t add) {
  const Word a = splitWord(c->mem_r32(near + offset));
  gte_write_data(kIr0, keep);
  gte_write_data(kIr1, static_cast<std::uint32_t>(a.low));
  gte_write_data(kIr2, static_cast<std::uint32_t>(a.middle));
  gte_write_data(kIr3, static_cast<std::uint32_t>(a.top));
  gte_op(c, kGpf);
  const Word b = splitWord(c->mem_r32(far + offset));
  gte_write_data(kIr0, add);
  gte_write_data(kIr1, static_cast<std::uint32_t>(b.low));
  gte_write_data(kIr2, static_cast<std::uint32_t>(b.middle));
  gte_write_data(kIr3, static_cast<std::uint32_t>(b.top));
  gte_op(c, kGpl);
  return Triple{gte_read_data(kMac1), gte_read_data(kMac2), gte_read_data(kMac3)};
}

// The GTE arm's depth cue. Only the z slot's reference changes with the arm: the two near slots are
// always differenced against their own x component read back from the scratch, while the z slot is
// differenced against the far vertex's x, y or z depending on which of the three arms the sign
// tests select. The near/far naming is the scratch's: scratch+0 is the first blend, scratch+0xC the
// second, and the third blend is the far vertex itself.
void writeBlendedVertex(Core *c,
                        std::uint32_t near,
                        std::uint32_t far,
                        std::uint32_t dest,
                        std::uint32_t keep,
                        std::uint32_t add) {
  const Triple first = blendComponent(c, near, far, 0u, keep, add);
  c->mem_w32(kGteScratch + 0x00u, signedShift(first.x, 12));
  c->mem_w32(kGteScratch + 0x04u, signedShift(first.y, 12));
  c->mem_w32(kGteScratch + 0x08u, signedShift(first.z, 12));
  const Triple second = blendComponent(c, near, far, 4u, keep, add);
  c->mem_w32(kGteScratch + 0x0Cu, signedShift(second.x, 12));
  c->mem_w32(kGteScratch + 0x10u, signedShift(second.y, 12));
  c->mem_w32(kGteScratch + 0x14u, signedShift(second.z, 12));
  const Triple third = blendComponent(c, near, far, 8u, keep, add);
  const std::int32_t farX = static_cast<std::int32_t>(third.x >> 12);
  const std::int32_t farY = static_cast<std::int32_t>(third.y >> 12);
  const std::int32_t farZ = static_cast<std::int32_t>(third.z >> 12);
  const std::int32_t secondX = static_cast<std::int32_t>(c->mem_r32(kGteScratch + 0x0Cu));
  const std::int32_t secondY = static_cast<std::int32_t>(c->mem_r32(kGteScratch + 0x10u));
  const std::int32_t secondZ = static_cast<std::int32_t>(c->mem_r32(kGteScratch + 0x14u));
  const std::int32_t firstX = static_cast<std::int32_t>(c->mem_r32(kGteScratch + 0x00u));
  const std::int32_t firstY = static_cast<std::int32_t>(c->mem_r32(kGteScratch + 0x04u));
  const std::int32_t firstZ = static_cast<std::int32_t>(c->mem_r32(kGteScratch + 0x08u));
  // EACH BRANCH ROTATES THE TRIPLE IT BIASES, cyclically, and the far vertex rotates with the
  // scratch. The z slot is the (reference, z, y) of the first branch, (y, x, z) of the second and
  // (z, y, x) of the third; the two scratch slots follow the same rotation, so the second branch's
  // near slots are referenced to their Y, not to their X. Reading all three branches as "the same
  // expression with a different reference" is what a rationalised version of this looks like, and
  // it is wrong in six of the nine stores.
  if (farY - farX >= 0 && farZ - farX >= 0) {
    c->mem_w32(dest + 8u, biased(farX, farZ, farY, 0xFFu, 24, 16));
    c->mem_w32(dest + 4u, biased(secondX, secondZ, secondY, 0x1FFu, 23, 14));
    c->mem_w32(dest + 0u, biased(firstX, firstZ, firstY, 0x1FFu, 23, 14));
  } else if (farZ - farY >= 0) {
    c->mem_w32(dest + 8u, biased(farY, farX, farZ, 0xFFu, 24, 16));
    c->mem_w32(dest + 4u, biased(secondY, secondX, secondZ, 0x1FFu, 23, 14));
    c->mem_w32(dest + 0u, biased(firstY, firstX, firstZ, 0x1FFu, 23, 14));
  } else {
    c->mem_w32(dest + 8u, biased(farZ, farY, farX, 0xFFu, 24, 16));
    c->mem_w32(dest + 4u, biased(secondZ, secondY, secondX, 0x1FFu, 23, 14));
    c->mem_w32(dest + 0u, biased(firstZ, firstY, firstX, 0x1FFu, 23, 14));
  }
}

// The software arm, which is what runs for every node whose colour weight byte is zero. A z whose
// middle and top components are both non-negative is stored repacked, with the other two words
// verbatim; otherwise all three words are differenced against one component of THEMSELVES, and
// which component rotates the same way the GTE arm's branches do — the middle in the first branch,
// the top in the second. There are only these two branches here: the arm reads no GTE scratch, so
// nothing about it can be checked against the GTE path's third.
void writeSoftwareVertex(Core *c, std::uint32_t source, std::uint32_t dest, Tail &tail) {
  const std::uint32_t xWord = c->mem_r32(source + 0u);
  const std::uint32_t yWord = c->mem_r32(source + 4u);
  const std::uint32_t zWord = c->mem_r32(source + 8u);
  const std::int32_t zMiddle = static_cast<std::int32_t>(zWord << 9) >> 23;
  const std::int32_t zTop = static_cast<std::int32_t>(zWord) >> 23;
  if (zMiddle >= 0 && zTop >= 0) {
    c->mem_w32(dest + 0u, xWord);
    c->mem_w32(dest + 4u, yWord);
    const std::uint32_t z = (zWord & 0x3FFFu) | (static_cast<std::uint32_t>(zMiddle) << 16) |
                            (static_cast<std::uint32_t>(zTop) << 24);
    c->mem_w32(dest + 8u, z);
    return;
  }
  const Word x = splitWord(xWord);
  const Word y = splitWord(yWord);
  const Word z = splitWord(zWord);
  // `0x8002AEB0 sub $v1, $t5, $t4` is the SECOND branch's own sign test, and $v1 is never rewritten
  // after it: this arm's loop tail (0x8002AFD0) advances only $v0 and the destination, so the
  // difference the branch tested in is what the function leaves in $v1 for the caller. The first
  // branch takes no such test and leaves whatever $v1 already held, which is the second index
  // shifted (0x8002AE48 `sll $v1, $at, 2`), so the value has to be written HERE and not derived
  // after the loop — a run of zero vertices is a legal outcome and still leaves it behind.
  tail.v1 = static_cast<std::uint32_t>(zTop - zMiddle);
  if (zTop - zMiddle >= 0) {
    c->mem_w32(dest + 0u, biased(x.middle, x.low, x.top, 0x1FFu, 23, 14));
    c->mem_w32(dest + 4u, biased(y.middle, y.low, y.top, 0x1FFu, 23, 14));
    c->mem_w32(dest + 8u, biased(z.middle, z.low, z.top, 0x1FFu, 24, 16));
    return;
  }
  c->mem_w32(dest + 0u, biased(x.top, x.middle, x.low, 0x1FFu, 23, 14));
  c->mem_w32(dest + 4u, biased(y.top, y.middle, y.low, 0x1FFu, 23, 14));
  c->mem_w32(dest + 8u, biased(z.top, z.middle, z.low, 0x1FFu, 24, 16));
}

// The last list, and the only one that writes vertex colours. The node's own word at +4 is a pair
// of record indices and the entry the walk stopped on carries a packed colour: its low byte is the
// weight, which is zero or not (and that one byte decides which of the two arms below runs), its
// next two bytes are the two record indices of the colour block, and the two index halves of the
// node's own word are the strides of the source and destination blocks. The two arms walk different
// distances and leave v1 differently — the software arm keeps the four-times term, the GTE arm ends
// on its own far cursor — and the body returns v0 and v1 from whichever arm ran last.
void blendNodeLight(Core *c, std::uint32_t node, std::uint32_t charge, Tail &tail) {
  std::uint32_t flags = 0u;
  std::uint32_t delta = 0u;
  if (!nodeIsDue(c, node, charge, flags, delta, tail)) {
    return;
  }
  const RecordChain chain = walkRecordChain(c, node, c->mem_r8(node + 2u), flags, delta, tail);
  c->mem_w8(node + 3u, static_cast<std::uint8_t>(chain.exitValue));
  c->mem_w8(node + 2u, static_cast<std::uint8_t>(chain.next));
  const std::uint32_t colour = c->mem_r32(chain.stop + 4u);
  const std::uint32_t weight = colour & 0xFFu;
  const std::uint32_t green = (colour >> 8) & 0xFFu;
  const std::uint32_t blue = (colour >> 16) & 0xFFu;
  const std::uint32_t base = c->mem_r32(node + 8u) + node;
  const std::uint32_t pair = c->mem_r32(node + 4u);
  const std::uint32_t first = pair & 0xFFFFu;
  const std::uint32_t second = pair >> 16;
  const std::uint32_t firstStride = (first << 3) + (first << 2);
  tail.v1 = first << 2;
  tail.v0 = firstStride;
  if (weight == 0u) {
    // TWO dependent loads, not one address: 0x8002AE40 `lw $a2,0x2c($a2)` reads the pointer at
    // 0x800785D4 and 0x8002AE58 `lw $a2,0x10($a2)` then reads the record block through it.
    // Collapsing them into `mem[0x800785E4]` reads a different word entirely.
    const std::uint32_t records = c->mem_r32(c->mem_r32(kTerrainCollision) + kVertexBlock);
    multiply(firstStride, green, tail);
    const std::uint32_t source = base + tail.lo;
    const std::uint32_t end = source + firstStride;
    std::uint32_t dest = records + (second << 3) + (second << 2);
    for (std::uint32_t at = source; at != end; at += kVertexStride) {
      writeSoftwareVertex(c, at, dest, tail);
      dest += kVertexStride;
    }
    tail.v0 = end;
    return;
  }
  // The same two dependent loads as the software arm above, the GTE half at 0x8002AFF8.
  const std::uint32_t records = c->mem_r32(c->mem_r32(kTerrainCollision) + kVertexBlock);
  multiply(firstStride, blue, tail);
  std::uint32_t far = base + tail.lo;
  multiply(firstStride, green, tail);
  const std::uint32_t near = base + tail.lo;
  const std::uint32_t end = near + firstStride;
  std::uint32_t dest = records + (second << 3) + (second << 2);
  const std::uint32_t keep = 0x1000u - (weight << 4);
  const std::uint32_t add = weight << 4;
  for (std::uint32_t at = near; at != end; at += kVertexStride) {
    writeBlendedVertex(c, at, far, dest, keep, add);
    far += kVertexStride;
    dest += kVertexStride;
  }
  tail.v0 = end;
  tail.v1 = far;
}

template <typename Body> void forEachNode(Core *c, std::uint32_t offset, Body body) {
  const std::uint32_t count = c->mem_r32(kNodeLists + offset);
  std::uint32_t cursor = c->mem_r32(kNodeLists + offset + 4u);
  const std::uint32_t end = cursor + (count << 2);
  for (std::uint32_t index = 0u; index < count; ++index) {
    const std::uint32_t node = c->mem_r32(cursor);
    cursor += 4u;
    body(node, index);
  }
}

// 0x8002A6FC — the environment light pass: seven lists of nodes are ticked against the frame delta
// in $a0, every node whose tick is spent walks a chain to pick a light record, and the seven bodies
// then copy a light record, broadcast a directional angle, stamp four moby fields and blend a
// vertex colour block. Nothing is returned, but v0, v1, hi and lo are all left live, so an empty
// list hands the next one its values and a run with no due node at all returns the caller's own —
// which is what the Artisans route's last list does, every call, with v0 ending on a skipped node's
// tick byte.
//
// The two vertex arms are byte-exact in the field widths they store (nine bits in most fields,
// eight in the GTE arm's three z slots) and in which component each biased store is referenced to,
// and the GTE arm reads its two scratch vertices with a signed shift and its far vertex with a
// logical one. The differential holds the first two lists and the tick/chain logic against the
// retail body on every call of the route; the five lists whose walk has eight-byte records, and
// with them both vertex arms, get no due node there and so are carried on the listing alone.
void propagateEnvironmentLight(Core *c) {
  const std::uint32_t charge = c->r[4];
  Tail tail{c->r[2], c->r[3], c->hi, c->lo};
  forEachNode(c, 0x00u, [&](std::uint32_t node, std::uint32_t) {
    copyNodeRecords(c, node, charge, tail);
  });
  forEachNode(c, 0x08u, [&](std::uint32_t node, std::uint32_t) {
    writeNodeAngle(c, node, charge, tail);
  });
  forEachNode(c, 0x10u, [&](std::uint32_t node, std::uint32_t index) {
    tagNodeMoby(c, node, charge, index, 0x18u, tail);
  });
  forEachNode(c, 0x18u, [&](std::uint32_t node, std::uint32_t index) {
    tagNodeMoby(c, node, charge, index, 0x19u, tail);
  });
  forEachNode(c, 0x20u, [&](std::uint32_t node, std::uint32_t index) {
    tagNodeMoby(c, node, charge, index, 0x1Au, tail);
  });
  forEachNode(c, 0x28u, [&](std::uint32_t node, std::uint32_t index) {
    tagNodeMoby(c, node, charge, index, 0x1Bu, tail);
  });
  forEachNode(c, 0x40u, [&](std::uint32_t node, std::uint32_t) {
    blendNodeLight(c, node, charge, tail);
  });
  c->r[2] = tail.v0;
  c->r[3] = tail.v1;
  c->hi = tail.hi;
  c->lo = tail.lo;
}

} // namespace

void registerEnvironmentLightOverrides(Core &core) {
  spyro::installNativeOverride(
      core, 0x8002A6FCu, "propagate_environment_light", propagateEnvironmentLight);
}

} // namespace spyro1::native
