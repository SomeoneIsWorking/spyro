#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace spyro::paired_actor {

// Exact integer projection product consumed by the pure face/material/OT slice.
// `offset` fields in Primitive select these records in the producer-owned table.
struct ProjectedVertex {
  int16_t x = 0;
  int16_t y = 0;
  uint16_t depth = 0;
};

// Pure, projection-free decode of 0x80023AC4's normal primitive stream. Offsets remain byte offsets
// into the function's projected-vertex and material tables; the eventual producer owns mapping them.
struct Primitive {
  uint32_t source_ordinal = 0;
  bool quad = false;
  bool semi_transparent = false;
  int8_t ot_adjust = 0;                 // signed high nibble of word 1
  uint16_t projected_offset[4]{};
  uint16_t material_offset[4]{};
  uint32_t packet_attr[4]{};            // compact UV/CLUT/TPAGE words, still uninterpreted
};

struct DecodeResult {
  std::vector<Primitive> primitives;
  std::string error;
  explicit operator bool() const { return error.empty(); }
};

// words[0] is the byte span beginning after that header; records begin at words[1]. Triangles occupy five
// words and sign-bit quads occupy six. The decoder refuses truncation/trailing bytes rather than
// silently returning a plausible prefix.
DecodeResult decode_normal_stream(std::span<const uint32_t> words);

struct MaterialTables {
  std::span<const uint32_t> base;
  std::span<const uint32_t> override_colors;
  uint32_t override_control = 0;        // 0x80078A80; byte 3 selects the transformed table
};

struct ResolvedMaterial {
  uint32_t rgb[4]{};                    // low 24 bits, in primitive vertex order
  uint8_t command = 0;                  // GT3/GT4, with semi-transparency bit
};

bool resolve_material(const Primitive& primitive, const MaterialTables& tables,
                      ResolvedMaterial& out, std::string& error);

struct OrderedPrimitive {
  Primitive primitive;
  uint32_t ot_bin = 0;
};

// Exact 0x80025348/0x800255F0 normal-path bin expression. Returns false for the guest's raw<=0 gate.
bool compute_ot_bin(const Primitive& primitive, const uint32_t vertex_depth[4],
                    uint32_t depth_origin, uint8_t shift, uint32_t& raw, uint32_t& bin);

// The guest accumulates FIFO chains per bin, then drains bins high-to-low. Stable sort expresses the
// same contract without exposing packet pointers: descending bins, source order preserved in a bin.
std::vector<OrderedPrimitive> stable_descending_bins(std::span<const OrderedPrimitive> input);

}  // namespace spyro::paired_actor
