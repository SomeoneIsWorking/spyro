// Observation-only groundwork for the 0x800521C0 -> 0x8001F158 -> 0x8001F798 actor chain.
#include "actor_draw_recipe.h"
#include "actor_prefix_builder.h"
#include "cfg.h"
#include "core.h"
#include "rec_decls.h"
#include "recomp_iface.h"
#include "spyro_game.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <lucent/log.h>
#include <string_view>
#include <unordered_map>
#include <vector>

int gpu_vk_wide_engine(Core *);
namespace {
constexpr uint32_t kRecordBase = 0x800712F4u; // 0x8006FCF4 + 5632
constexpr uint32_t kRecordSize = 56u;
constexpr uint32_t kDurableRecords = 53u;  // source indices 0..52 only
constexpr uint32_t kTerminatorIndex = 53u; // exact terminator observed at 0x80071E8C
constexpr uint32_t kPoolPtr = 0x800757B0u;

struct PrefixBuildCapture {
  struct Record {
    spyro::actor_prefix::Input input;
    spyro::actor_prefix::Output expected;
    uint32_t descriptor = 0, command = 0, colorBase = 0, fog = 0;
    bool colorSeen = false;
  };
  std::vector<Record> records;
  uint32_t activeRecord = 0;
  uint32_t verticesPre = 0, verticesPost = 0, controlsCompared = 0;
  uint32_t inputMismatch = 0, controlMismatch = 0, rawViewMismatch = 0;
  std::array<uint32_t, 16> controlMismatchByReg{};
  uint32_t firstExpected = 0, firstActual = 0, firstIndex = 0;
  uint32_t irMismatch = 0, sxyMismatch = 0, szMismatch = 0;
  uint32_t finalCheckpoint = 0, pointerMismatch = 0;
  uint32_t scratchWordsCompared = 0, scratchWordMismatch = 0;
  uint32_t highRecords = 0, highColorsCaptured = 0;
  uint32_t positiveRecords = 0, positiveColorsCompared = 0, colorMismatch = 0;
  uint32_t primitiveWordsCaptured = 0;
  uint32_t clipModeRecords = 0, clipModeVertices = 0;
  const char *first = "none";
};

static bool physical_span(uint32_t p, uint32_t bytes) {
  const uint32_t physical = p & 0x1fffffffu;
  return (p & 3u) == 0u && bytes >= 4u && bytes <= 0x200000u && physical <= 0x200000u - bytes;
}

static uint32_t kseg(uint32_t p) {
  return 0x80000000u | (p & 0x1fffffu);
}

template <class Read>
static bool
copy_primitive_words(Read read, uint32_t command, std::vector<uint32_t> &words, uint32_t &bytes) {
  words.clear();
  bytes = 0;
  if (!physical_span(command, 4u)) {
    return false;
  }
  bytes = read(kseg(command));
  if ((bytes & 3u) != 0u || bytes > 0x1ffffcu || !physical_span(command, bytes + 4u)) {
    return false;
  }
  words.reserve(bytes / 4u);
  for (uint32_t offset = 4; offset < bytes + 4u; offset += 4u) {
    words.push_back(read(kseg(command + offset)));
  }
  return true;
}

struct WordWrite {
  uint32_t addr = 0, value = 0;
};

// A deterministic sparse write overlay. Reads prefer the latest staged value, then fall through to
// the backing reader. The vector remains in first-touch order for exact post-state comparisons;
// the index makes repeated reads and updates independent of overlay size.
template <class BackingRead> class WordOverlay {
public:
  WordOverlay(BackingRead backing, std::vector<WordWrite> &entries)
      : mBacking(std::move(backing)), mEntries(entries) {
    for (size_t i = 0; i < mEntries.size(); ++i) {
      mIndex[mEntries[i].addr] = i;
    }
  }
  uint32_t read32(uint32_t addr) const {
    const auto found = mIndex.find(addr);
    return found == mIndex.end() ? mBacking(addr) : mEntries[found->second].value;
  }
  void write32(uint32_t addr, uint32_t value) {
    const auto found = mIndex.find(addr);
    if (found != mIndex.end()) {
      mEntries[found->second].value = value;
      return;
    }
    mIndex.emplace(addr, mEntries.size());
    mEntries.push_back({addr, value});
  }
  void clear() {
    mEntries.clear();
    mIndex.clear();
  }
  const std::vector<WordWrite> &entries() const {
    return mEntries;
  }
  uint32_t operator()(uint32_t addr) const {
    return read32(addr);
  }

private:
  BackingRead mBacking;
  std::vector<WordWrite> &mEntries;
  std::unordered_map<uint32_t, size_t> mIndex;
};
template <class BackingRead>
WordOverlay(BackingRead, std::vector<WordWrite> &) -> WordOverlay<BackingRead>;

enum class Family : uint8_t { G4, GT4, G3, GT3, FT4, Unsupported };
struct PacketKey {
  uint32_t packet = 0, record = 0;
  Family family = Family::Unsupported;
};
struct SourceSnapshot {
  uint32_t record = 0, source = 0, r1 = 0, aux = 0, scratch = 0, depthOrigin = 0, shift = 0;
  uint32_t depthBase = 0, colorBase = 0, fog = 0, pool = 0, localOt = 0;
  std::array<uint32_t, 10> words{};
  std::array<uint32_t, 4> status{};
  std::array<uint32_t, 4> xy{}, depth{}, color{};
};
static bool durable_record(uint32_t p) {
  return p >= kRecordBase && p < kRecordBase + kDurableRecords * kRecordSize &&
         ((p - kRecordBase) % kRecordSize) == 0;
}
static bool ram_word_span(uint32_t p, uint32_t bytes) {
  return (p & 3u) == 0u && p >= 0x80000000u && bytes >= 4u && p <= 0x801FFFFFu - (bytes - 1u);
}
static bool scratch_word(uint32_t p) {
  return (p & 3u) == 0u && p >= 0x1F800000u && p <= 0x1F8003FCu;
}
static bool guest_word(uint32_t p) {
  return scratch_word(p) || ram_word_span(p, 4u);
}
template <class Read>
static bool capture_source(Read read,
                           uint32_t record,
                           uint32_t source,
                           uint32_t auxAddr,
                           uint32_t r1,
                           uint32_t scratch,
                           uint32_t depthOrigin,
                           uint32_t shift,
                           SourceSnapshot &out) {
  if (!durable_record(record) || !ram_word_span(source, 40u) || !ram_word_span(auxAddr, 4u)) {
    return false;
  }
  out = {};
  out.record = record;
  out.source = source;
  out.r1 = r1;
  out.scratch = scratch;
  out.depthOrigin = depthOrigin;
  out.shift = shift;
  out.aux = read(auxAddr);
  for (uint32_t i = 0; i < out.words.size(); ++i) {
    out.words[i] = read(source + i * 4u);
  }
  return true;
}

struct PacketCensus {
  uint32_t packets = 0, bytes = 0, f3 = 0, g3 = 0, ft3 = 0, gt3 = 0, f4 = 0, g4 = 0, ft4 = 0,
           gt4 = 0, semi = 0, raw = 0, other = 0;
  const char *first = "none";
  std::vector<PacketKey> entries;
};

enum class PrefixColorArm : uint8_t { High, PositiveBlend, Plain, NegativeBlend, Invalid };
struct PrefixCensus {
  uint32_t setups = 0, declaredVertices = 0;
  uint32_t primaryAbsolute = 0, primaryDelta = 0, alternateAbsolute = 0, alternateDelta = 0;
  uint32_t primaryDecisions = 0, alternateDecisions = 0;
  std::array<uint32_t, 4> primarySites{};
  std::array<uint32_t, 2> alternateSites{};
  uint32_t colorSelected = 0, high = 0, positiveBlend = 0, plain = 0, negativeBlend = 0;
  uint32_t colorMismatch = 0;
  uint64_t matched = 0;
};

enum class PrefixResult : uint8_t { NoCorpus, Pass, Fail };

static PrefixResult prefix_result(uint32_t setups, bool complete) {
  if (setups == 0) {
    return PrefixResult::NoCorpus;
  }
  return complete ? PrefixResult::Pass : PrefixResult::Fail;
}

static const char *prefix_result_name(PrefixResult result) {
  switch (result) {
  case PrefixResult::NoCorpus:
    return "NO_CORPUS";
  case PrefixResult::Pass:
    return "PASS";
  case PrefixResult::Fail:
    return "FAIL";
  }
  return "FAIL";
}

static bool prefix_complete(const PrefixCensus &c) {
  const uint32_t primarySites =
      c.primarySites[0] + c.primarySites[1] + c.primarySites[2] + c.primarySites[3];
  const uint32_t alternateSites = c.alternateSites[0] + c.alternateSites[1];
  const uint64_t expectedMatched =
      (uint64_t)c.setups + c.primaryDecisions + c.alternateDecisions + c.colorSelected;
  return c.setups != 0 && c.declaredVertices != 0 &&
         c.primaryDecisions == c.primaryAbsolute + c.primaryDelta &&
         c.alternateDecisions == c.alternateAbsolute + c.alternateDelta &&
         c.primaryDecisions == primarySites && c.alternateDecisions == alternateSites &&
         c.colorSelected == c.high + c.positiveBlend + c.plain + c.negativeBlend &&
         c.colorSelected != 0 && c.colorMismatch == 0 && c.matched == expectedMatched;
}

static PrefixColorArm expected_color_arm(int32_t cr29, int32_t cr30) {
  if (cr30 >= 1024) {
    return PrefixColorArm::High;
  }
  if (cr30 > 0) {
    return PrefixColorArm::PositiveBlend;
  }
  if (cr29 > 0 && cr30 < -2048) {
    return PrefixColorArm::NegativeBlend;
  }
  return PrefixColorArm::Plain;
}

static void
prefix_count_selector(PrefixCensus &o, bool alternate, uint32_t site, uint32_t selector) {
  if (alternate) {
    ++o.alternateDecisions;
    ++o.alternateSites[site];
    ((selector & 1u) ? o.alternateDelta : o.alternateAbsolute)++;
  } else {
    ++o.primaryDecisions;
    ++o.primarySites[site];
    ((selector & 1u) ? o.primaryDelta : o.primaryAbsolute)++;
  }
}

template <class Read>
static PrefixColorArm
actual_color_arm(Read read, uint32_t descriptor, uint32_t commands, uint32_t colors, uint32_t fog) {
  if (!ram_word_span(descriptor, 36u)) {
    return PrefixColorArm::Invalid;
  }
  const bool high =
      commands == read(descriptor + 20u) && colors == read(descriptor + 24u) && fog == 0;
  const bool positive = commands == read(descriptor + 20u) && colors == 0x80070DF4u;
  const bool plain =
      commands == read(descriptor + 28u) && colors == read(descriptor + 32u) && fog == 0;
  const bool negative = commands == read(descriptor + 28u) && colors == 0x80070DF4u;
  if ((unsigned)high + (unsigned)positive + (unsigned)plain + (unsigned)negative != 1u) {
    return PrefixColorArm::Invalid;
  }
  if (high) {
    return PrefixColorArm::High;
  }
  if (positive) {
    return PrefixColorArm::PositiveBlend;
  }
  if (plain) {
    return PrefixColorArm::Plain;
  }
  return PrefixColorArm::NegativeBlend;
}

static void prefix_checkpoint(Core *c, uint64_t, uint32_t pc, void *user) {
  auto &o = *static_cast<PrefixCensus *>(user);
  switch (pc) {
  case 0x8001FA1Cu:
    ++o.setups;
    o.declaredVertices += c->r[31];
    break;
  case 0x8001FA88u:
    prefix_count_selector(o, false, 0, c->r[6]);
    break;
  case 0x8001FB30u:
    prefix_count_selector(o, false, 1, c->r[6]);
    break;
  case 0x8001FC20u:
    prefix_count_selector(o, false, 2, c->r[6]);
    break;
  case 0x8001FCD8u:
    prefix_count_selector(o, false, 3, c->r[6]);
    break;
  case 0x8001FAC4u:
    prefix_count_selector(o, true, 0, c->r[12]);
    break;
  case 0x8001FB84u:
    prefix_count_selector(o, true, 1, c->r[12]);
    break;
  case 0x8001FF64u: {
    ++o.colorSelected;
    const PrefixColorArm expected =
        expected_color_arm((int32_t)gte_read_ctrl(29), (int32_t)gte_read_ctrl(30));
    const PrefixColorArm actual = actual_color_arm(
        [&](uint32_t p) {
          return c->mem_r32(p);
        },
        c->lo,
        c->r[30],
        c->r[25],
        c->r[18]);
    if (actual == PrefixColorArm::High) {
      ++o.high;
    } else if (actual == PrefixColorArm::PositiveBlend) {
      ++o.positiveBlend;
    } else if (actual == PrefixColorArm::Plain) {
      ++o.plain;
    } else if (actual == PrefixColorArm::NegativeBlend) {
      ++o.negativeBlend;
    }
    if (actual != expected) {
      ++o.colorMismatch;
    }
    break;
  }
  }
}
struct EpochState {
  bool active = false, bSeen = false, familySeen = false;
  uint32_t source = 0, record = 0;
};
static void epoch_clear(EpochState &e) {
  e = {};
}
static void epoch_open(EpochState &e, uint32_t source, uint32_t record) {
  e = {true, false, false, source, record};
}
static bool epoch_subset(EpochState &e, uint32_t source, uint32_t record) {
  if (!e.active || e.bSeen || e.source != source || e.record != record) {
    return false;
  }
  e.bSeen = true;
  return true;
}
static bool epoch_family(EpochState &e, uint32_t cursor, uint32_t record, uint32_t expectedCursor) {
  const bool ok = e.active && !e.familySeen && e.record == record && cursor == expectedCursor;
  e.familySeen = true;
  e.active = false;
  return ok;
}
struct CheckpointCensus {
  uint32_t familyArms = 0, insertions = 0, g4 = 0, gt4 = 0, g3 = 0, gt3 = 0, ft4 = 0,
           recordJoins = 0, badRecord = 0, badPacket = 0, postSplice = 0, finals = 0,
           firstRecord = 0, minRecord = 0xFFFFFFFFu, maxRecord = 0, sourceA = 0, sourceB = 0,
           badSource = 0, badSourceRecord = 0, badClassifier = 0, badSubset = 0, badFamily = 0,
           badTables = 0, payloadCompared = 0, payloadMismatch = 0, directTri = 0, quadFirst = 0,
           quadSecond = 0, unsupportedPayload = 0;
  uint32_t firstBadTable = 0;
  EpochState epoch{};
  std::vector<SourceSnapshot> sources;
  std::vector<PacketKey> entries;
  struct Expected {
    uint32_t packet = 0;
    Family family = Family::Unsupported;
    std::vector<uint32_t> words;
  };
  std::vector<Expected> expected;
  Expected pendingExpected{};
  PacketKey pendingEntry{};
  bool pendingFamily = false;
  enum class Outcome : uint8_t { Reject, G4, GT4, G3, GT3, Unsupported };
  enum class Origin : uint8_t { None, Direct, QuadFirst, QuadSecond, FullQuad };
  struct Prediction {
    bool valid = false;
    Outcome outcome = Outcome::Reject;
    Origin origin = Origin::None;
    uint32_t next = 0;
    const char *reason = "none";
  };
  Prediction prediction{};
  bool observed = false;
  Outcome observedOutcome = Outcome::Reject;
  Origin observedOrigin = Origin::None;
  uint32_t evaluated = 0, predictedReject = 0, predictedEmit = 0, predictedUnsupported = 0,
           outcodeReject = 0, nclipReject = 0, zeroReject = 0, skipReject = 0, depthReject = 0,
           evalDirect = 0, evalQuadFirst = 0, evalQuadSecond = 0, evalFullQuad = 0,
           evalTwoSided = 0, evalMismatch = 0, cursorMismatch = 0, terminators = 0,
           terminatorSubsets = 0, poolMismatch = 0;
};

static uint32_t cmd_family_size(uint8_t cmd) {
  switch (cmd & 0xFCu) {
  case 0x38:
    return 36u;
  case 0x3C:
    return 52u;
  case 0x30:
    return 28u;
  case 0x34:
    return 40u;
  case 0x2C:
    return 40u;
  default:
    return 0u;
  }
}

static uint32_t sar(uint32_t v, uint32_t n) {
  return (uint32_t)((int32_t)v >> (n & 31u));
}
static bool capture_tables(Core *c, SourceSnapshot &s, uint32_t &badAddr) {
  const uint32_t w0 = s.words[0], w1 = s.words[1], w2 = s.words[2];
  const uint32_t vo[] = {(w0 >> 20) & 0x7FCu, (w0 >> 11) & 0x7FCu, (w0 >> 2) & 0x7FCu, w2 & 0x7FCu};
  const uint32_t co[] = {
      (w1 >> 17) & 0x7FCu, (w1 >> 8) & 0x7FCu, (w1 << 1) & 0x7FCu, (w2 >> 9) & 0x7FCu};
  const unsigned count = (int32_t)w0 < 0 ? 4u : 3u;
  for (unsigned i = 0; i < count; ++i) {
    const uint32_t xp = s.scratch + vo[i], zp = s.depthBase + vo[i], cp = s.colorBase + co[i];
    if (!scratch_word(xp)) {
      badAddr = xp;
      return false;
    }
    if (!guest_word(zp)) {
      badAddr = zp;
      return false;
    }
    if (!ram_word_span(cp, 4u)) {
      badAddr = cp;
      return false;
    }
  }
  for (unsigned i = 0; i < count; ++i) {
    uint32_t xy = c->mem_r32(s.scratch + vo[i]);
    s.status[i] = xy;
    s.xy[i] = (int32_t(s.shift) < 0) ? sar(xy, 5) : xy;
    s.depth[i] = c->mem_r32(s.depthBase + vo[i]);
    s.color[i] = c->mem_r32(s.colorBase + co[i]);
  }
  s.color[0] &= 0x00FFFFFFu;
  return true;
}
static spyro::actor_draw_recipe::PrimitiveInput recipe_input(const SourceSnapshot &s) {
  return {s.words, s.status, s.xy, s.depth, s.color, s.depthOrigin, s.shift, s.fog};
}

static CheckpointCensus::Prediction evaluate_candidate(const SourceSnapshot &s) {
  using O = CheckpointCensus::Outcome;
  using R = CheckpointCensus::Origin;
  const auto result = spyro::actor_draw_recipe::evaluate(recipe_input(s));
  CheckpointCensus::Prediction p{true, O::Reject, R::None, s.source + result.nextWord * 4u, "none"};
  using AF = spyro::actor_draw_recipe::Family;
  using AO = spyro::actor_draw_recipe::Origin;
  using AR = spyro::actor_draw_recipe::Reason;
  if (!result.supported) {
    p.outcome = O::Unsupported;
  } else if (result.emitted) {
    p.outcome = result.family == AF::G4    ? O::G4
                : result.family == AF::GT4 ? O::GT4
                : result.family == AF::G3  ? O::G3
                                           : O::GT3;
  }
  if (result.emitted) {
    p.origin = result.origin == AO::Direct       ? R::Direct
               : result.origin == AO::QuadFirst  ? R::QuadFirst
               : result.origin == AO::QuadSecond ? R::QuadSecond
               : result.origin == AO::FullQuad   ? R::FullQuad
                                                 : R::None;
  }
  p.reason = result.reason == AR::Outcode    ? "outcode"
             : result.reason == AR::Nclip    ? "nclip"
             : result.reason == AR::ZeroArea ? "zero"
             : result.reason == AR::Skip     ? "skip"
             : result.reason == AR::Depth    ? "depth"
             : result.reason == AR::Ft4      ? "ft4"
             : result.reason == AR::Semi     ? "semi"
                                             : "none";
  return p;
}
static void finish_prediction(CheckpointCensus &o, Core *c) {
  if (!o.prediction.valid) {
    return;
  }
  ++o.evaluated;
  const bool emit = o.prediction.outcome != CheckpointCensus::Outcome::Reject &&
                    o.prediction.outcome != CheckpointCensus::Outcome::Unsupported;
  if (o.prediction.outcome == CheckpointCensus::Outcome::Unsupported) {
    ++o.predictedUnsupported;
  } else if (emit) {
    ++o.predictedEmit;
  } else {
    ++o.predictedReject;
  }
  if (o.sources.back().words[0] & 1u) {
    ++o.evalTwoSided;
  }
  if (o.prediction.origin == CheckpointCensus::Origin::Direct) {
    ++o.evalDirect;
  } else if (o.prediction.origin == CheckpointCensus::Origin::QuadFirst) {
    ++o.evalQuadFirst;
  } else if (o.prediction.origin == CheckpointCensus::Origin::QuadSecond) {
    ++o.evalQuadSecond;
  } else if (o.prediction.origin == CheckpointCensus::Origin::FullQuad) {
    ++o.evalFullQuad;
  }
  if (std::string_view(o.prediction.reason) == "outcode") {
    ++o.outcodeReject;
  } else if (std::string_view(o.prediction.reason) == "nclip") {
    ++o.nclipReject;
  } else if (std::string_view(o.prediction.reason) == "zero") {
    ++o.zeroReject;
  } else if (std::string_view(o.prediction.reason) == "skip") {
    ++o.skipReject;
  } else if (std::string_view(o.prediction.reason) == "depth") {
    ++o.depthReject;
  }
  if (o.prediction.next != c->r[30]) {
    ++o.cursorMismatch;
  }
  auto observed = CheckpointCensus::Outcome::Reject;
  const uint32_t start = o.sources.back().pool, after = c->r[24];
  if (after != start) {
    const uint8_t cmd = (uint8_t)(c->mem_r32(start + 4u) >> 24);
    switch (cmd & 0xFCu) {
    case 0x38:
      observed = CheckpointCensus::Outcome::G4;
      break;
    case 0x3C:
      observed = CheckpointCensus::Outcome::GT4;
      break;
    case 0x30:
      observed = CheckpointCensus::Outcome::G3;
      break;
    case 0x34:
      observed = CheckpointCensus::Outcome::GT3;
      break;
    case 0x2C:
      observed = CheckpointCensus::Outcome::Unsupported;
      break;
    default:
      ++o.poolMismatch;
    }
    const uint32_t bytes = cmd_family_size(cmd);
    if (!bytes || after != start + bytes) {
      ++o.poolMismatch;
    }
    ++o.insertions;
    if (observed == CheckpointCensus::Outcome::G4) {
      ++o.g4;
    } else if (observed == CheckpointCensus::Outcome::GT4) {
      ++o.gt4;
    } else if (observed == CheckpointCensus::Outcome::G3) {
      ++o.g3;
    } else if (observed == CheckpointCensus::Outcome::GT3) {
      ++o.gt3;
    } else if (observed == CheckpointCensus::Outcome::Unsupported) {
      ++o.ft4;
    }
    if (!o.pendingFamily) {
      ++o.badClassifier;
      ++o.unsupportedPayload;
    } else {
      o.pendingEntry.packet = start;
      o.entries.push_back(o.pendingEntry);
      o.pendingExpected.packet = start;
      o.expected.push_back(std::move(o.pendingExpected));
      ++o.recordJoins;
    }
  }
  if (observed != o.prediction.outcome || (emit && o.observedOrigin != o.prediction.origin)) {
    ++o.evalMismatch;
  }
  o.prediction = {};
  o.observed = false;
  o.pendingFamily = false;
  o.pendingExpected = {};
  o.pendingEntry = {};
}

static std::vector<uint32_t> expected_payload(const SourceSnapshot &s, Family f, bool quadSecond) {
  (void)quadSecond;
  const auto result = spyro::actor_draw_recipe::evaluate(recipe_input(s));
  const bool family =
      (f == Family::G4 && result.family == spyro::actor_draw_recipe::Family::G4) ||
      (f == Family::GT4 && result.family == spyro::actor_draw_recipe::Family::GT4) ||
      (f == Family::G3 && result.family == spyro::actor_draw_recipe::Family::G3) ||
      (f == Family::GT3 && result.family == spyro::actor_draw_recipe::Family::GT3);
  return result.emitted && family ? result.payload : std::vector<uint32_t>{};
}
struct PayloadCompare {
  uint32_t compared = 0, mismatches = 0, expected = 0, actual = 0, packet = 0, index = 0;
  const char *first = "none";
};

struct RecipeJoin {
  uint32_t candidatesCompared = 0, inputMismatch = 0, orderMismatch = 0;
  PayloadCompare payload{};
  const char *first = "none";
};

static Family recipe_family(spyro::actor_draw_recipe::Family family) {
  using F = spyro::actor_draw_recipe::Family;
  return family == F::G4    ? Family::G4
         : family == F::GT4 ? Family::GT4
         : family == F::G3  ? Family::G3
                            : Family::GT3;
}

static const char *compare_recipe_input(const spyro::actor_draw_recipe::PrimitiveInput &expected,
                                        uint32_t sourceWords,
                                        const SourceSnapshot &actual) {
  if (sourceWords > expected.words.size() || !std::equal(expected.words.begin(),
                                                         expected.words.begin() + sourceWords,
                                                         actual.words.begin())) {
    return "source_words";
  }
  if (expected.status != actual.status) {
    return "status";
  }
  if (expected.xy != actual.xy) {
    return "xy";
  }
  if (expected.depth != actual.depth) {
    return "depth";
  }
  if (expected.color != actual.color) {
    return "color";
  }
  if (expected.depthOrigin != actual.depthOrigin) {
    return "depth_origin";
  }
  if (expected.shift != actual.shift) {
    return "ot_shift";
  }
  if (expected.fog != actual.fog) {
    return "fog";
  }
  return "none";
}
template <class Read>
static PayloadCompare compare_payloads(const std::vector<CheckpointCensus::Expected> &expected,
                                       uint32_t poolBegin,
                                       uint32_t poolEnd,
                                       Read read) {
  PayloadCompare out{};
  for (const auto &e : expected) {
    const uint64_t end = (uint64_t)e.packet + e.words.size() * 4u;
    if (e.words.empty() || e.packet < poolBegin || end > poolEnd) {
      ++out.mismatches;
      if (out.first == std::string_view("none")) {
        out.first = "span";
      }
      continue;
    }
    for (size_t i = 0; i < e.words.size(); ++i) {
      const uint32_t actual = read(e.packet + (uint32_t)i * 4u);
      const bool same = i == 0 ? (actual & 0xFF000000u) == e.words[i] : actual == e.words[i];
      if (!same) {
        ++out.mismatches;
        if (out.first == std::string_view("none")) {
          out.first = i == 0 ? "tag" : i == 1 ? "command_color" : i % 2 == 0 ? "xy" : "color_uv";
          out.expected = e.words[i];
          out.actual = actual;
          out.packet = e.packet;
          out.index = (uint32_t)i;
        }
        break;
      }
    }
    ++out.compared;
  }
  return out;
}

struct OtEntry {
  uint32_t packet = 0, bin = 0;
};
struct OtCensus {
  EpochState epoch{};
  SourceSnapshot source{};
  bool haveSource = false;
  uint32_t candidates = 0, emitted = 0, pre = 0, post = 0, finals = 0, binsScanned = 0,
           nonempty = 0, nodes = 0, badSource = 0, badEpoch = 0, badBin = 0, cycles = 0,
           duplicates = 0, outOfRange = 0, emptyAppend = 0, nonemptyAppend = 0;
  std::vector<OtEntry> expected, actual;
  std::vector<WordWrite> globalExpected;
  uint32_t globalRecords = 0, noLocal = 0, groups = 0, globalEmpty = 0, globalPreexisting = 0,
           baseBounce = 0, tagPatches = 0, localClears = 0, globalCompared = 0, globalMismatch = 0;
  const spyro::actor_draw_recipe::Recipe *recipe = nullptr;
  const std::vector<PrefixBuildCapture::Record> *prefixRecords = nullptr;
  uint32_t recipeCandidate = 0, recipeFace = 0, recipeInputMismatch = 0, recipeOrderMismatch = 0;
  const char *recipeFirst = "none";
};
template <class Read>
static bool simulate_global(Read read,
                            uint32_t minPair,
                            uint32_t maxPair,
                            uint32_t cr13,
                            uint32_t cr14,
                            uint32_t globalBase,
                            std::vector<WordWrite> &writes,
                            OtCensus &count) {
  WordOverlay overlay(std::move(read), writes);
  overlay.clear();
  ++count.globalRecords;
  if ((int32_t)(maxPair - minPair) < 0) {
    ++count.noLocal;
    return true;
  }
  const uint32_t s = ((cr13 >> 8) + (cr13 & 255u)) & 31u, step = 256u >> s;
  if (!step || minPair > maxPair || ((maxPair - minPair) & 7u)) {
    return false;
  }
  uint32_t off = (cr14 << 3) + ((32u << s) - 8u);
  const uint32_t gap = 0x800704F4u - maxPair;
  const uint32_t adjust = (int32_t)gap < 0 ? 0u : (((gap << s) >> 8) << 3);
  off -= adjust;
  if ((int32_t)off < 0) {
    off = 0;
  }
  uint32_t global = globalBase + off, scan = maxPair, terminal = minPair - 8u;
  while (true) {
    ++count.groups;
    uint32_t candidate = scan - step;
    const uint32_t limit = (int32_t)(terminal - candidate) > 0 ? terminal : candidate;
    uint32_t head = overlay.read32(global), tail = overlay.read32(global + 4u), cursor = head;
    overlay.write32(global, head);
    overlay.write32(global + 4u, tail);
    bool have = head != 0;
    if (have) {
      ++count.globalPreexisting;
      if (!tail) {
        return false;
      }
    } else {
      ++count.globalEmpty;
      if (tail) {
        return false;
      }
    }
    for (uint32_t p = scan; p != limit; p -= 8u) {
      const uint32_t newest = overlay.read32(p), oldest = overlay.read32(p + 4u);
      overlay.write32(p, newest);
      overlay.write32(p + 4u, oldest);
      if (!newest && !oldest) {
        continue;
      }
      if (!newest || !oldest) {
        return false;
      }
      if (!have) {
        tail = oldest;
        cursor = newest;
        have = true;
      } else {
        const uint32_t tag = overlay.read32(cursor);
        overlay.write32(cursor, (tag & 0xFF000000u) | (oldest & 0x00FFFFFFu));
        ++count.tagPatches;
        cursor = newest;
      }
      overlay.write32(p, 0);
      overlay.write32(p + 4u, 0);
      count.localClears += 2;
    }
    if (have) {
      overlay.write32(global, cursor);
      overlay.write32(global + 4u, tail);
    }
    if (limit == terminal) {
      break;
    }
    scan = limit;
    if (global == globalBase) {
      global = globalBase + 8u;
      ++count.baseBounce;
    } else {
      global -= 8u;
    }
  }
  return true;
}
template <class Read>
static uint32_t compare_global_words(const std::vector<WordWrite> &expected, const Read &read) {
  uint32_t mismatches = 0;
  for (const auto &w : expected) {
    mismatches += read(w.addr) != w.value;
  }
  return mismatches;
}
static bool compare_ot(const std::vector<OtEntry> &a,
                       const std::vector<OtEntry> &b,
                       uint32_t &first,
                       const char *&field) {
  if (a.size() != b.size()) {
    first = (uint32_t)std::min(a.size(), b.size());
    field = "count";
    return false;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i].bin != b[i].bin) {
      first = (uint32_t)i;
      field = "bin";
      return false;
    }
    if (a[i].packet != b[i].packet) {
      first = (uint32_t)i;
      field = "fifo";
      return false;
    }
  }
  first = 0;
  field = "none";
  return true;
}
template <class Read> static void snapshot_ot_read(OtCensus &o, const Read &read) {
  if (!o.haveSource) {
    return;
  }
  const uint32_t base = o.source.localOt, startNonempty = o.nonempty;
  std::vector<uint32_t> seen;
  for (uint32_t bin = 0; bin < 288u; ++bin) {
    ++o.binsScanned;
    const uint32_t newest = read(base + bin * 8u), oldest = read(base + bin * 8u + 4u);
    if (!newest && !oldest) {
      continue;
    }
    ++o.nonempty;
    if (!newest || !oldest) {
      ++o.outOfRange;
      continue;
    }
    uint32_t p = oldest;
    bool reached = false;
    for (uint32_t guard = 0; guard < o.expected.size() + 1u; ++guard) {
      if (p < 0x80000000u || p >= 0x80200000u) {
        ++o.outOfRange;
        break;
      }
      if (std::find(seen.begin(), seen.end(), p) != seen.end()) {
        ++o.duplicates;
        break;
      }
      seen.push_back(p);
      o.actual.push_back({p, bin});
      ++o.nodes;
      if (p == newest) {
        reached = true;
        break;
      }
      const uint32_t next = 0x80000000u | (read(p) & 0x00FFFFFFu);
      if (next == p) {
        ++o.cycles;
        break;
      }
      p = next;
    }
    if (!reached) {
      ++o.cycles;
    }
  }
  if (o.nonempty != startNonempty) {
    for (const auto &e : o.expected) {
      unsigned prior = 0;
      for (const auto &q : o.expected) {
        if (&q == &e) {
          break;
        }
        if (q.bin == e.bin) {
          ++prior;
        }
      }
      if (prior) {
        ++o.nonemptyAppend;
      } else {
        ++o.emptyAppend;
      }
    }
  }
}
static void snapshot_ot(Core *c, OtCensus &o) {
  snapshot_ot_read(o, [&](uint32_t p) {
    return c->mem_r32(p);
  });
}
static void actor_ot_checkpoint(Core *c, uint64_t, uint32_t pc, void *user) {
  auto &o = *static_cast<OtCensus *>(user);
  if (pc == 0x8001FFF8u) {
    epoch_clear(o.epoch);
    o.haveSource = false;
    ++o.candidates;
    SourceSnapshot s{};
    const uint32_t record = c->lo, source = c->r[30];
    const bool terminator = source == c->r[31];
    if (!capture_source(
            [&](uint32_t p) {
              return c->mem_r32(p);
            },
            record,
            source,
            source + 4u,
            c->r[1],
            c->r[29],
            c->r[22],
            c->r[23],
            s)) {
      ++o.badSource;
      return;
    }
    s.depthBase = c->r[28];
    s.colorBase = c->r[25];
    s.fog = c->r[18];
    s.pool = c->r[24];
    s.localOt = c->r[19];
    uint32_t bad = 0;
    if (!capture_tables(c, s, bad)) {
      ++o.badSource;
      return;
    }
    o.source = s;
    o.haveSource = true;
    epoch_open(o.epoch, source, record);
    if (terminator) {
      return;
    }
    if (!o.recipe || !o.prefixRecords || o.recipeCandidate >= o.recipe->candidateOrder.size()) {
      ++o.recipeInputMismatch;
      if (o.recipeFirst == std::string_view{"none"}) {
        o.recipeFirst = "candidate_count";
      }
      return;
    }
    const auto &expected = o.recipe->candidateOrder[o.recipeCandidate++];
    const bool recordInRange = expected.record < o.prefixRecords->size();
    const bool identity =
        recordInRange && record == kRecordBase + expected.record * kRecordSize &&
        source == (*o.prefixRecords)[expected.record].command + 4u + expected.sourceWord * 4u;
    const char *inputField =
        identity ? compare_recipe_input(expected.input, expected.evaluation.nextWord, s) : "none";
    if (!identity || inputField != std::string_view{"none"}) {
      ++o.recipeInputMismatch;
      if (o.recipeFirst == std::string_view{"none"}) {
        o.recipeFirst = identity ? inputField : "candidate_identity";
      }
    }
    return;
  }
  if (pc == 0x8002074Cu) {
    ++o.pre;
    snapshot_ot(c, o);
    const uint32_t globalBase = c->mem_r32(0x80075820u);
    if (!simulate_global(
            [&](uint32_t p) {
              return c->mem_r32(p);
            },
            c->r[20],
            c->r[21],
            gte_read_ctrl(13),
            gte_read_ctrl(14),
            globalBase,
            o.globalExpected,
            o)) {
      ++o.outOfRange;
    }
    return;
  }
  if (pc == 0x80020860u) {
    ++o.post;
    o.globalMismatch += compare_global_words(o.globalExpected, [&](uint32_t p) {
      return c->mem_r32(p);
    });
    o.globalCompared += o.globalExpected.size();
    return;
  }
  if (pc == 0x800208ACu) {
    ++o.finals;
    return;
  }
  Family f = Family::Unsupported;
  if (pc == 0x800201A8u) {
    f = Family::G4;
  } else if (pc == 0x8002023Cu) {
    f = Family::GT4;
  } else if (pc == 0x80020430u) {
    f = Family::G3;
  } else if (pc == 0x8002051Cu) {
    f = Family::GT3;
  } else {
    return;
  }
  ++o.emitted;
  if (!o.haveSource) {
    ++o.badEpoch;
    return;
  }
  const bool quad = (int32_t)o.source.words[0] < 0;
  uint32_t cursor = o.epoch.source +
                    ((f == Family::G4 || f == Family::G3) ? (quad ? 12u : 8u) : (quad ? 24u : 20u));
  if (!epoch_family(o.epoch, c->r[30], c->lo, cursor)) {
    ++o.badEpoch;
    return;
  }
  if (!o.recipe || o.recipeFace >= o.recipe->faces.size()) {
    ++o.recipeOrderMismatch;
    o.recipeFirst = "face_count";
    return;
  }
  const auto &face = o.recipe->faces[o.recipeFace++];
  if (recipe_family(face.family) != f) {
    ++o.recipeOrderMismatch;
    if (o.recipeFirst == std::string_view{"none"}) {
      o.recipeFirst = "family_order";
    }
  }
  const uint32_t addr = o.source.localOt + (face.localBin << 3);
  if (addr < o.source.localOt || ((addr - o.source.localOt) & 7u) ||
      (addr - o.source.localOt) / 8u >= 288u) {
    ++o.badBin;
    return;
  }
  o.expected.push_back({c->r[24], (addr - o.source.localOt) / 8u});
}

static void actor_chain_prefix_oracle(Core *c) {
  static constexpr uint32_t targets[] = {0x8001FA1Cu,
                                         0x8001FA88u,
                                         0x8001FAC4u,
                                         0x8001FB30u,
                                         0x8001FB84u,
                                         0x8001FC20u,
                                         0x8001FCD8u,
                                         0x8001FF64u};
  PrefixCensus census{};
  if (!c->pcObserver.arm(targets, std::size(targets), prefix_checkpoint, &census)) {
    abort();
  }
  gen_func_8001F798(c);
  const uint64_t seen = c->pcObserver.seen(), matched = c->pcObserver.matched();
  c->pcObserver.disarm();
  census.matched = matched;
  const PrefixResult result = prefix_result(census.setups, prefix_complete(census));
  lucent::info(
      "actorchainprefix",
      "pass=prefix checkpoints={}/{} setups={} vertices_declared={} "
      "primary[decisions={} absolute={} delta={} sites={},{},{},{}] "
      "alternate[decisions={} absolute={} delta={} sites={},{}] "
      "color[selected={} high={} positive_blend={} plain={} negative_blend={} mismatch={}] "
      "omitted[far=unobserved terminator=unobserved outcode=unobserved] result={}",
      seen,
      matched,
      census.setups,
      census.declaredVertices,
      census.primaryDecisions,
      census.primaryAbsolute,
      census.primaryDelta,
      census.primarySites[0],
      census.primarySites[1],
      census.primarySites[2],
      census.primarySites[3],
      census.alternateDecisions,
      census.alternateAbsolute,
      census.alternateDelta,
      census.alternateSites[0],
      census.alternateSites[1],
      census.colorSelected,
      census.high,
      census.positiveBlend,
      census.plain,
      census.negativeBlend,
      census.colorMismatch,
      prefix_result_name(result));
}

static bool
copy_prefix_stream(Core *c, uint32_t model, uint32_t count, spyro::actor_prefix::OwnedStream &out) {
  if (!physical_span(model, 8u)) {
    return false;
  }
  const uint32_t model0 = c->mem_r32(kseg(model));
  const uint32_t model1 = c->mem_r32(kseg(model + 4u));
  uint32_t full = model0 & 0x1fffffu;
  uint32_t delta = full + ((model1 >> 24) << 2);
  if (!physical_span(full, 4u) || delta > 0x1fffffu) {
    return false;
  }
  out = {};
  out.firstFull = c->mem_r32(kseg(full));
  full += 4;
  uint32_t selector = out.firstFull;
  for (uint32_t i = 1; i < count; ++i) {
    if (selector & 1u) {
      if (delta > 0x1ffffeu) {
        return false;
      }
      const int16_t word = (int16_t)c->mem_r16(kseg(delta));
      out.deltaWords.push_back(word);
      selector = (uint16_t)word;
      delta += 2;
    } else {
      if (!physical_span(full, 4u)) {
        return false;
      }
      selector = c->mem_r32(kseg(full));
      out.fullWords.push_back(selector);
      full += 4;
    }
  }
  return true;
}

static bool capture_prefix_record(Core *c, uint32_t record, PrefixBuildCapture::Record &capture) {
  if (c->mem_r32(record) == 0) {
    return false;
  }
  const uint32_t descriptor = c->mem_r32(record + 4u);
  const uint32_t model = c->mem_r32(record + 8u);
  const uint32_t alternate = c->mem_r32(record + 12u);
  if (!physical_span(descriptor, 36u) || !physical_span(model, 8u)) {
    return false;
  }
  auto &input = capture.input;
  input = {};
  input.header = c->mem_r32(record);
  input.tx = (int32_t)c->mem_r32(record + 16u);
  input.ty = (int32_t)c->mem_r32(record + 20u);
  input.tz = (int32_t)c->mem_r32(record + 24u);
  for (uint32_t i = 0; i < input.matrixWords.size(); ++i) {
    input.matrixWords[i] = c->mem_r32(record + 28u + i * 4u);
  }
  input.cr29 = (int32_t)gte_read_ctrl(29);
  input.cr30 = (int16_t)(input.matrixWords[4] >> 16);
  input.transformShift = c->mem_r8(kseg(descriptor + 5u));
  input.streamShift = (uint8_t)(c->mem_r8(kseg(descriptor + 6u)) + 1u);
  input.vertexCount = c->mem_r8(kseg(descriptor + 8u));
  const uint32_t modelMeta = c->mem_r32(kseg(model + 4u));
  input.optionalExpansion = input.cr30 > 0 && (((modelMeta << 16) >> 14) != 0);
  const uint16_t blend = (uint16_t)((input.header & 0xff00u) >> 2);
  if (input.vertexCount != 0 && !copy_prefix_stream(c, model, input.vertexCount, input.primary)) {
    return false;
  }
  if (blend != 0 && (!physical_span(alternate, 8u) ||
                     !copy_prefix_stream(c, alternate, input.vertexCount, input.alternate))) {
    return false;
  }
  input.projection = {.ofx = (int32_t)gte_read_ctrl(24),
                      .ofy = (int32_t)gte_read_ctrl(25),
                      .h = (uint16_t)gte_read_ctrl(26)};
  if (input.cr30 >= 1024) {
    input.colorArm = spyro::actor_prefix::ColorArm::High;
  } else if (input.cr30 > 0) {
    input.colorArm = spyro::actor_prefix::ColorArm::PositiveBlend;
  } else if (input.cr29 <= 0 || input.cr30 >= -2048) {
    input.colorArm = spyro::actor_prefix::ColorArm::Plain;
  } else {
    input.colorArm = spyro::actor_prefix::ColorArm::NegativeBlend;
  }
  const uint32_t colorCount = c->mem_r16(kseg(descriptor + 2u));
  const uint32_t primaryColors = c->mem_r32(kseg(descriptor + 24u));
  const uint32_t secondaryColors = c->mem_r32(kseg(descriptor + 32u));
  if (!physical_span(primaryColors, std::max(1u, colorCount) * 4u)) {
    return false;
  }
  input.primaryColors.reserve(colorCount);
  for (uint32_t i = 0; i < colorCount; ++i) {
    input.primaryColors.push_back(c->mem_r32(kseg(primaryColors + i * 4u)));
  }
  if (input.colorArm == spyro::actor_prefix::ColorArm::PositiveBlend) {
    if (!physical_span(secondaryColors, std::max(1u, colorCount) * 4u)) {
      return false;
    }
    input.secondaryColors.reserve(colorCount);
    for (uint32_t i = 0; i < colorCount; ++i) {
      input.secondaryColors.push_back(c->mem_r32(kseg(secondaryColors + i * 4u)));
    }
  }
  const uint32_t command = c->mem_r32(kseg(descriptor + 20u));
  uint32_t primitiveBytes = 0;
  if (!copy_primitive_words(
          [&](uint32_t p) {
            return c->mem_r32(p);
          },
          command,
          input.primitiveWords,
          primitiveBytes)) {
    return false;
  }
  capture.descriptor = kseg(descriptor);
  capture.command = kseg(command);
  capture.colorBase =
      input.colorArm == spyro::actor_prefix::ColorArm::High ? kseg(primaryColors) : 0x80070DF4u;
  capture.expected = spyro::actor_prefix::build(input);
  capture.fog = capture.expected.fog;
  return true;
}

static bool capture_prefix_records(Core *c, std::vector<PrefixBuildCapture::Record> &records) {
  records.clear();
  records.reserve(kDurableRecords);
  for (uint32_t i = 0; i <= kTerminatorIndex; ++i) {
    const uint32_t address = kRecordBase + i * kRecordSize;
    if (c->mem_r32(address) == 0) {
      return !records.empty();
    }
    PrefixBuildCapture::Record record{};
    if (i == kTerminatorIndex || !capture_prefix_record(c, address, record)) {
      records.clear();
      return false;
    }
    records.push_back(std::move(record));
  }
  records.clear();
  return false;
}

static spyro::actor_draw_recipe::Recipe
compose_prefix_records(std::span<const PrefixBuildCapture::Record> records,
                       std::vector<spyro::actor_prefix::Output> &outputs) {
  outputs.clear();
  outputs.reserve(records.size());
  for (const auto &record : records) {
    outputs.push_back(record.expected);
  }
  return spyro::actor_draw_recipe::compose(outputs);
}

static void prefix_build_first(PrefixBuildCapture &capture,
                               const char *field,
                               uint32_t index = 0,
                               uint32_t expected = 0,
                               uint32_t actual = 0) {
  if (capture.first == std::string_view{"none"}) {
    capture.first = field;
    capture.firstIndex = index;
    capture.firstExpected = expected;
    capture.firstActual = actual;
  }
}

static bool prefix_build_rtps(uint32_t pc) {
  return pc == 0x8001FAC0u || pc == 0x8001FB80u || pc == 0x8001FCE0u || pc == 0x8001FD14u;
}

static uint32_t prefix_build_record_vertex_base(const PrefixBuildCapture &capture) {
  uint32_t base = 0;
  for (uint32_t i = 0; i < capture.activeRecord; ++i) {
    base += (uint32_t)capture.records[i].expected.vertices.size();
  }
  return base;
}

static void prefix_build_pre(Core *, uint64_t, uint32_t pc, uint32_t, void *user) {
  auto &capture = *static_cast<PrefixBuildCapture *>(user);
  if (!prefix_build_rtps(pc)) {
    return;
  }
  if (capture.activeRecord >= capture.records.size()) {
    ++capture.inputMismatch;
    prefix_build_first(capture, "extra_rtps");
    return;
  }
  const auto &expected = capture.records[capture.activeRecord].expected;
  const uint32_t index = capture.verticesPre++ - prefix_build_record_vertex_base(capture);
  if (index >= expected.vertices.size()) {
    ++capture.inputMismatch;
    prefix_build_first(capture, "extra_rtps");
    return;
  }
  const auto &vertex = expected.vertices[index];
  const uint32_t dr0 =
      (uint16_t)vertex.projectionInput.x | ((uint32_t)(uint16_t)vertex.projectionInput.y << 16);
  if (gte_read_data(0) != dr0 || (int16_t)gte_read_data(1) != vertex.projectionInput.z) {
    ++capture.inputMismatch;
    prefix_build_first(capture, "projection_input", index, dr0, gte_read_data(0));
  }
  for (uint32_t i = 0; i < 8; ++i) {
    ++capture.controlsCompared;
    if (gte_read_ctrl(i) != expected.controls[i]) {
      ++capture.controlMismatch;
      ++capture.controlMismatchByReg[i];
      prefix_build_first(capture, "control", i, expected.controls[i], gte_read_ctrl(i));
    }
  }
  for (uint32_t i = 13; i < 16; ++i) {
    ++capture.controlsCompared;
    if (gte_read_ctrl(i) != expected.controls[i]) {
      ++capture.controlMismatch;
      ++capture.controlMismatchByReg[i];
      prefix_build_first(capture, "control", i, expected.controls[i], gte_read_ctrl(i));
    }
  }
}

static void prefix_build_post(Core *, uint64_t, uint32_t pc, uint32_t, void *user) {
  auto &capture = *static_cast<PrefixBuildCapture *>(user);
  if (!prefix_build_rtps(pc)) {
    return;
  }
  if (capture.activeRecord >= capture.records.size()) {
    return;
  }
  const auto &expected = capture.records[capture.activeRecord].expected;
  const uint32_t index = capture.verticesPost++ - prefix_build_record_vertex_base(capture);
  if (index >= expected.vertices.size()) {
    return;
  }
  const auto &projected = expected.vertices[index].projected;
  for (uint32_t i = 0; i < 3; ++i) {
    if ((int32_t)gte_read_data(25u + i) != (int32_t)(projected.raw_view_fixed[i] >> 12)) {
      ++capture.rawViewMismatch;
      prefix_build_first(capture, "mac");
    }
    if ((int16_t)gte_read_data(9u + i) != projected.ir[i]) {
      ++capture.irMismatch;
      prefix_build_first(capture, "ir");
    }
  }
  const uint32_t sxy = (uint16_t)projected.sx | ((uint32_t)(uint16_t)projected.sy << 16);
  if (gte_read_data(14) != sxy) {
    ++capture.sxyMismatch;
    prefix_build_first(capture, "sxy");
  }
  if ((uint16_t)gte_read_data(19) != projected.sz) {
    ++capture.szMismatch;
    prefix_build_first(capture, "sz");
  }
}

static void prefix_build_color(Core *c, uint64_t, uint32_t, void *user) {
  auto &capture = *static_cast<PrefixBuildCapture *>(user);
  if (capture.activeRecord >= capture.records.size()) {
    ++capture.pointerMismatch;
    prefix_build_first(capture, "extra_record");
    return;
  }
  auto &record = capture.records[capture.activeRecord];
  if (c->lo != record.descriptor || c->r[30] != record.command || c->r[25] != record.colorBase ||
      c->r[18] != record.fog) {
    ++capture.pointerMismatch;
    prefix_build_first(capture, "final_pointer");
  }
  if (record.input.colorArm == spyro::actor_prefix::ColorArm::PositiveBlend) {
    for (uint32_t i = 0; i < record.expected.colors.size(); ++i) {
      ++capture.positiveColorsCompared;
      if (c->mem_r32(record.colorBase + i * 4u) != record.expected.colors[i]) {
        ++capture.colorMismatch;
        prefix_build_first(capture, "positive_color");
      }
    }
  }
  record.colorSeen = true;
}

static void prefix_build_record_end(Core *c, uint64_t, uint32_t, void *user) {
  auto &capture = *static_cast<PrefixBuildCapture *>(user);
  ++capture.finalCheckpoint;
  if (capture.activeRecord >= capture.records.size()) {
    ++capture.pointerMismatch;
    prefix_build_first(capture, "extra_record_end");
    return;
  }
  const auto &record = capture.records[capture.activeRecord];
  for (uint32_t i = 0; i < record.expected.vertices.size(); ++i) {
    ++capture.scratchWordsCompared;
    const uint32_t actual = c->mem_r32(0x1F800000u + i * 4u);
    if (actual != record.expected.vertices[i].scratchWord) {
      ++capture.scratchWordMismatch;
      prefix_build_first(
          capture, "scratch_word", i, record.expected.vertices[i].scratchWord, actual);
    }
  }
  const bool shouldSeeColor = record.expected.status == spyro::actor_prefix::Status::Ok;
  if (record.colorSeen != shouldSeeColor) {
    ++capture.pointerMismatch;
    prefix_build_first(capture, "color_lifecycle");
  }
  ++capture.activeRecord;
}

static void prefix_build_checkpoint(Core *c, uint64_t cycle, uint32_t pc, void *user) {
  if (pc == 0x8001FF64u) {
    prefix_build_color(c, cycle, pc, user);
  } else if (pc == 0x80020860u) {
    prefix_build_record_end(c, cycle, pc, user);
  }
}

static void actor_chain_prefix_build_oracle(Core *c) {
  PrefixBuildCapture capture{};
  const bool captured = capture_prefix_records(c, capture.records);
  std::vector<spyro::actor_prefix::Output> outputs;
  std::array<uint32_t, 9> statusCounts{};
  uint32_t expectedVertices = 0, expectedPositiveColors = 0, expectedVisibleRecords = 0;
  for (const auto &record : capture.records) {
    ++statusCounts[(uint32_t)record.expected.status];
    expectedVertices += (uint32_t)record.expected.vertices.size();
    if ((int32_t)record.input.header < 0) {
      ++capture.clipModeRecords;
      capture.clipModeVertices += (uint32_t)record.expected.vertices.size();
    }
    capture.primitiveWordsCaptured += (uint32_t)record.input.primitiveWords.size();
    if (record.input.colorArm == spyro::actor_prefix::ColorArm::PositiveBlend) {
      ++capture.positiveRecords;
      expectedPositiveColors += (uint32_t)record.expected.colors.size();
    } else if (record.input.colorArm == spyro::actor_prefix::ColorArm::High) {
      ++capture.highRecords;
      capture.highColorsCaptured += (uint32_t)record.input.primaryColors.size();
    }
    expectedVisibleRecords += record.expected.status == spyro::actor_prefix::Status::Ok;
  }
  const auto recipe = compose_prefix_records(capture.records, outputs);
  const auto boundary = spyro::actor_prefix::classifyCall(outputs);
  const bool recipeComplete = recipe.status == spyro::actor_draw_recipe::Status::Ready ||
                              recipe.status == spyro::actor_draw_recipe::Status::ValidEmpty;
  const bool supported =
      captured && boundary.status == spyro::actor_prefix::CallStatus::Owned && recipeComplete;
  static constexpr uint32_t targets[] = {0x8001FF64u, 0x80020860u};
  if (supported) {
    if (!c->pcObserver.arm(targets, std::size(targets), prefix_build_checkpoint, &capture)) {
      abort();
    }
    gte_op_observer_arm(c, prefix_build_pre, prefix_build_post, &capture);
  }
  gen_func_8001F798(c);
  const uint64_t gteSeen = gte_preop_observer_disarm(c);
  const uint64_t pcSeen = c->pcObserver.seen(), pcMatched = c->pcObserver.matched();
  c->pcObserver.disarm();
  const bool pass = supported && capture.verticesPre == expectedVertices &&
                    capture.verticesPost == expectedVertices && capture.inputMismatch == 0 &&
                    capture.controlMismatch == 0 && capture.rawViewMismatch == 0 &&
                    capture.irMismatch == 0 && capture.sxyMismatch == 0 &&
                    capture.szMismatch == 0 && capture.finalCheckpoint == capture.records.size() &&
                    capture.pointerMismatch == 0 && capture.colorMismatch == 0 &&
                    capture.scratchWordMismatch == 0 &&
                    capture.scratchWordsCompared == expectedVertices &&
                    capture.positiveColorsCompared == expectedPositiveColors &&
                    capture.activeRecord == capture.records.size() &&
                    pcMatched == capture.records.size() + expectedVisibleRecords;
  lucent::info("actorchainprefixbuild",
               "captured={} supported={} records={} vertices[expected={} pre={} post={}] "
               "gte_seen={} controls={} "
               "mismatch[input={} control={} raw_view={} ir={} sxy={} sz={} pointer={} "
               "positive_color={} scratch_word={}] "
               "control_regs[0..7={},{},{},{},{},{},{},{} 13..15={},{},{}] "
               "final={}/{} pc_seen={} pc_matched={}/{} "
               "boundary[visible={} rejected={} unsupported={}] "
               "clip_mode[records={} vertices={}] "
               "status[ok={} optional_expansion={} transform_blend={} count_zero={} stream={} "
               "plain={} negative={} color_count={} visibility_rejected={}] "
               "scratch_words={}/{} "
               "colors[high_records={} high_captured={} positive_records={} "
               "positive_compared={}/{}] primitive_words_captured={} "
               "recipe[status={} candidates={} rejected={} faces={} first_reason={}] "
               "first={}#{}[exp={:08X} act={:08X}] result={}",
               captured,
               supported,
               capture.records.size(),
               expectedVertices,
               capture.verticesPre,
               capture.verticesPost,
               gteSeen,
               capture.controlsCompared,
               capture.inputMismatch,
               capture.controlMismatch,
               capture.rawViewMismatch,
               capture.irMismatch,
               capture.sxyMismatch,
               capture.szMismatch,
               capture.pointerMismatch,
               capture.colorMismatch,
               capture.scratchWordMismatch,
               capture.controlMismatchByReg[0],
               capture.controlMismatchByReg[1],
               capture.controlMismatchByReg[2],
               capture.controlMismatchByReg[3],
               capture.controlMismatchByReg[4],
               capture.controlMismatchByReg[5],
               capture.controlMismatchByReg[6],
               capture.controlMismatchByReg[7],
               capture.controlMismatchByReg[13],
               capture.controlMismatchByReg[14],
               capture.controlMismatchByReg[15],
               capture.finalCheckpoint,
               capture.records.size(),
               pcSeen,
               pcMatched,
               capture.records.size() + expectedVisibleRecords,
               boundary.visibleRecords,
               boundary.rejectedRecords,
               boundary.unsupportedRecords,
               capture.clipModeRecords,
               capture.clipModeVertices,
               statusCounts[0],
               statusCounts[1],
               statusCounts[2],
               statusCounts[3],
               statusCounts[4],
               statusCounts[5],
               statusCounts[6],
               statusCounts[7],
               statusCounts[8],
               capture.scratchWordsCompared,
               expectedVertices,
               capture.highRecords,
               capture.highColorsCaptured,
               capture.positiveRecords,
               capture.positiveColorsCompared,
               expectedPositiveColors,
               capture.primitiveWordsCaptured,
               recipe.status == spyro::actor_draw_recipe::Status::NoCorpus     ? "NO_CORPUS"
               : recipe.status == spyro::actor_draw_recipe::Status::Ready      ? "READY"
               : recipe.status == spyro::actor_draw_recipe::Status::ValidEmpty ? "VALID_EMPTY"
                                                                               : "UNSUPPORTED",
               recipe.candidates,
               recipe.rejectedCandidates,
               recipe.faces.size(),
               (unsigned)recipe.firstReason,
               capture.first,
               capture.firstIndex,
               capture.firstExpected,
               capture.firstActual,
               !captured    ? "NO_CORPUS"
               : !supported ? "REFUSED"
               : pass       ? "PASS"
                            : "FAIL");
}

static void actor_chain_ot_oracle(Core *c) {
  if (gpu_vk_wide_engine(c)) {
    lucent::error("actorchainoracle",
                  "REFUSED: OT diagnostic conflicts with widescreen 0x8001F798 hook");
    abort();
  }
  static constexpr uint32_t targets[] = {0x8001FFF8u,
                                         0x800201A8u,
                                         0x8002023Cu,
                                         0x80020430u,
                                         0x8002051Cu,
                                         0x8002074Cu,
                                         0x80020860u,
                                         0x800208ACu};
  std::vector<PrefixBuildCapture::Record> prefixRecords;
  const bool recipeCaptured = capture_prefix_records(c, prefixRecords);
  std::vector<spyro::actor_prefix::Output> prefixOutputs;
  const auto recipe = compose_prefix_records(prefixRecords, prefixOutputs);
  OtCensus o{};
  o.recipe = &recipe;
  o.prefixRecords = &prefixRecords;
  if (!c->pcObserver.arm(targets, std::size(targets), actor_ot_checkpoint, &o)) {
    abort();
  }
  gen_func_8001F798(c);
  const uint64_t seen = c->pcObserver.seen(), matched = c->pcObserver.matched();
  c->pcObserver.disarm();
  std::stable_sort(o.expected.begin(), o.expected.end(), [](const OtEntry &a, const OtEntry &b) {
    return a.bin < b.bin;
  });
  uint32_t first = 0;
  const char *field = "none";
  const uint64_t expectedMatched = (uint64_t)o.candidates + o.emitted + o.pre + o.post + o.finals;
  const bool ordered = compare_ot(o.expected, o.actual, first, field);
  const bool positive =
      recipeCaptured && recipe.status == spyro::actor_draw_recipe::Status::Ready &&
      o.recipeCandidate == recipe.candidateOrder.size() && o.recipeFace == recipe.faces.size() &&
      o.recipeInputMismatch == 0 && o.recipeOrderMismatch == 0 && o.candidates > 0 &&
      o.emitted > 0 && matched == expectedMatched && o.pre > 0 && o.pre == o.post &&
      o.finals == 1 && o.binsScanned == 288u * o.pre && o.nonempty > 0 && o.emptyAppend > 0 &&
      o.nonemptyAppend > 0 && o.expected.size() == o.emitted && o.nodes == o.emitted &&
      o.globalRecords == o.pre && o.globalCompared > 0 && o.globalMismatch == 0 &&
      o.badSource == 0 && o.badEpoch == 0 && o.badBin == 0 && o.cycles == 0 && o.duplicates == 0 &&
      o.outOfRange == 0 && ordered;
  lucent::info(
      "actorchainoracle",
      "pass=ot checkpoints={}/{} candidates={} emitted={} expected={} actual={} bins_scanned={} "
      "nonempty={} append[empty={} nonempty={}] pre/post/final={}/{}/{} global[records={} "
      "no_local={} groups={} empty={} preexisting={} bounce={} patches={} clears={} compared={} "
      "mismatch={}] bad_source={} bad_epoch={} bad_bin={} cycles={} duplicates={} out_of_range={} "
      "ordered={} first={} field={} recipe[captured={} status={} candidates={}/{} "
      "input_mismatch={} "
      "faces={}/{} order_mismatch={} first={}] result={}",
      seen,
      matched,
      o.candidates,
      o.emitted,
      o.expected.size(),
      o.actual.size(),
      o.binsScanned,
      o.nonempty,
      o.emptyAppend,
      o.nonemptyAppend,
      o.pre,
      o.post,
      o.finals,
      o.globalRecords,
      o.noLocal,
      o.groups,
      o.globalEmpty,
      o.globalPreexisting,
      o.baseBounce,
      o.tagPatches,
      o.localClears,
      o.globalCompared,
      o.globalMismatch,
      o.badSource,
      o.badEpoch,
      o.badBin,
      o.cycles,
      o.duplicates,
      o.outOfRange,
      ordered,
      first,
      field,
      recipeCaptured,
      (unsigned)recipe.status,
      o.recipeCandidate,
      recipe.candidateOrder.size(),
      o.recipeInputMismatch,
      o.recipeFace,
      recipe.faces.size(),
      o.recipeOrderMismatch,
      o.recipeFirst,
      positive ? "PASS" : "FAIL");
}

void actor_checkpoint(Core *c, uint64_t, uint32_t pc, void *user) {
  auto &o = *static_cast<CheckpointCensus *>(user);
  if (pc == 0x8002031Cu) {
    ++o.sourceB;
    if (c->r[30] == c->r[31]) {
      ++o.terminatorSubsets;
      return;
    }
    if (!epoch_subset(o.epoch, c->r[30], c->lo)) {
      ++o.badClassifier;
      ++o.badSubset;
    }
    return;
  }
  if (pc == 0x8001FFF8u) {
    finish_prediction(o, c);
    epoch_clear(o.epoch);
    if (c->r[30] == c->r[31]) {
      ++o.terminators;
      return;
    }
    SourceSnapshot s{};
    const uint32_t record = c->lo, source = c->r[30], auxAddr = c->r[30] + 4u;
    if (!capture_source(
            [&](uint32_t p) {
              return c->mem_r32(p);
            },
            record,
            source,
            auxAddr,
            c->r[1],
            c->r[29],
            c->r[22],
            c->r[23],
            s)) {
      ++o.badSource;
      if (!durable_record(record)) {
        ++o.badSourceRecord;
      }
    } else {
      s.depthBase = c->r[28];
      s.colorBase = c->r[25];
      s.fog = c->r[18];
      s.pool = c->r[24];
      s.localOt = c->r[19];
      uint32_t badAddr = 0;
      if (!capture_tables(c, s, badAddr)) {
        ++o.badSource;
        ++o.badTables;
        if (!o.firstBadTable) {
          o.firstBadTable = badAddr;
        }
      } else {
        o.sources.push_back(s);
        epoch_open(o.epoch, source, record);
        o.prediction = evaluate_candidate(s);
      }
    }
    ++o.sourceA;
    return;
  }
  if (pc == 0x80020860u) {
    ++o.postSplice;
    return;
  }
  if (pc == 0x800208ACu) {
    finish_prediction(o, c);
    ++o.finals;
    return;
  }
  Family family = Family::Unsupported;
  if (pc == 0x800201A8u) {
    family = Family::G4;
  } else if (pc == 0x8002023Cu) {
    family = Family::GT4;
  } else if (pc == 0x80020430u) {
    family = Family::G3;
  } else if (pc == 0x8002051Cu) {
    family = Family::GT3;
  } else if (pc == 0x8002066Cu) {
    family = Family::FT4;
  } else {
    return;
  }
  ++o.familyArms;
  // 1F798 saves the 0x38 record cursor in LO before reusing r28 as the material/scratch base.
  const uint32_t record = c->lo, packet = c->r[24], pool = c->mem_r32(kPoolPtr);
  if (!o.firstRecord) {
    o.firstRecord = record;
  }
  o.minRecord = std::min(o.minRecord, record);
  o.maxRecord = std::max(o.maxRecord, record);
  if (!durable_record(record)) {
    ++o.badRecord;
  }
  if (o.sources.empty()) {
    ++o.unsupportedPayload;
    return;
  }
  if (packet != o.sources.back().pool || packet < pool || packet >= 0x80200000u) {
    ++o.badPacket;
  }
  o.observed = true;
  if (family == Family::G4) {
    o.observedOutcome = CheckpointCensus::Outcome::G4;
  } else if (family == Family::GT4) {
    o.observedOutcome = CheckpointCensus::Outcome::GT4;
  } else if (family == Family::G3) {
    o.observedOutcome = CheckpointCensus::Outcome::G3;
  } else if (family == Family::GT3) {
    o.observedOutcome = CheckpointCensus::Outcome::GT3;
  } else {
    o.observedOutcome = CheckpointCensus::Outcome::Unsupported;
    o.observedOrigin = CheckpointCensus::Origin::None;
    o.pendingFamily = true;
    o.pendingEntry = {packet, record, family};
    o.pendingExpected = {packet, family, {}};
    return;
  }
  const bool quad = (int32_t)o.sources.back().words[0] < 0;
  uint32_t expectedCursor = o.epoch.source;
  if (family == Family::G4 || family == Family::G3) {
    expectedCursor += quad ? 12u : 8u;
  } else if (family == Family::GT4 || family == Family::GT3) {
    expectedCursor += quad ? 24u : 20u;
  }
  if (!epoch_family(o.epoch, c->r[30], record, expectedCursor)) {
    ++o.badClassifier;
    ++o.badFamily;
    ++o.unsupportedPayload;
    return;
  }
  const bool second =
      (family == Family::G3 || family == Family::GT3) && quad && (int32_t)c->r[17] > 0;
  if (family == Family::G4 || family == Family::GT4) {
    o.observedOrigin = CheckpointCensus::Origin::FullQuad;
  } else if (!quad) {
    ++o.directTri;
    o.observedOrigin = CheckpointCensus::Origin::Direct;
  } else if (second) {
    ++o.quadSecond;
    o.observedOrigin = CheckpointCensus::Origin::QuadSecond;
  } else {
    ++o.quadFirst;
    o.observedOrigin = CheckpointCensus::Origin::QuadFirst;
  }
  auto words = expected_payload(o.sources.back(), family, second);
  if (words.empty()) {
    ++o.unsupportedPayload;
  } else {
    o.pendingFamily = true;
    o.pendingEntry = {packet, record, family};
    o.pendingExpected = {packet, family, std::move(words)};
  }
}

static Family command_family(uint8_t cmd) {
  switch (cmd & 0xFCu) {
  case 0x38:
    return Family::G4;
  case 0x3C:
    return Family::GT4;
  case 0x30:
    return Family::G3;
  case 0x34:
    return Family::GT3;
  case 0x2C:
    return Family::FT4;
  default:
    return Family::Unsupported;
  }
}
static bool compare_ordered(const std::vector<PacketKey> &expected,
                            const std::vector<PacketKey> &actual) {
  if (expected.size() != actual.size()) {
    return false;
  }
  for (size_t i = 0; i < expected.size(); ++i) {
    if (expected[i].packet != actual[i].packet || expected[i].family != actual[i].family) {
      return false;
    }
  }
  return true;
}
static const char *payload_result(uint32_t candidates,
                                  bool corePositive,
                                  bool corruptionApplicable,
                                  bool corruptionRejected) {
  if (candidates == 0) {
    return "NO_CORPUS";
  }
  return corePositive && (!corruptionApplicable || corruptionRejected) ? "PASS" : "FAIL";
}
static bool payload_accounting(uint32_t candidates,
                               uint32_t evaluated,
                               uint32_t emitted,
                               uint32_t rejected,
                               uint32_t unsupported,
                               uint32_t packets) {
  return candidates > 0 && evaluated == candidates &&
         emitted + rejected + unsupported == candidates && emitted == packets;
}

template <class Read>
bool parse_packets(uint32_t begin, uint32_t end, Read read, PacketCensus &out) {
  if (end < begin || ((end - begin) & 3u)) {
    out.first = "span";
    return false;
  }
  uint32_t p = begin;
  while (p < end) {
    if (end - p < 8u) {
      out.first = "header";
      return false;
    }
    const uint32_t tag = read(p), words = tag >> 24, bytes = (words + 1u) * 4u;
    if (words == 0u || bytes > end - p) {
      out.first = "stride";
      return false;
    }
    const uint8_t cmd = (uint8_t)(read(p + 4u) >> 24);
    out.semi += (cmd & 2u) != 0u;
    out.raw += (cmd & 1u) != 0u;
    switch (cmd & 0xFCu) {
    case 0x20:
      ++out.f3;
      break;
    case 0x30:
      ++out.g3;
      break;
    case 0x24:
      ++out.ft3;
      break;
    case 0x34:
      ++out.gt3;
      break;
    case 0x28:
      ++out.f4;
      break;
    case 0x38:
      ++out.g4;
      break;
    case 0x2C:
      ++out.ft4;
      break;
    case 0x3C:
      ++out.gt4;
      break;
    default:
      ++out.other;
      out.first = "command";
      return false;
    }
    out.entries.push_back({p, 0, command_family(cmd)});
    ++out.packets;
    out.bytes += bytes;
    p += bytes;
  }
  return p == end;
}

void actor_chain_oracle(Core *c) {
  if (gpu_vk_wide_engine(c)) {
    lucent::error("actorchainoracle",
                  "REFUSED: diagnostic override would displace active 0x8001F798 widescreen hook");
    abort();
  }
  std::vector<PrefixBuildCapture::Record> prefixRecords;
  const bool recipeCaptured = capture_prefix_records(c, prefixRecords);
  std::vector<spyro::actor_prefix::Output> prefixOutputs;
  const auto recipe = compose_prefix_records(prefixRecords, prefixOutputs);
  const uint32_t before = c->mem_r32(kPoolPtr);
  // Payload/source pass: two source heads + five family sites + final = PcObserver's exact limit.
  static constexpr uint32_t targets[] = {0x8001FFF8u,
                                         0x8002031Cu,
                                         0x800201A8u,
                                         0x8002023Cu,
                                         0x80020430u,
                                         0x8002051Cu,
                                         0x8002066Cu,
                                         0x800208ACu};
  CheckpointCensus checkpoints{};
  if (!c->pcObserver.arm(targets, std::size(targets), actor_checkpoint, &checkpoints)) {
    abort();
  }
  gen_func_8001F798(c);
  const uint64_t seen = c->pcObserver.seen(), matched = c->pcObserver.matched();
  c->pcObserver.disarm();
  const uint32_t after = c->mem_r32(kPoolPtr);
  PacketCensus census{};
  const bool parsed = parse_packets(
      before,
      after,
      [&](uint32_t p) {
        return c->mem_r32(p);
      },
      census);
  const PayloadCompare payload =
      compare_payloads(checkpoints.expected, before, after, [&](uint32_t p) {
        return c->mem_r32(p);
      });
  RecipeJoin recipeJoin{};
  const size_t candidateCount = std::min(recipe.candidateOrder.size(), checkpoints.sources.size());
  for (size_t i = 0; i < candidateCount; ++i) {
    const auto &expected = recipe.candidateOrder[i];
    const auto &actual = checkpoints.sources[i];
    ++recipeJoin.candidatesCompared;
    const bool recordInRange = expected.record < prefixRecords.size();
    const bool identity =
        recordInRange && actual.record == kRecordBase + expected.record * kRecordSize &&
        actual.source == prefixRecords[expected.record].command + 4u + expected.sourceWord * 4u;
    const char *inputField =
        identity ? compare_recipe_input(expected.input, expected.evaluation.nextWord, actual)
                 : "none";
    if (!identity || inputField != std::string_view{"none"}) {
      ++recipeJoin.inputMismatch;
      if (recipeJoin.first == std::string_view{"none"}) {
        recipeJoin.first = identity ? inputField : "candidate_identity";
      }
    }
  }
  if (recipe.candidateOrder.size() != checkpoints.sources.size()) {
    ++recipeJoin.inputMismatch;
    recipeJoin.first = "candidate_count";
  }
  std::vector<CheckpointCensus::Expected> recipePayloads;
  if (recipe.faces.size() == census.entries.size()) {
    recipePayloads.reserve(recipe.faces.size());
    for (size_t i = 0; i < recipe.faces.size(); ++i) {
      const Family family = recipe_family(recipe.faces[i].family);
      recipeJoin.orderMismatch += family != census.entries[i].family;
      recipePayloads.push_back({census.entries[i].packet, family, recipe.faces[i].payload});
    }
    recipeJoin.payload = compare_payloads(recipePayloads, before, after, [&](uint32_t p) {
      return c->mem_r32(p);
    });
  } else {
    ++recipeJoin.orderMismatch;
    recipeJoin.first = "face_count";
  }
  bool ordered = compare_ordered(checkpoints.entries, census.entries);
  const char *orderedFirst = "none";
  if (!ordered) {
    orderedFirst = "address_or_family";
  }
  const bool families = checkpoints.g4 == census.g4 && checkpoints.gt4 == census.gt4 &&
                        checkpoints.g3 == census.g3 && checkpoints.gt3 == census.gt3 &&
                        checkpoints.ft4 == census.ft4 && census.f3 == 0 && census.ft3 == 0 &&
                        census.f4 == 0 && census.other == 0;
  const uint64_t expectedMatched = (uint64_t)checkpoints.sourceA + checkpoints.terminators +
                                   checkpoints.sourceB + checkpoints.familyArms +
                                   checkpoints.finals;
  const bool positive =
      recipeCaptured && parsed && ordered && families && checkpoints.insertions == census.packets &&
      checkpoints.insertions == checkpoints.recordJoins && checkpoints.badRecord == 0 &&
      checkpoints.badPacket == 0 && checkpoints.sourceA > 0 && !checkpoints.sources.empty() &&
      checkpoints.badSource == 0 && checkpoints.badClassifier == 0 &&
      checkpoints.badSourceRecord == 0 && checkpoints.badTables == 0 && checkpoints.finals == 1 &&
      matched == expectedMatched && checkpoints.unsupportedPayload == 0 &&
      payload.compared == census.packets && payload.mismatches == 0 &&
      payload_accounting(checkpoints.sourceA,
                         checkpoints.evaluated,
                         checkpoints.predictedEmit,
                         checkpoints.predictedReject,
                         checkpoints.predictedUnsupported,
                         census.packets) &&
      checkpoints.evalMismatch == 0 && checkpoints.cursorMismatch == 0;
  const bool recipePositive =
      recipeCaptured && recipe.status == spyro::actor_draw_recipe::Status::Ready &&
      recipeJoin.candidatesCompared == recipe.candidateOrder.size() &&
      recipeJoin.inputMismatch == 0 && recipeJoin.orderMismatch == 0 &&
      recipeJoin.payload.compared == recipe.faces.size() && recipeJoin.payload.mismatches == 0;
  const bool corruptionApplicable = after > before;
  bool negative = false;
  if (after > before) {
    PacketCensus corrupt{};
    negative = !parse_packets(
                   before,
                   after,
                   [&](uint32_t p) {
                     uint32_t v = c->mem_r32(p);
                     if (p == before + 4u) {
                       v = (v & 0x00FFFFFFu) | 0x7C000000u;
                     }
                     return v;
                   },
                   corrupt) &&
               corrupt.first == std::string_view("command");
  }
  const char *result =
      !recipeCaptured
          ? "NO_TERMINATOR"
          : payload_result(
                checkpoints.sourceA, positive && recipePositive, corruptionApplicable, negative);
  lucent::info(
      "actorchainoracle",
      "pass=payload records={}/{} terminated={} checkpoints={}/{} candidates={} "
      "terminator_heads={} positive_subset={} terminator_subset={} emitted={} "
      "candidate_minus_packets={} eval[evaluated={} emit={} reject={} unsupported={} mismatch={} "
      "cursor_mismatch={} outcode={} nclip={} zero={} skip={} depth={} two_sided={} "
      "origins[direct={} quad_first={} quad_second={} full_quad={}]] bad_source={} "
      "bad_source_record={} bad_classifier={} [subset={} family={}] bad_tables={} "
      "first_bad_table={:08X} joins={} ordered={} ordered_first={} payload={}/{} "
      "payload_mismatch={} payload_first={} payload_witness[p={:08X} i={} exp={:08X} act={:08X}] "
      "unsupported_payload={} origin[direct={} quad_first={} quad_second={}] bad_record={} "
      "record_range={:08X}..{:08X} first_record={:08X} bad_packet={} families[G4={} GT4={} G3={} "
      "GT3={} FT4={}] final={} packets={} bytes={} F3={} G3={} FT3={} GT3={} F4={} G4={} FT4={} "
      "GT4={} semi={} raw={} other={} recipe[captured={} candidates={}/{} input_mismatch={} "
      "faces={}/{} order_mismatch={} payload={}/{} payload_mismatch={} first={}] "
      "corruption[applicable={} rejected={}] first={} result={}",
      prefixRecords.size(),
      kDurableRecords,
      recipeCaptured,
      seen,
      matched,
      checkpoints.sourceA,
      checkpoints.terminators,
      checkpoints.sourceB,
      checkpoints.terminatorSubsets,
      checkpoints.insertions,
      (int64_t)checkpoints.sourceA - (int64_t)checkpoints.insertions,
      checkpoints.evaluated,
      checkpoints.predictedEmit,
      checkpoints.predictedReject,
      checkpoints.predictedUnsupported,
      checkpoints.evalMismatch,
      checkpoints.cursorMismatch,
      checkpoints.outcodeReject,
      checkpoints.nclipReject,
      checkpoints.zeroReject,
      checkpoints.skipReject,
      checkpoints.depthReject,
      checkpoints.evalTwoSided,
      checkpoints.evalDirect,
      checkpoints.evalQuadFirst,
      checkpoints.evalQuadSecond,
      checkpoints.evalFullQuad,
      checkpoints.badSource,
      checkpoints.badSourceRecord,
      checkpoints.badClassifier,
      checkpoints.badSubset,
      checkpoints.badFamily,
      checkpoints.badTables,
      checkpoints.firstBadTable,
      checkpoints.recordJoins,
      ordered,
      orderedFirst,
      payload.compared,
      census.packets,
      payload.mismatches,
      payload.first,
      payload.packet,
      payload.index,
      payload.expected,
      payload.actual,
      checkpoints.unsupportedPayload,
      checkpoints.directTri,
      checkpoints.quadFirst,
      checkpoints.quadSecond,
      checkpoints.badRecord,
      checkpoints.minRecord,
      checkpoints.maxRecord,
      checkpoints.firstRecord,
      checkpoints.badPacket,
      checkpoints.g4,
      checkpoints.gt4,
      checkpoints.g3,
      checkpoints.gt3,
      checkpoints.ft4,
      checkpoints.finals,
      census.packets,
      census.bytes,
      census.f3,
      census.g3,
      census.ft3,
      census.gt3,
      census.f4,
      census.g4,
      census.ft4,
      census.gt4,
      census.semi,
      census.raw,
      census.other,
      recipeCaptured,
      recipeJoin.candidatesCompared,
      recipe.candidateOrder.size(),
      recipeJoin.inputMismatch,
      recipe.faces.size(),
      census.packets,
      recipeJoin.orderMismatch,
      recipeJoin.payload.compared,
      recipe.faces.size(),
      recipeJoin.payload.mismatches,
      recipeJoin.first,
      corruptionApplicable,
      negative,
      census.first,
      result);
  // Groundwork is deliberately observation-only: a failed join is the diagnostic result, not a
  // reason to crash an otherwise valid generated render.  Promotion to an acceptance oracle will
  // make a nonempty mismatch fatal only after source/payload/bin joins are independently green.
}
} // namespace

void spyro_register_actor_chain_oracle() {
  const char *mode = cfg_str("PSXPORT_ACTOR_CHAIN_ORACLE");
  if (!mode || !*mode) {
    return;
  }
  const std::string_view selected(mode);
  if (selected != "payload" && selected != "ot" && selected != "prefix" &&
      selected != "prefix-build") {
    lucent::error("actorchainoracle",
                  "REFUSED: PSXPORT_ACTOR_CHAIN_ORACLE={} requires payload, ot, prefix, or "
                  "prefix-build",
                  mode);
    abort();
  }
  if (const char *identity = cfg_str("PSXPORT_NDIFF_IDENTITY"); identity && *identity) {
    lucent::error(
        "actorchainoracle",
        "REFUSED: PSXPORT_NDIFF_IDENTITY can overwrite the 0x8001F798 diagnostic override");
    abort();
  }
  if (const char *trace = cfg_str("PSXPORT_FNTRACE");
      trace && (std::string_view(trace).find("1F798") != std::string_view::npos ||
                std::string_view(trace).find("1f798") != std::string_view::npos)) {
    lucent::error("actorchainoracle",
                  "REFUSED: PSXPORT_FNTRACE targets the same 0x8001F798 override slot");
    abort();
  }
  lucent::info("actorchainoracle",
               "armed pass={} 0x8001F798; diagnostic only, native submission remains disabled",
               mode);
  auto overrideFn = actor_chain_oracle;
  if (selected == "ot") {
    overrideFn = actor_chain_ot_oracle;
  } else if (selected == "prefix") {
    overrideFn = actor_chain_prefix_oracle;
  } else if (selected == "prefix-build") {
    overrideFn = actor_chain_prefix_build_oracle;
  }
  psxport_recomp()->shard_set_override(0x8001F798u, overrideFn);
}

int spyro_actor_chain_oracle_selftest() {
  spyro::actor_draw_recipe::PrimitiveInput recipeInput{};
  SourceSnapshot recipeActual{};
  const bool recipeInputPositive =
      compare_recipe_input(recipeInput, 6, recipeActual) == std::string_view{"none"};
  recipeActual.words[7] = 1;
  const bool recipeLookaheadIgnored =
      compare_recipe_input(recipeInput, 6, recipeActual) == std::string_view{"none"};
  recipeActual.words[2] = 1;
  const bool recipeSourceCorruption =
      compare_recipe_input(recipeInput, 6, recipeActual) == std::string_view{"source_words"};
  recipeActual.words = {};
  recipeActual.depth[2] = 1;
  const bool recipeDepthCorruption =
      compare_recipe_input(recipeInput, 6, recipeActual) == std::string_view{"depth"};
  recipeActual.depth[2] = 0;
  recipeActual.fog = 1;
  const bool recipeFogCorruption =
      compare_recipe_input(recipeInput, 6, recipeActual) == std::string_view{"fog"};
  uint32_t primitiveReads = 0, primitiveBytes = 0;
  std::vector<uint32_t> primitiveWords;
  auto malformedPrimitiveRead = [&](uint32_t) {
    ++primitiveReads;
    return 0xffffffffu;
  };
  const bool malformedAddressRejected =
      !copy_primitive_words(malformedPrimitiveRead, 0x80001002u, primitiveWords, primitiveBytes) &&
      primitiveReads == 0;
  primitiveReads = 0;
  const bool oversizedPrimitiveRejected =
      !copy_primitive_words(malformedPrimitiveRead, 0x80001000u, primitiveWords, primitiveBytes) &&
      primitiveReads == 1;
  primitiveReads = 0;
  auto validPrimitiveRead = [&](uint32_t p) {
    ++primitiveReads;
    return p == 0x80001000u ? 8u : p;
  };
  const bool validPrimitiveCopied =
      copy_primitive_words(validPrimitiveRead, 0x80001000u, primitiveWords, primitiveBytes) &&
      primitiveReads == 3 && primitiveBytes == 8 && primitiveWords.size() == 2;
  PrefixCensus prefixPositive{};
  prefixPositive.setups = 2;
  prefixPositive.declaredVertices = 7;
  prefix_count_selector(prefixPositive, false, 0, 0);
  prefix_count_selector(prefixPositive, false, 1, 1);
  prefix_count_selector(prefixPositive, false, 2, 0);
  for (unsigned i = 0; i < 4; ++i) {
    prefix_count_selector(prefixPositive, false, 3, 1);
  }
  prefix_count_selector(prefixPositive, true, 0, 0);
  for (unsigned i = 0; i < 3; ++i) {
    prefix_count_selector(prefixPositive, true, 1, 1);
  }
  prefixPositive.colorSelected = 4;
  prefixPositive.high = prefixPositive.positiveBlend = prefixPositive.plain =
      prefixPositive.negativeBlend = 1;
  prefixPositive.matched = (uint64_t)prefixPositive.setups + prefixPositive.primaryDecisions +
                           prefixPositive.alternateDecisions + prefixPositive.colorSelected;
  PrefixCensus prefixBadSite = prefixPositive;
  --prefixBadSite.primarySites[3];
  PrefixCensus prefixBadColor = prefixPositive;
  prefixBadColor.colorMismatch = 1;
  PrefixCensus prefixBadMatched = prefixPositive;
  --prefixBadMatched.matched;
  std::array<uint32_t, 9> descriptor{};
  descriptor[5] = 0x80002000u;
  descriptor[6] = 0x80003000u;
  descriptor[7] = 0x80004000u;
  descriptor[8] = 0x80005000u;
  const uint32_t descriptorBase = 0x80001000u;
  auto descriptorRead = [&](uint32_t p) {
    return descriptor[(p - descriptorBase) / 4u];
  };
  auto ambiguousDescriptor = descriptor;
  ambiguousDescriptor[7] = ambiguousDescriptor[5];
  ambiguousDescriptor[8] = ambiguousDescriptor[6];
  auto ambiguousRead = [&](uint32_t p) {
    return ambiguousDescriptor[(p - descriptorBase) / 4u];
  };
  const bool colorClassification =
      expected_color_arm(0, 1024) == PrefixColorArm::High &&
      actual_color_arm(descriptorRead, descriptorBase, descriptor[5], descriptor[6], 0) ==
          PrefixColorArm::High &&
      actual_color_arm(descriptorRead, descriptorBase, descriptor[5], 0x80070DF4u, 4) ==
          PrefixColorArm::PositiveBlend &&
      actual_color_arm(descriptorRead, descriptorBase, descriptor[7], descriptor[8], 0) ==
          PrefixColorArm::Plain &&
      actual_color_arm(descriptorRead, descriptorBase, descriptor[7], 0x80070DF4u, 2) ==
          PrefixColorArm::NegativeBlend &&
      actual_color_arm(descriptorRead, descriptorBase, 0xDEADBEEFu, descriptor[8], 0) ==
          PrefixColorArm::Invalid &&
      actual_color_arm(
          ambiguousRead, descriptorBase, ambiguousDescriptor[5], ambiguousDescriptor[6], 0) ==
          PrefixColorArm::Invalid;
  const bool prefixResultAnswers = prefix_result(0, true) == PrefixResult::NoCorpus &&
                                   prefix_result(1, true) == PrefixResult::Pass &&
                                   prefix_result(1, false) == PrefixResult::Fail;
  bool ok = recipeInputPositive && recipeLookaheadIgnored && recipeSourceCorruption &&
            recipeDepthCorruption && recipeFogCorruption && malformedAddressRejected &&
            oversizedPrimitiveRejected && validPrimitiveCopied && prefix_complete(prefixPositive) &&
            !prefix_complete(prefixBadSite) && !prefix_complete(prefixBadColor) &&
            !prefix_complete(prefixBadMatched) && !prefix_complete(PrefixCensus{}) &&
            colorClassification && prefixResultAnswers;
  constexpr uint32_t base = 0x1000u;
  std::array<uint32_t, 49> w{};
  w[0] = 0x08001024u;
  w[1] = 0x3B000000u; // G4, semi+raw, 9 words
  w[9] = 0x0C000000u;
  w[10] = 0x3C000000u; // GT4, 13 words
  w[22] = 0x06000000u;
  w[23] = 0x30000000u; // G3, 7 words
  w[29] = 0x09000000u;
  w[30] = 0x34000000u; // GT3, 10 words
  w[39] = 0x09000000u;
  w[40] = 0x2C000000u; // FT4, 10 words
  PacketCensus good{};
  ok = ok &&
       parse_packets(
           base,
           base + 196u,
           [&](uint32_t p) {
             return w[(p - base) / 4u];
           },
           good) &&
       good.packets == 5u && good.g4 == 1u && good.gt4 == 1u && good.g3 == 1u && good.gt3 == 1u &&
       good.ft4 == 1u && good.semi == 1u && good.raw == 1u && good.bytes == 196u;
  std::vector<PacketKey> expected = good.entries, corruptAddress = expected,
                         corruptFamily = expected;
  corruptAddress[0].packet += 4u;
  corruptFamily[0].family = Family::GT4;
  ok = ok && compare_ordered(expected, good.entries) &&
       !compare_ordered(expected, corruptAddress) && !compare_ordered(expected, corruptFamily);
  PacketCensus bad{};
  w[1] = 0x7C000000u;
  ok = ok &&
       !parse_packets(
           base,
           base + 196u,
           [&](uint32_t p) {
             return w[(p - base) / 4u];
           },
           bad) &&
       bad.other == 1u;
  uint32_t reads = 0;
  SourceSnapshot snap{};
  auto read = [&](uint32_t p) {
    ++reads;
    return p;
  };
  const uint32_t lastRecord = kRecordBase + (kDurableRecords - 1u) * kRecordSize;
  ok = ok && capture_source(read, lastRecord, 0x801FFFD8u, 0x801FFFFCu, 1, 2, 3, 4, snap) &&
       reads == 11u && snap.words.front() == 0x801FFFD8u && snap.words.back() == 0x801FFFFCu &&
       snap.aux == 0x801FFFFCu;
  auto rejects_without_read = [&](uint32_t record, uint32_t source, uint32_t aux) {
    reads = 0;
    SourceSnapshot reject{};
    return !capture_source(read, record, source, aux, 1, 2, 3, 4, reject) && reads == 0u;
  };
  ok = ok && rejects_without_read(kRecordBase - 4u, 0x80001000u, 0x80002000u) &&
       rejects_without_read(kRecordBase, 0x801FFFDCu, 0x80002000u) &&
       rejects_without_read(kRecordBase, 0x80001002u, 0x80002000u) &&
       rejects_without_read(kRecordBase, 0x80001000u, 0x801FFFFEu);
  EpochState ep{};
  const bool bWithoutA = !epoch_subset(ep, 0x80001000u, kRecordBase);
  epoch_open(ep, 0x80001000u, kRecordBase);
  const bool bOnce = epoch_subset(ep, 0x80001000u, kRecordBase);
  const bool duplicateB = !epoch_subset(ep, 0x80001000u, kRecordBase);
  epoch_open(ep, 0x80001000u, kRecordBase);
  const bool changedSource = !epoch_subset(ep, 0x80001004u, kRecordBase);
  epoch_open(ep, 0x80001000u, kRecordBase);
  const bool changedRecord = !epoch_subset(ep, 0x80001000u, kRecordBase + kRecordSize);
  epoch_clear(ep);
  const bool familyAfterFailedA = !epoch_family(ep, 0x80001008u, kRecordBase, 0x80001008u);
  epoch_open(ep, 0x80001000u, kRecordBase);
  const bool wrongFamilyCursor = !epoch_family(ep, 0x8000100Cu, kRecordBase, 0x80001008u);
  epoch_open(ep, 0x80001000u, kRecordBase);
  const bool wrongFamilyRecord =
      !epoch_family(ep, 0x80001008u, kRecordBase + kRecordSize, 0x80001008u);
  epoch_open(ep, 0x80001000u, kRecordBase);
  const bool familyOnce = epoch_family(ep, 0x80001008u, kRecordBase, 0x80001008u);
  const bool staleFamily = !epoch_family(ep, 0x80001008u, kRecordBase, 0x80001008u);
  ok = ok && bWithoutA && bOnce && duplicateB && changedSource && changedRecord &&
       familyAfterFailedA && wrongFamilyCursor && wrongFamilyRecord && familyOnce && staleFamily;
  auto xy = [](int16_t x, int16_t y) {
    return uint32_t(uint16_t(x)) | (uint32_t(uint16_t(y)) << 16);
  };
  auto candidate = [&](uint32_t w0, uint32_t control) {
    SourceSnapshot s{};
    s.source = 0x80001000u;
    s.words[0] = w0 | control;
    s.xy = {xy(0, 0), xy(1, 0), xy(0, 1), xy(1, 1)};
    s.depth = {100, 100, 100, 100};
    return s;
  };
  auto directPositive = evaluate_candidate(candidate(0, 0));
  auto directNegativeInput = candidate(0, 0);
  std::swap(directNegativeInput.xy[1], directNegativeInput.xy[2]);
  const auto directNegative = evaluate_candidate(directNegativeInput);
  auto directTwoSidedInput = directNegativeInput;
  directTwoSidedInput.words[0] |= 1u;
  const auto directTwoSided = evaluate_candidate(directTwoSidedInput);
  auto directZeroInput = candidate(0, 0);
  directZeroInput.xy[2] = xy(2, 0);
  const auto directZero = evaluate_candidate(directZeroInput);
  const auto directSkip = evaluate_candidate(candidate(0, 8u));
  const auto directTexturedSkip = evaluate_candidate(candidate(0, 10u));
  const auto quadSkip = evaluate_candidate(candidate(0x80000000u, 8u));
  const auto quadTexturedSkip = evaluate_candidate(candidate(0x80000000u, 10u));
  auto outcodeInput = candidate(0, 0);
  outcodeInput.shift = ~0u;
  outcodeInput.status = {1, 1, 1};
  const auto outcode = evaluate_candidate(outcodeInput);
  auto depthInput = candidate(0, 0);
  depthInput.depthOrigin = 1000;
  const auto depthReject = evaluate_candidate(depthInput);
  auto ft4Input = candidate(0x80000000u, 4u);
  const auto ft4Reject = evaluate_candidate(ft4Input);
  using EO = CheckpointCensus::Outcome;
  using ER = CheckpointCensus::Origin;
  const bool evaluatorBranches =
      directPositive.outcome == EO::G3 && directPositive.origin == ER::Direct &&
      directPositive.next == 0x80001008u && directNegative.outcome == EO::Reject &&
      directTwoSided.outcome == EO::G3 && directZero.outcome == EO::Reject &&
      std::string_view(directSkip.reason) == "skip" && directSkip.next == 0x80001008u &&
      directTexturedSkip.next == 0x80001014u && std::string_view(quadSkip.reason) == "skip" &&
      quadSkip.next == 0x8000100Cu && quadTexturedSkip.next == 0x80001018u &&
      std::string_view(outcode.reason) == "outcode" &&
      std::string_view(depthReject.reason) == "depth" && ft4Reject.outcome == EO::Unsupported &&
      ft4Reject.next == 0x80001014u;
  using QD = spyro::actor_draw_recipe::QuadDecision;
  const bool quadTable = spyro::actor_draw_recipe::classifyQuad(-1, 1, false) == QD::Full &&
                         spyro::actor_draw_recipe::classifyQuad(-1, -1, false) == QD::First &&
                         spyro::actor_draw_recipe::classifyQuad(1, 1, false) == QD::Second &&
                         spyro::actor_draw_recipe::classifyQuad(1, -1, false) == QD::Reject &&
                         spyro::actor_draw_recipe::classifyQuad(1, -1, true) == QD::Full &&
                         spyro::actor_draw_recipe::classifyQuad(-1, 1, true) == QD::Full &&
                         spyro::actor_draw_recipe::classifyQuad(0, 1, false) == QD::Second &&
                         spyro::actor_draw_recipe::classifyQuad(0, 0, false) == QD::Reject;
  const bool allRejectAccounting =
      payload_accounting(4, 4, 0, 4, 0, 0) &&
      std::string_view(payload_result(4, true, false, false)) == "PASS";
  const bool noCorpusResult =
      std::string_view(payload_result(0, true, false, false)) == "NO_CORPUS";
  const bool corruptionSemantics =
      std::string_view(payload_result(4, true, true, true)) == "PASS" &&
      std::string_view(payload_result(4, true, true, false)) == "FAIL";
  ok = ok && evaluatorBranches && quadTable && allRejectAccounting && noCorpusResult &&
       corruptionSemantics;
  CheckpointCensus::Expected pe{
      base,
      Family::G3,
      {0x06000000u, 0x30112233u, 0x00020001u, 0x00445566u, 0x00040003u, 0x00778899u, 0x00060005u}};
  std::vector<uint32_t> actual = pe.words;
  actual[0] |= 0x00123456u;
  auto payloadRead = [&](uint32_t p) {
    return actual[(p - base) / 4u];
  };
  const auto payloadGood = compare_payloads({pe}, base, base + 28u, payloadRead);
  actual[2] ^= 1u;
  const auto payloadBadXy = compare_payloads({pe}, base, base + 28u, payloadRead);
  actual[2] ^= 1u;
  actual[1] ^= 1u;
  const auto payloadBadColor = compare_payloads({pe}, base, base + 28u, payloadRead);
  ok = ok && payloadGood.compared == 1 && payloadGood.mismatches == 0 &&
       payloadBadXy.mismatches == 1 && std::string_view(payloadBadXy.first) == "xy" &&
       payloadBadColor.mismatches == 1 &&
       std::string_view(payloadBadColor.first) == "command_color";
  std::vector<OtEntry> otExpected{{0x80001000u, 3}, {0x80001020u, 3}, {0x80001040u, 7}},
      otActual = otExpected;
  uint32_t otFirst = 0;
  const char *otField = "none";
  const bool otGood = compare_ot(otExpected, otActual, otFirst, otField);
  otActual[2].bin = 8;
  const bool otBadBin =
      !compare_ot(otExpected, otActual, otFirst, otField) && std::string_view(otField) == "bin";
  otActual = otExpected;
  std::swap(otActual[0].packet, otActual[1].packet);
  const bool otBadLink =
      !compare_ot(otExpected, otActual, otFirst, otField) && std::string_view(otField) == "fifo";
  constexpr uint32_t ob = 0x80010000u, p1 = 0x80020000u, p2 = 0x80020020u, p3 = 0x80020040u;
  const std::vector<OtEntry> fixtureExpected{{p1, 1}, {p2, 2}, {p3, 2}};
  auto runOt = [&](std::vector<WordWrite> memoryEntries) {
    OtCensus x{};
    x.haveSource = true;
    x.source.localOt = ob;
    x.expected = fixtureExpected;
    WordOverlay memory(
        [](uint32_t) {
          return 0u;
        },
        memoryEntries);
    snapshot_ot_read(x, memory);
    return x;
  };
  const std::vector<WordWrite> validMem{
      {ob + 8, p1}, {ob + 12, p1}, {ob + 16, p3}, {ob + 20, p2}, {p2, p3 & 0x00FFFFFFu}};
  const auto walkGood = runOt(validMem);
  const bool walkPositive =
      walkGood.binsScanned == 288 && walkGood.actual.size() == 3 && walkGood.emptyAppend == 2 &&
      walkGood.nonemptyAppend == 1 && walkGood.cycles == 0 && walkGood.duplicates == 0 &&
      walkGood.outOfRange == 0 && compare_ot(fixtureExpected, walkGood.actual, otFirst, otField);
  auto orderMem = validMem;
  orderMem[2].value = p2;
  orderMem[3].value = p3;
  orderMem[4] = {p3, p2 & 0x00FFFFFFu};
  const auto walkOrder = runOt(orderMem);
  const bool walkBadOrder = !compare_ot(fixtureExpected, walkOrder.actual, otFirst, otField) &&
                            std::string_view(otField) == "fifo";
  auto cycleMem = validMem;
  cycleMem[4].value = p2 & 0x00FFFFFFu;
  const auto walkCycle = runOt(cycleMem);
  const bool walkHasCycle = walkCycle.cycles > 0;
  auto duplicateMem = validMem;
  duplicateMem.push_back({ob + 24, p1});
  duplicateMem.push_back({ob + 28, p1});
  const auto walkDuplicate = runOt(duplicateMem);
  const bool walkHasDuplicate = walkDuplicate.duplicates > 0;
  auto rangeMem = validMem;
  rangeMem.push_back({ob + 24, 0x80200000u});
  rangeMem.push_back({ob + 28, 0x80200000u});
  const auto walkRange = runOt(rangeMem);
  const bool walkOutOfRange = walkRange.outOfRange > 0;
  auto oneSideMem = validMem;
  oneSideMem.push_back({ob + 24, p1});
  const auto walkOneSide = runOt(oneSideMem);
  const bool walkOneSided = walkOneSide.outOfRange > 0;
  constexpr uint32_t gb = 0x80030000u, lmin = 0x8006FCF4u, lmax = lmin + 320u, oldh = 0x80021000u,
                     oldt = 0x80021020u, p4 = 0x80020060u;
  std::vector<WordWrite> spliceMemoryEntries{{gb, oldh},
                                             {gb + 4, oldt},
                                             {oldh, 0x0CABCDEFu},
                                             {lmax, p2},
                                             {lmax + 4, p1},
                                             {p1, p2 & 0xFFFFFFu},
                                             {lmax - 8, p3},
                                             {lmax - 4, p3},
                                             {lmin, p4},
                                             {lmin + 4, p4}};
  WordOverlay spliceMemory(
      [](uint32_t) {
        return 0u;
      },
      spliceMemoryEntries);
  OtCensus spliceCount{};
  std::vector<WordWrite> spliceExpected;
  const bool spliceSim =
      simulate_global(spliceMemory, lmin, lmax, 0, 0, gb, spliceExpected, spliceCount);
  WordOverlay splicePost(spliceMemory, spliceExpected);
  const bool splicePositive =
      spliceSim && spliceCount.groups >= 2 && spliceCount.globalPreexisting > 0 &&
      spliceCount.globalEmpty > 0 && spliceCount.baseBounce > 0 && spliceCount.tagPatches >= 2 &&
      spliceCount.localClears == 6 && compare_global_words(spliceExpected, splicePost) == 0 &&
      splicePost.read32(oldh) == ((0x0CABCDEFu & 0xFF000000u) | (p1 & 0xFFFFFFu)) &&
      splicePost.read32(gb) == p3 && splicePost.read32(gb + 4) == oldt &&
      splicePost.read32(gb + 8) == p4 && splicePost.read32(gb + 12) == p4 &&
      splicePost.read32(lmax) == 0 && splicePost.read32(lmax + 4) == 0;
  auto corruptExpected = spliceExpected;
  corruptExpected.front().value ^= 1u;
  const bool spliceCorrupt = compare_global_words(corruptExpected, splicePost) > 0;
  const uint32_t untouchedLocal = lmax - 16u;
  const bool hasUntouchedLocal =
      std::any_of(spliceExpected.begin(), spliceExpected.end(), [&](const WordWrite &w) {
        return w.addr == untouchedLocal && w.value == 0;
      });
  const bool corruptGlobalTail = compare_global_words(spliceExpected, [&](uint32_t p) {
                                   return splicePost(p) ^ (p == gb + 4u ? 1u : 0u);
                                 }) > 0;
  const bool corruptUntouchedLocal =
      hasUntouchedLocal && compare_global_words(spliceExpected, [&](uint32_t p) {
                             return splicePost(p) ^ (p == untouchedLocal ? 1u : 0u);
                           }) > 0;
  OtCensus noLocalCount{};
  std::vector<WordWrite> noLocalExpected;
  const bool noLocal =
      simulate_global(spliceMemory, lmax, lmin, 0, 0, gb, noLocalExpected, noLocalCount) &&
      noLocalCount.noLocal == 1 && noLocalExpected.empty();
  ok = ok && otGood && otBadBin && otBadLink && walkPositive && walkBadOrder && walkHasCycle &&
       walkHasDuplicate && walkOutOfRange && walkOneSided && splicePositive && spliceCorrupt &&
       corruptGlobalTail && corruptUntouchedLocal && noLocal;
  lucent::info(
      "selftest",
      "{}(actorchainrecipe): packets={} bytes={} G4={} GT4={} G3={} GT3={} FT4={} semi={} raw={} "
      "corrupt_address=1 corrupt_family=1 corrupt_command={} source_valid_reads=11 "
      "source_invalid_cases=4 source_invalid_reads=0 epoch_negatives[b_without_a={} duplicate_b={} "
      "changed_source={} changed_record={} family_after_failed={} wrong_cursor={} "
      "wrong_family_record={} stale_family={}] evaluator[branches={} quad_table={} all_reject={} "
      "no_corpus={} corruption_semantics={}] payload_good={} corrupt_xy={} "
      "corrupt_command_color={} ot_walk[positive={} bins={} empty={} append={} corrupt_bin={} "
      "corrupt_link={} cycle={} duplicate={} out_of_range={} one_sided={}] global[positive={} "
      "groups={} preexisting={} empty={} bounce={} patches={} clears={} corrupt={} "
      "corrupt_untouched_global_tail={} corrupt_untouched_local={} no_local={}] "
      "prefix[positive={} site_corrupt_rejected={} color_corrupt_rejected={} "
      "matched_corrupt_rejected={} no_corpus_rejected={} classification_both_answers={} "
      "ambiguous_rejected={} result_answers={}] recipe_input[positive={} lookahead_ignored={} "
      "source_corrupt={} depth_corrupt={} fog_corrupt={}]",
      ok ? "PASS" : "FAIL",
      good.packets,
      good.bytes,
      good.g4,
      good.gt4,
      good.g3,
      good.gt3,
      good.ft4,
      good.semi,
      good.raw,
      bad.other == 1u,
      bWithoutA,
      duplicateB,
      changedSource,
      changedRecord,
      familyAfterFailedA,
      wrongFamilyCursor,
      wrongFamilyRecord,
      staleFamily,
      evaluatorBranches,
      quadTable,
      allRejectAccounting,
      noCorpusResult,
      corruptionSemantics,
      payloadGood.mismatches == 0,
      payloadBadXy.mismatches == 1,
      payloadBadColor.mismatches == 1,
      walkPositive,
      walkGood.binsScanned,
      walkGood.emptyAppend,
      walkGood.nonemptyAppend,
      otBadBin,
      walkBadOrder,
      walkHasCycle,
      walkHasDuplicate,
      walkOutOfRange,
      walkOneSided,
      splicePositive,
      spliceCount.groups,
      spliceCount.globalPreexisting,
      spliceCount.globalEmpty,
      spliceCount.baseBounce,
      spliceCount.tagPatches,
      spliceCount.localClears,
      spliceCorrupt,
      corruptGlobalTail,
      corruptUntouchedLocal,
      noLocal,
      prefix_complete(prefixPositive),
      !prefix_complete(prefixBadSite),
      !prefix_complete(prefixBadColor),
      !prefix_complete(prefixBadMatched),
      !prefix_complete(PrefixCensus{}),
      colorClassification,
      actual_color_arm(
          ambiguousRead, descriptorBase, ambiguousDescriptor[5], ambiguousDescriptor[6], 0) ==
          PrefixColorArm::Invalid,
      prefixResultAnswers,
      recipeInputPositive,
      recipeLookaheadIgnored,
      recipeSourceCorruption,
      recipeDepthCorruption,
      recipeFogCorruption);
  return ok ? 0 : 1;
}
