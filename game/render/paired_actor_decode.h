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
  float view_z = 0;                   // unsaturated RTPS row-2 MAC / 4096; native D32 input
};


// Pure, projection-free decode of 0x80023AC4's normal primitive stream. Offsets remain byte offsets
// into the function's projected-vertex and material tables; the eventual producer owns mapping them.
struct Primitive {
  uint32_t source_ordinal = 0;
  bool quad = false;
  bool two_sided = false;               // word 0 bit 0; bypasses normal-path NCLIP rejection
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
  uint32_t override_control = 0;        // nonzero high byte selects the separate alternate parser
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

struct ResolvedFace {
  uint32_t source_ordinal = 0;
  uint32_t fragment_ordinal = 0;        // zero for normal unsplit/full faces; split key is explicit
  bool quad = false;
  ProjectedVertex vertex[4]{};
  ResolvedMaterial material{};
  uint32_t packet_attr[4]{};
  uint32_t ot_raw = 0;
  uint32_t ot_bin = 0;
};

struct OverlapDepthStats {
  uint64_t pairs = 0, bbox_overlap = 0, sampled_overlap = 0;
  uint64_t stable = 0, inverted = 0, ties = 0, disjoint = 0;
  uint64_t opaque_comparable = 0, covered_pixels = 0;
  uint64_t tri_tri = 0, tri_quad = 0, quad_quad = 0;
  uint64_t opaque_opaque = 0, opaque_semi = 0, semi_semi = 0;
  uint64_t inverted_same_bin = 0, inverted_diff_bin = 0;
  uint32_t first_inverted_a = UINT32_MAX, first_inverted_b = UINT32_MAX;
  uint32_t max_inverted_bin_delta = 0;
  int32_t first_x = 0, first_y = 0;
  float first_game_near_ord = 0, first_game_far_ord = 0, max_required_bias = 0;
};

// Pairwise interior overlap discriminator using the renderer's fixed quad split
// (0,1,2)+(1,2,3). Smaller OT bin is game-nearer; smaller positive view_z is D32-nearer.
OverlapDepthStats analyze_overlap_depth(std::span<const ResolvedFace> faces);

struct FaceCompareResult {
  uint32_t compared = 0;
  uint32_t expected = 0;
  uint32_t actual = 0;
  uint32_t mismatch_index = 0;
  std::string first_field;              // empty only when every ordered face matches
  explicit operator bool() const { return first_field.empty(); }
};

struct FaceCompareOptions {
  bool depth = true;
  bool ot_bin = true;
};

// Ordered runtime-oracle comparison. The result always contains both denominators and names the
// first differing field; an empty side is therefore a count mismatch, never a silent success.
FaceCompareResult compare_ordered_faces(std::span<const ResolvedFace> expected,
                                        std::span<const ResolvedFace> actual,
                                        FaceCompareOptions options = {});

struct ResolveResult {
  std::vector<ResolvedFace> faces;
  uint32_t candidates = 0;
  uint32_t triangles = 0;
  uint32_t quads = 0;
  std::string error;
  explicit operator bool() const { return error.empty(); }
};

// Resolve the normal stream against the producer-owned projected table. Primitive projected offsets
// are guest byte offsets into its four-byte SXY/SZ slots, hence offset/4 selects ProjectedVertex.
// Rejected raw<=0 OT candidates are counted but omitted. Output is guest drain order: descending bin,
// stable source order within a bin, with source_ordinal retained as the oracle join key.
ResolveResult resolve_normal_faces(std::span<const Primitive> primitives,
                                   std::span<const ProjectedVertex> projected,
                                   const MaterialTables& materials,
                                   uint32_t depth_origin, uint8_t shift);

// Exact 0x80025348/0x800255F0 normal-path bin expression. Returns false for the guest's raw<=0 gate.
bool compute_ot_bin(const Primitive& primitive, const uint32_t vertex_depth[4],
                    uint32_t depth_origin, uint8_t shift, uint32_t& raw, uint32_t& bin);

// The guest accumulates FIFO chains per bin, then drains bins high-to-low. Stable sort expresses the
// same contract without exposing packet pointers: descending bins, source order preserved in a bin.
std::vector<OrderedPrimitive> stable_descending_bins(std::span<const OrderedPrimitive> input);

}  // namespace spyro::paired_actor
