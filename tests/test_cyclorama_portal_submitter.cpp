#include "core.h"
#include "cyclorama_portal_submitter.h"
#include "game.h"
#include "render_queue.h"
#include "testutil.h"

#include <array>
#include <memory>

namespace {

using spyro::cyclorama_portal_mesh::Face;
using spyro::cyclorama_portal_mesh::PortalFrame;
using spyro::cyclorama_portal_mesh::Recipe;
using spyro::cyclorama_portal_mesh::Status;
using spyro::cyclorama_portal_submitter::Draw;

struct Harness {
  std::unique_ptr<Core> core = std::make_unique<Core>();
  std::unique_ptr<Game> game = std::make_unique<Game>();

  Harness() {
    core->game = game.get();
  }
};

PortalFrame frame(uint32_t ordinal, Status status = Status::Ready) {
  PortalFrame out{};
  out.status = status;
  out.portalOrdinal = ordinal;
  out.otBin = (uint16_t)(100u + ordinal);
  out.maskVisible = true;
  return out;
}

Recipe recipe(uint32_t rgb = 0x00332211u) {
  Recipe out{};
  out.status = Status::Ready;
  Face face{};
  face.sourceOrdinal = 7;
  face.gouraud = true;
  for (size_t i = 0; i < face.vertices.size(); ++i) {
    face.vertices[i].sx = (int16_t)(10 + i * 10);
    face.vertices[i].sy = (int16_t)(20 + i * 10);
    face.vertices[i].screenX = (float)face.vertices[i].sx;
    face.vertices[i].screenY = (float)face.vertices[i].sy;
    face.vertices[i].viewZ = 100.0f + (float)i;
    face.vertices[i].rgb = rgb + (uint32_t)i;
  }
  out.faces.push_back(face);
  return out;
}

void test_batch_admission_and_submission_preserve_family_and_order() {
  Harness h;
  PortalFrame first = frame(0);
  PortalFrame second = frame(1);
  Recipe firstRecipe = recipe();
  Recipe secondRecipe = recipe(0x00665544u);
  const std::array<Draw, 2> draws{{{&first, &firstRecipe}, {&second, &secondRecipe}}};

  const auto plan = spyro::cyclorama_portal_submitter::prepare(
      h.core.get(), h.game->rq, spyro::cyclorama_portal_mesh::kProducerKey, draws);
  CHECK(plan.status == spyro::cyclorama_portal_submitter::Status::Ready);
  CHECK_EQ(plan.faces.size(), 2u);
  CHECK(plan.faces[0].replay.authored());
  CHECK(plan.faces[0].replay.key.ot_bin == first.otBin);
  CHECK(plan.faces[1].replay.key.ot_bin == second.otBin);
  CHECK(plan.faces[0].replay.key.link_ordinal > plan.faces[1].replay.key.link_ordinal);
  CHECK_EQ(h.game->rq.n, 0);

  spyro::cyclorama_portal_submitter::submit(h.core.get(), h.game->rq, draws, plan);
  CHECK_EQ(h.game->rq.n, 2);
  CHECK_EQ(h.game->rq.items[0].painter_object, spyro::cyclorama_portal_mesh::kProducerKey);
  CHECK_EQ(h.game->rq.items[1].painter_object, spyro::cyclorama_portal_mesh::kProducerKey);
  CHECK_EQ(h.game->rq.items[0].nv, 3);
  CHECK_EQ(h.game->rq.items[0].mode, 3);
  CHECK(h.game->rq.items[0].painter_replay.authored());
}

void test_near_family_uses_its_guest_producer_key() {
  Harness h;
  PortalFrame near = frame(0, Status::NearFamilyUnsupported);
  Recipe nearRecipe = recipe();
  const std::array<Draw, 1> draws{{{&near, &nearRecipe}}};
  const auto plan = spyro::cyclorama_portal_submitter::prepare(
      h.core.get(), h.game->rq, spyro::cyclorama_portal_mesh::kNearProducerKey, draws);
  CHECK(plan.status == spyro::cyclorama_portal_submitter::Status::Ready);
  CHECK_EQ(plan.producerKey, spyro::cyclorama_portal_mesh::kNearProducerKey);
}

void test_visible_mesh_without_mask_is_refused_atomically() {
  Harness h;
  PortalFrame apertureMissing = frame(0);
  apertureMissing.maskVisible = false;
  Recipe portalRecipe = recipe();
  const std::array<Draw, 1> draws{{{&apertureMissing, &portalRecipe}}};
  const auto plan = spyro::cyclorama_portal_submitter::prepare(
      h.core.get(), h.game->rq, spyro::cyclorama_portal_mesh::kProducerKey, draws);
  CHECK(plan.status == spyro::cyclorama_portal_submitter::Status::InvalidAperture);
  CHECK(plan.faces.empty());
  CHECK_EQ(h.game->rq.n, 0);
}

void test_capacity_refusal_is_atomic() {
  Harness h;
  h.game->rq.consumed = 0;
  h.game->rq.n = RQ_MAX;
  PortalFrame portal = frame(0);
  Recipe portalRecipe = recipe();
  const std::array<Draw, 1> draws{{{&portal, &portalRecipe}}};
  const auto plan = spyro::cyclorama_portal_submitter::prepare(
      h.core.get(), h.game->rq, spyro::cyclorama_portal_mesh::kProducerKey, draws);
  CHECK(plan.status == spyro::cyclorama_portal_submitter::Status::QueueCapacityExceeded);
  CHECK_EQ(h.game->rq.n, RQ_MAX);
}

} // namespace

int main() {
  RUN(batch_admission_and_submission_preserve_family_and_order);
  RUN(near_family_uses_its_guest_producer_key);
  RUN(visible_mesh_without_mask_is_refused_atomically);
  RUN(capacity_refusal_is_atomic);
  return pt_summary();
}
