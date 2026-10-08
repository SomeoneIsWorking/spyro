#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

// The shaded Moby renderer's normal-lighting program, `func_80022A2C` (external/spyro-1
// asm/renderers/r_moby.s). Two arms run the same GTE sequence on a signed normal and differ in
// where the normal comes from and what follows:
//
//   per-vertex pass  0x800230EC-0x80023258   one normal per VERTEX, for a model whose vertex list
//   does
//                                            not sit directly behind its 16-byte header; the colour
//                                            words it produces are what Gouraud primitives index
//   variant-1 face   0x80023574-0x800236D4   one normal per PRIMITIVE, the word after its indices
//
// Both rotate the normal by the record's rotation (MVMVA 1,0,3,3,0), scale it by the lighting
// entry's own factor (GPF 0), then run CC against the light colour matrix with the entry as the
// background colour. They are one native owner because a second transcription of those three words
// would drift silently and both results would still be a plausible colour.
//
// It is native because a render producer must not execute guest lighting or read the guest's
// scratch array (the native renderer replaces 0x80022A2C and never populates it).
namespace spyro::shaded_light {

using Matrix3 = std::array<std::array<std::int16_t, 3>, 3>;

// What the program reads besides the normal. `rotation` is the GTE rotation matrix at that point in
// retail, the Moby's own (camera or screen-space) composed rotation; `colourMatrix` is the light
// colour matrix retail loads from `D_800770C8` at 0x80022AC8-0x80022B04 (rows are identical there);
// `entry` is the lighting word, `D_8006E3D8 + (Moby[0x4C] >> 22)` (0x8002312C, 0x80023548).
struct Input {
  Matrix3 rotation{};
  Matrix3 colourMatrix{};
  std::uint32_t entry = 0;
};

// The GTE's IR1, IR2, IR3 as the arms load them. The two arms order the same three signed bytes
// differently, which is why each has its own constructor and nothing else builds this.
using NormalIr = std::array<std::int32_t, 3>;

// 0x80023130-0x80023150: three consecutive `lb`, IR3 = byte 0, IR1 = byte 1, IR2 = byte 2.
NormalIr vertexNormal(std::array<std::int8_t, 3> memoryOrder);

// 0x80023578-0x80023594: the primitive's normal word, IR3 = bits 24..31, IR1 = bits 16..23,
// IR2 = bits 8..15, each sign-extended.
NormalIr faceNormal(std::uint32_t word);

// The outputs of MVMVA, GPF and CC for one normal, before the selection that follows.
struct GteStage {
  // RGB2 after CC: the colour word, red in the low byte, code byte zero.
  std::uint32_t rgb = 0;
  // IR1..IR3 after CC's second phase (the lit brightness times 255/256), which the highlight step
  // reads.
  std::array<std::int32_t, 3> ir{};
};

// MVMVA 1,0,3,3,0; GPF 0; CC, exactly as the encoded words 0x4A49E012, 0x4B90003D and 0x4B38041C
// run them.
GteStage gteStage(const Input &input, const NormalIr &normal);

// 0x80023654-0x800236D0 and 0x800231D8-0x80023250: when the entry's top nibble is nonzero and the
// lit brightness exceeds that nibble's threshold (`nibble << 7` plus the entry's red background),
// the colour is rebuilt from the brightness plus twice the excess; otherwise the CC colour stands.
std::uint32_t highlight(std::uint32_t entry, const GteStage &stage);

// The per-vertex pass's own selection (0x800231C4-0x80023254): a colour below the entry's 24-bit
// value (a PACKED integer comparison, not per channel) is replaced by it; anything else takes the
// highlight. The variant-1 face arm has no such floor and calls `highlight` directly.
std::uint32_t vertexColour(std::uint32_t entry, const GteStage &stage);

// One colour word per normal, in vertex order.
std::vector<std::uint32_t> vertexColours(const Input &input,
                                         std::span<const std::array<std::int8_t, 3>> normals);

// The colour of one variant-1 primitive from its normal word.
std::uint32_t faceColour(const Input &input, std::uint32_t normalWord);

} // namespace spyro::shaded_light
