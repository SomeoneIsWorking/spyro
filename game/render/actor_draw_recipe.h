#pragma once

#include "actor_prefix_builder.h"

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
  Semi,
  Ft4,
  Malformed,
  BinRange,
  Prefix,
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
  std::vector<Candidate> candidateOrder;
  std::vector<Face> faces;
};

// One authoritative reached primitive evaluator. Inputs are immutable semantic
// values, never guest addresses or scratch pointers.
Evaluation evaluate(const PrimitiveInput &input);

QuadDecision classifyQuad(int32_t firstArea, int32_t secondArea, bool twoSided);

// Atomically composes complete prefix outputs. Unsupported/malformed input
// clears every face; a complete call with no accepted faces is ValidEmpty.
Recipe compose(std::span<const actor_prefix::Output> records);

} // namespace spyro::actor_draw_recipe
