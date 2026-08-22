#include "actor_scene_oracle.h"

#include "actor_recipe_capture.h"
#include "cfg.h"
#include "core.h"

#include <cstdint>
#include <lucent/log.h>
#include <vector>

namespace spyro::actor_scene_oracle {
namespace {

constexpr uint32_t kBuildLists = 0x800521C0u;
constexpr uint32_t kPrepareActors = 0x8001F158u;

} // namespace

Status capture(Core *c, std::vector<actor_recipe_capture::Record> &records) {
  records.clear();
  if (!cfg_on("PSXPORT_ACTOR_SCENE_ORACLE")) {
    return Status::Disabled;
  }
  rec_dispatch(c, kBuildLists);
  rec_dispatch(c, kPrepareActors);
  if (!actor_recipe_capture::capture_records(c, records)) {
    lucent::error("actorsceneoracle", "retail preparation produced no capturable record corpus");
    return Status::Refused;
  }
  lucent::info("actorsceneoracle", "retail preparation produced {} record(s)", records.size());
  for (uint32_t index = 0; index < records.size(); ++index) {
    const auto &input = records[index].input;
    const uint32_t moby = c->mem_r32(actor_recipe_capture::kRecordBase +
                                     index * actor_recipe_capture::kRecordSize + 52u);
    lucent::info("actorsceneoracle",
                 "record={} moby=0x{:08X} view=({},{},{}) vertices={} header=0x{:08X} "
                 "matrix={:08X},{:08X},{:08X},{:08X},{:08X}",
                 index,
                 moby,
                 input.tx,
                 input.ty,
                 input.tz,
                 input.vertexCount,
                 input.header,
                 input.matrixWords[0],
                 input.matrixWords[1],
                 input.matrixWords[2],
                 input.matrixWords[3],
                 input.matrixWords[4]);
  }
  return Status::Captured;
}

bool compare(std::span<const actor_recipe_capture::Record> retail,
             std::span<const actor_recipe_capture::Record> semantic) {
  if (retail.size() != semantic.size()) {
    lucent::error("actorsceneoracle",
                  "record count mismatch retail={} semantic={}",
                  retail.size(),
                  semantic.size());
    return false;
  }
  for (uint32_t index = 0; index < retail.size(); ++index) {
    const auto &a = retail[index];
    const auto &b = semantic[index];
    const auto outputs = actor_prefix::compareOutputs(a.expected, b.expected);
    const bool metadataMatches = a.descriptor == b.descriptor && a.command == b.command &&
                                 a.input.header == b.input.header && a.input.tx == b.input.tx &&
                                 a.input.ty == b.input.ty && a.input.tz == b.input.tz &&
                                 a.input.matrixWords == b.input.matrixWords &&
                                 a.input.vertexCount == b.input.vertexCount;
    if (!metadataMatches || outputs.mismatches != 0) {
      lucent::error("actorsceneoracle",
                    "record={} mismatch metadata={} output_mismatches={} first={}",
                    index,
                    metadataMatches ? "match" : "DIFF",
                    outputs.mismatches,
                    outputs.firstField);
      return false;
    }
  }
  lucent::info(
      "actorsceneoracle", "PASS {} semantic record(s) match retail preparation", retail.size());
  return true;
}

} // namespace spyro::actor_scene_oracle
