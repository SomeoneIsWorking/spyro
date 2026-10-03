#include "interp_census.h"

#include "core.h"
#include "cyclorama_mask_recipe.h"
#include "cyclorama_portal_mesh_recipe.h"
#include "game.h"
#include "render_queue.h"
#include "spyro_context.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <lucent/log.h>
#include <span>

namespace spyro::interp_census {
namespace {

// The publishers that own a draw's category, by the guest address the producer is dispatched at.
// Every first-party world producer publishes inside a `ProducerScope`, which stamps that address as
// the item's painter object, so this reads the item's own identity rather than keeping a second
// list beside the queue. An item with no painter object is a guest packet through the ordinary OT
// walk; it is classified by its layer, which is the only identity such an item has.
constexpr uint32_t kActorDraw = 0x8001f798u;
constexpr uint32_t kSecondaryActor = 0x80020f34u;
// The key the paired-actor renderer stamps on its items, taken from the constant in
// `fx_paired_actor.cpp` rather than retyped. This one was typed wrong at first (`0x80023acu`), and
// the failure was invisible in the product: an unknown publisher falls through to the world bucket
// rather than being refused, so every paired-actor item was counted as world geometry — a confident
// answer about the wrong category. The partition test is what makes that typo impossible now.
constexpr uint32_t kPairedActor = 0x80023ac4u;

constexpr uint32_t kWorldEnvironment = 0x800258f0u;
constexpr uint32_t kTerrain = 0x8004eba8u;
constexpr uint32_t kShadedQueue = 0x80022a2cu;
// The two cyclorama producers already publish their key from their own header, so the table reads
// that constant rather than a second spelling of it.
constexpr uint32_t kCycloramaMask = spyro::cyclorama_mask_recipe::kProducerKey;
constexpr uint32_t kCycloramaPortal = spyro::cyclorama_portal_mesh::kProducerKey;

constexpr uint32_t kFieldParticles = 0x800573c8u;
constexpr uint32_t kGlow = 0x800580f4u;
constexpr uint32_t kSparkle = 0x800584c4u;
constexpr uint32_t kFlame = 0x80058d64u;
constexpr uint32_t kTracers = 0x800189f0u;
constexpr uint32_t kFieldShadow = 0x80059a48u;
constexpr uint32_t kMobyShadow = 0x80059f8cu;
constexpr uint32_t kDragonBurst = 0x80058864u;

struct Publisher {
  uint32_t key;
  Category category;
};

// The whole table, so the partition is one list rather than a chain of comparisons spread across a
// header and a test.
constexpr std::array<Publisher, 16> kPublishers{{
    {kActorDraw, Category::Actors},
    {kSecondaryActor, Category::Actors},
    {kPairedActor, Category::Actors},
    {kWorldEnvironment, Category::World},
    {kTerrain, Category::World},
    {kShadedQueue, Category::World},
    {kCycloramaMask, Category::World},
    {kCycloramaPortal, Category::World},
    {kFieldParticles, Category::Particles},
    {kGlow, Category::Particles},
    {kSparkle, Category::Particles},
    {kFlame, Category::Particles},
    {kTracers, Category::Particles},
    {kFieldShadow, Category::Particles},
    {kMobyShadow, Category::Particles},
    {kDragonBurst, Category::Particles},
}};

void countQueue(std::span<const RqItem> items, std::array<uint32_t, kCategoryCount> &into) {
  for (const auto &item : items) {
    ++into[(size_t)categoryOf(item)];
  }
}

} // namespace

const char *name(Category category) {
  switch (category) {
  case Category::Camera:
    return "camera";
  case Category::Actors:
    return "actors";
  case Category::World:
    return "world";
  case Category::Particles:
    return "particles";
  case Category::Hud:
    return "hud";
  case Category::Count:
    break;
  }
  return "invalid";
}

const char *name(Reason reason) {
  switch (reason) {
  case Reason::None:
    return "none";
  case Reason::NoTemporalSource:
    return "no-temporal-source";
  case Reason::IntervalNotAdmitted:
    return "interval-not-admitted";
  case Reason::NoPredecessor:
    return "no-predecessor";
  case Reason::Incompatible:
    return "incompatible";
  case Reason::SamplerRefused:
    return "sampler-refused";
  case Reason::CameraMismatch:
    return "camera-mismatch";
  case Reason::Count:
    break;
  }
  return "invalid";
}

bool hasLayer(Category category) {
  // Which of the five categories a temporal source reconstructs. The particle and effect producers
  // do not: they are whole-frame lists the guest rebuilds each update with no retained corpus, so
  // an in-between present draws them at the current update's values. That is the coverage gap the
  // report names, and it is a statement about this list, not an inference from a zero.
  switch (category) {
  case Category::Camera:
  case Category::Actors:
  case Category::World:
  case Category::Hud:
    return true;
  case Category::Particles:
  case Category::Count:
    break;
  }
  return false;
}

Category categoryOf(const RqItem &item) {
  if (item.layer == RQ_HUD) {
    return Category::Hud;
  }
  for (const auto &publisher : kPublishers) {
    if (item.painter_object == publisher.key) {
      return publisher.category;
    }
  }
  // A world item from a publisher this table does not name, or an ordinary guest packet with no
  // painter object at all, is still world geometry rather than an effect: it is depth-sorted
  // against the terrain, which is what the world category means here.
  return Category::World;
}

void Totals::add(const Frame &frame) {
  ++logicFrames;
  for (size_t c = 0; c < kCategoryCount; ++c) {
    total.captured[c] += frame.captured[c];
    total.reconstructed[c] += frame.reconstructed[c];
    for (size_t r = 0; r < kReasonCount; ++r) {
      total.reasons[c][r] += frame.reasons[c][r];
    }
    total.layers[c].records += frame.layers[c].records;
    total.layers[c].interpolated += frame.layers[c].interpolated;
    total.layers[c].noPredecessor += frame.layers[c].noPredecessor;
    total.layers[c].incompatible += frame.layers[c].incompatible;
    total.layers[c].samplerRefused += frame.layers[c].samplerRefused;
    if (frame.layers[c].ran) {
      total.layers[c].ran = true;
    }
  }
}

void Census::beginLogicFrame(Core &core) {
  current_ = {};
  const RenderQueue *queue = core.game != nullptr ? &core.game->rq : nullptr;
  if (queue == nullptr) {
    return;
  }
  const std::span<const RqItem> items(queue->items, (size_t)std::max(0, queue->n));
  countQueue(items, current_.captured);
}

void Census::reconstruct(Core &core, float t) {
  if (!isInBetween(t)) {
    return;
  }
  ++totals_.presentedInterpolated;
  const RenderQueue *redirect = core.game != nullptr ? core.game->rqRedirect : nullptr;
  if (redirect == nullptr) {
    return;
  }
  const std::span<const RqItem> items(redirect->items, (size_t)std::max(0, redirect->n));
  countQueue(items, current_.reconstructed);
}

void Census::recordLayer(Category category, const LayerCensus &layer) {
  const size_t c = (size_t)category;
  current_.layers[c].records += layer.records;
  current_.layers[c].interpolated += layer.interpolated;
  current_.layers[c].noPredecessor += layer.noPredecessor;
  current_.layers[c].incompatible += layer.incompatible;
  current_.layers[c].samplerRefused += layer.samplerRefused;
  current_.layers[c].ran = current_.layers[c].ran || layer.ran;
}

void Census::endLogicFrame() {
  // One logic frame closes here: both of its presents have run, so the captured denominator, the
  // reconstructed items and every layer's record census are all in. The real-present count is
  // incremented from this same event so the two denominators cannot disagree by a frame.
  ++totals_.presentedReal;
  // A category with no layer cannot explain its own shortfall from a rule, because no rule ran. The
  // shortfall IS the reason, read from the denominator rather than asserted, so a category that
  // grows one costs nothing to describe correctly.
  for (size_t c = 0; c < kCategoryCount; ++c) {
    if (!hasLayer((Category)c)) {
      current_.reasons[c][(size_t)Reason::NoTemporalSource] += current_.latestOnly((Category)c);
    }
  }
  totals_.add(current_);
}

void Census::cameraSample(bool compatible, Reason reason) {
  // One camera sample per in-between present. The captured unit is one: the presenter draws the
  // scene through exactly one camera, and an in-between present either sits between the two
  // captured cameras or does not.
  ++current_.captured[(size_t)Category::Camera];
  if (compatible) {
    ++current_.reconstructed[(size_t)Category::Camera];
  } else {
    current_.reasons[(size_t)Category::Camera][(size_t)reason] += 1u;
  }
}

void Census::report(const char *when) const {
  lucent::info("interpcensus",
               "totals when={} logic_frames={} presented_real={} presented_interpolated={}",
               when,
               totals_.logicFrames,
               totals_.presentedReal,
               totals_.presentedInterpolated);
  for (size_t c = 0; c < kCategoryCount; ++c) {
    const auto category = (Category)c;
    if (totals_.total.captured[c] == 0u && category != Category::Camera) {
      lucent::info("interpcensus",
                   "  {} draws=0 — this category never drew, which is evidence of neither a rule "
                   "nor its absence",
                   name(category));
      continue;
    }
    lucent::info("interpcensus",
                 "  {} draws={} interpolated={} latest_only={}",
                 name(category),
                 totals_.total.captured[c],
                 totals_.total.reconstructed[c],
                 totals_.total.latestOnly(category));
    for (size_t r = 1; r < kReasonCount; ++r) {
      const uint32_t count = totals_.total.reasons[c][r];
      if (count != 0u) {
        lucent::info("interpcensus", "      item-reason={} draws={}", name((Reason)r), count);
      }
    }
    const auto &layer = totals_.total.layers[c];
    if (layer.records != 0u) {
      lucent::info("interpcensus",
                   "      records={} interpolated={} no_predecessor={} incompatible={} "
                   "refused={} ran={}",
                   layer.records,
                   layer.interpolated,
                   layer.noPredecessor,
                   layer.incompatible,
                   layer.samplerRefused,
                   layer.ran ? 1 : 0);
    }
  }
}

bool replCommand(Core &core, const char *command, const char *line) {
  if (std::strcmp(command, "interpcensus") != 0) {
    return false;
  }
  Census &owner = spyro::context(core).interpCensus;
  if (line != nullptr && std::strstr(line, "reset") != nullptr) {
    owner.reset();
    lucent::info("interpcensus", "counters cleared");
    return true;
  }
  owner.report("repl");
  return true;
}

} // namespace spyro::interp_census