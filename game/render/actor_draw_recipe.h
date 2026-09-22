#pragma once

#include "actor_prefix_builder.h"
#include "face_light_program.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace spyro::actor_draw_recipe {

enum class Family : uint8_t { G4, GT4, G3, GT3 };
enum class Origin : uint8_t { Direct, QuadFirst, QuadSecond, FullQuad };
enum class QuadDecision : uint8_t { Reject, First, Second, Full };
enum class Reason : uint8_t {
  None,
  Outcode,
  Skip,
  Nclip,
  ZeroArea,
  Depth,
  Ft4,
  // The four ways a record's own stream is malformed. They were one `Malformed` value, and a
  // refusal that printed `reason=7` could not say whether the primitive ran off the end of the
  // record or pointed one vertex past its array — which is the entire difference between an
  // unported arm and a decoder bug.
  ShortPrimitive, // fewer words remain than this primitive's own header requires
  VertexOffset,   // a vertex offset is unaligned or past the record's vertex array
  ColorOffset,    // a colour offset is unaligned or past the record's colour array
  NextWord,       // the evaluator's advance does not move forward, or leaves the stream
  BinRange,
  Prefix,
  FaceLight,
};
enum class Status : uint8_t { NoCorpus, Ready, ValidEmpty, Unsupported };

struct PrimitiveInput {
  std::array<uint32_t, 10> words{};
  std::array<uint32_t, 4> status{};
  std::array<uint32_t, 4> xy{};
  std::array<uint32_t, 4> depth{};
  std::array<uint32_t, 4> color{};
  uint32_t depthOrigin = 0;
  uint32_t shift = 0;
  uint32_t fog = 0;
  std::array<float, 4> screenX{};
  std::array<float, 4> screenY{};
  std::array<float, 4> viewZ{};
  // The view-space coordinates retail's projection loop stores per vertex, kept because the bit-2
  // colour program differences them. `lighting` records what that program made of this face:
  // Ready both when it produced `color` and when the face never asked for it.
  std::array<face_light::ViewVertex, 4> view{};
  uint32_t lightingControl = 0;
  face_light::Status lighting = face_light::Status::Ready;
};

struct Evaluation {
  bool supported = true;
  bool emitted = false;
  Family family = Family::G3;
  Origin origin = Origin::Direct;
  Reason reason = Reason::None;
  uint32_t nextWord = 0;
  uint32_t localBin = 0;
  std::vector<uint32_t> payload;
};

struct Candidate {
  uint32_t record = 0;
  uint32_t sourceWord = 0;
  PrimitiveInput input;
  Evaluation evaluation;
};

struct Face {
  uint32_t record = 0;
  uint32_t moby = 0; // The guest Moby instance owning this face, or 0 when unknown.
  uint32_t sourceWord = 0;
  uint32_t sourceOrdinal = 0;
  Family family = Family::G3;
  Origin origin = Origin::Direct;
  uint32_t localBin = 0;
  PrimitiveInput input;
  std::vector<uint32_t> payload;
};

struct Recipe {
  Status status = Status::NoCorpus;
  Reason firstReason = Reason::None;
  uint32_t records = 0;
  uint32_t visibleRecords = 0;
  uint32_t rejectedRecords = 0;
  uint32_t candidates = 0;
  uint32_t rejectedCandidates = 0;
  // Emitted faces that took the bit-2 colour program, the denominator for any claim that it ran.
  uint32_t faceLightFaces = 0;
  uint32_t firstUnsupportedRecord = 0;
  uint32_t firstUnsupportedSourceWord = 0;
  std::array<uint32_t, 2> firstUnsupportedWords{};
  // For a vertex- or colour-offset refusal: which of the primitive's slots, the byte offset the
  // stream asked for, and the size of the array it ran off. Without these, `reason=color-offset`
  // names the kind of defect but not the one number that decides whether the decoder is wrong or
  // the record is.
  uint32_t firstUnsupportedSlot = 0;
  uint32_t firstUnsupportedOffset = 0;
  uint32_t firstUnsupportedLimit = 0;
  std::vector<Candidate> candidateOrder;
  std::vector<Face> faces;
};

// Names for the two values a refusal reports, so the abort reads as words rather than as the
// integers an enum happens to be numbered with.
const char *statusName(Status status);
const char *reasonName(Reason reason);

// One authoritative reached primitive evaluator. Inputs are immutable semantic
// values, never guest addresses or scratch pointers.
Evaluation evaluate(const PrimitiveInput &input);

QuadDecision classifyQuad(int32_t firstArea, int32_t secondArea, bool twoSided);

// Atomically composes complete prefix outputs. Unsupported/malformed input
// clears every face; a complete call with no accepted faces is ValidEmpty.
// Without a lighting environment the bit-2 colour program cannot run, and the faces that ask for
// it are refused by name rather than drawn with the material colours retail would have replaced.
Recipe compose(std::span<const actor_prefix::Output> records,
               const face_light::Environment &lighting = {});

} // namespace spyro::actor_draw_recipe
