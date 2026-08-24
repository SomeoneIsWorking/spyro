#include "world_scene_capture.h"

#include "cfg.h"
#include "core.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <lucent/log.h>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace spyro::world_scene_capture {
namespace {

constexpr uint32_t kPoolPointer = 0x800757B0u;
constexpr uint32_t kOtPointer = 0x80075820u;
constexpr uint32_t kOtBins = 0x800u;
constexpr uint32_t kMaxChain = 65536u;

struct CapturedRecord {
  world_scene_oracle::Record record{};
  uint32_t paintGroup = 0;
  uint32_t paintSuborder = 0;
};

struct HighProjectionSnapshot {
  uint32_t sector = 0;
  uint32_t cacheAddress = 0;
  uint32_t packedVxy = 0;
  uint32_t packedVz = 0;
  psxport::native_projection::FixedAffine affine{};
  psxport::native_projection::ProjectionParams projection{};
  psxport::native_projection::NativeProjectedVertex projected{};
};

struct State {
  bool armed = false;
  bool complete = false;
  bool valid = true;
  const char *refusal = "none";
  uint32_t poolBegin = 0;
  uint32_t poolEnd = 0;
  uint32_t nextPaintGroup = 0;
  std::array<uint32_t, 6> phaseLinks{};
  uint32_t adaptiveChildren = 0;
  uint32_t adaptiveDeferred = 0;
  uint32_t adaptiveReplacements = 0;
  std::unordered_set<uint32_t> linkedPackets;
  std::unordered_set<uint32_t> adaptivePackets;
  std::unordered_map<uint32_t, uint32_t> paintGroups;
  std::unordered_map<uint32_t, uint32_t> sources;
  std::unordered_map<uint32_t, std::array<uint32_t, 5>> highFaceWords;
  std::vector<HighProjectionSnapshot> highProjections;
  std::vector<world_scene_oracle::Record> records;
};

std::mutex s_mutex;
std::unordered_map<Core *, State> s_states;

bool ramSpan(uint32_t address, uint32_t bytes) {
  const uint32_t physical = address & 0x1fffffffu;
  return bytes <= 0x200000u && physical <= 0x200000u - bytes;
}

uint32_t kseg(uint32_t address) {
  return 0x80000000u | (address & 0x1fffffu);
}

bool inPool(const State &state, uint32_t packet) {
  return packet >= state.poolBegin && packet < state.poolEnd;
}

bool refuse(State &state, const char *reason) {
  if (state.refusal == std::string_view("none")) {
    state.refusal = reason;
  }
  return false;
}

bool decodeGeometry(Core *core,
                    State &state,
                    uint32_t packet,
                    uint16_t otBin,
                    uint16_t drawMode,
                    uint8_t wordCount,
                    world_scene_oracle::Record &record) {
  if (!ramSpan(packet, 8u)) {
    return refuse(state, "packet_out_of_ram");
  }
  const uint32_t codeWord = core->mem_r32(kseg(packet + 4u));
  const uint8_t code = (uint8_t)(codeWord >> 24);
  const uint8_t baseCode = code & (uint8_t)~2u;
  record = {};
  record.packet = kseg(packet);
  record.otBin = otBin;
  switch (baseCode) {
  case 0x30:
    record.family = world_recipe::Family::G3;
    record.vertexCount = 3;
    break;
  case 0x34:
    record.family = world_recipe::Family::GT3;
    record.vertexCount = 3;
    record.material.textured = true;
    break;
  case 0x38:
    record.family = world_recipe::Family::G4;
    record.vertexCount = 4;
    break;
  case 0x3c:
    record.family = world_recipe::Family::GT4;
    record.vertexCount = 4;
    record.material.textured = true;
    break;
  default:
    return refuse(state, "unsupported_packet");
  }
  const uint32_t packetBytes = record.material.textured ? (record.vertexCount == 4 ? 52u : 40u)
                                                        : (record.vertexCount == 4 ? 36u : 28u);
  if (wordCount != packetBytes / 4u - 1u || !ramSpan(packet, packetBytes)) {
    return refuse(state, "packet_size_mismatch");
  }
  record.material.semiTransparent = (code & 2u) != 0;
  for (uint32_t vertex = 0; vertex < record.vertexCount; ++vertex) {
    const uint32_t stride = record.material.textured ? 12u : 8u;
    const uint32_t rgbAddress = packet + 4u + vertex * stride;
    const uint32_t xyAddress = rgbAddress + 4u;
    const uint32_t rgb = core->mem_r32(kseg(rgbAddress));
    const uint32_t xy = core->mem_r32(kseg(xyAddress));
    auto &out = record.vertices[vertex];
    out.sx = (int16_t)xy;
    out.sy = (int16_t)(xy >> 16);
    out.rgb = rgb & 0x00ffffffu;
    if (record.material.textured) {
      const uint32_t uv = core->mem_r32(kseg(xyAddress + 4u));
      out.u = (uint8_t)uv;
      out.v = (uint8_t)(uv >> 8);
      if (vertex == 0) {
        record.material.clut = (uint16_t)(uv >> 16);
      } else if (vertex == 1) {
        record.material.tpage = (uint16_t)(uv >> 16);
      }
    }
  }
  if (!record.material.textured && record.material.semiTransparent) {
    record.material.tpage = drawMode & 0x60u;
  }
  return true;
}

bool captureFinal(Core *core, State &state) {
  const uint32_t otBase = core->mem_r32(kOtPointer);
  if (!ramSpan(otBase, kOtBins * 8u) || state.poolEnd < state.poolBegin) {
    return refuse(state, "invalid_ot_or_pool");
  }
  uint16_t drawMode = 0;
  std::unordered_set<uint32_t> visited;
  std::unordered_set<uint32_t> observedWorldPackets;
  std::unordered_map<uint32_t, uint32_t> nextSuborder;
  std::vector<CapturedRecord> captured;
  for (uint32_t reverse = kOtBins; reverse > 0; --reverse) {
    const uint32_t bin = reverse - 1u;
    // Each bucket stores {last, first}. The unmerged producer chain is
    // traversable from `first`; func_80016784 later joins those chains.
    uint32_t packet = core->mem_r32(kseg(otBase + bin * 8u + 4u));
    for (uint32_t count = 0; packet != 0 && count < kMaxChain; ++count) {
      packet = kseg(packet);
      if (!ramSpan(packet, 8u) || !visited.insert(packet).second) {
        return refuse(state, "invalid_ot_chain");
      }
      const uint32_t tag = core->mem_r32(packet);
      const uint8_t wordCount = (uint8_t)(tag >> 24);
      const uint32_t command = core->mem_r32(kseg(packet + 4u));
      if (wordCount != 0 && (command >> 24) == 0xe1u) {
        drawMode = (uint16_t)command;
      }
      if (inPool(state, packet)) {
        observedWorldPackets.insert(packet);
        if (!state.linkedPackets.contains(packet) && !state.adaptivePackets.contains(packet)) {
          return refuse(state, "unannotated_world_packet");
        }
        const auto group = state.paintGroups.find(packet);
        if (group == state.paintGroups.end()) {
          return refuse(state, "paint_group_unavailable");
        }
        // Adaptive parents remain as zero-word splice nodes in the chain.
        if (wordCount != 0 && (command >> 24) != 0xe1u) {
          CapturedRecord out{};
          if (!decodeGeometry(
                  core, state, packet, (uint16_t)bin, drawMode, wordCount, out.record)) {
            return false;
          }
          if (const auto source = state.sources.find(packet); source != state.sources.end()) {
            out.record.source = source->second;
            if (const auto words = state.highFaceWords.find(source->second);
                words != state.highFaceWords.end()) {
              std::copy_n(words->second.begin(), 4, out.record.clipWords.begin());
              out.record.clipStatus = words->second[4];
            }
          }
          out.paintGroup = group->second;
          out.paintSuborder = nextSuborder[out.paintGroup]++;
          captured.push_back(out);
        }
      }
      const uint32_t next = tag & 0x00ffffffu;
      packet = next == 0 ? 0 : kseg(next);
    }
    if (packet != 0) {
      return refuse(state, "ot_chain_limit");
    }
  }

  if (observedWorldPackets.size() != state.linkedPackets.size() + state.adaptivePackets.size()) {
    return refuse(state, "world_packet_not_in_final_chain");
  }

  std::vector<world_recipe::Face> orderFaces;
  orderFaces.reserve(captured.size());
  for (const auto &item : captured) {
    world_recipe::Face face{};
    face.otBin = item.record.otBin;
    face.paintGroup = item.paintGroup;
    face.paintSuborder = item.paintSuborder;
    orderFaces.push_back(face);
  }
  std::vector<size_t> order;
  if (!world_recipe::paintOrder(orderFaces, order)) {
    return refuse(state, "invalid_final_paint_order");
  }
  state.records.clear();
  state.records.reserve(captured.size());
  for (const size_t index : order) {
    state.records.push_back(captured[index].record);
  }
  return true;
}

void noteAdaptive(Core *core, uint32_t packet, uint32_t original, bool accepted) {
  std::scoped_lock lock(s_mutex);
  auto found = s_states.find(core);
  if (found == s_states.end() || !found->second.armed) {
    return;
  }
  State &state = found->second;
  const uint32_t childPacket = kseg(packet);
  const auto parentGroup = state.paintGroups.find(kseg(original));
  if (parentGroup == state.paintGroups.end() || !state.adaptivePackets.insert(childPacket).second ||
      !state.paintGroups.emplace(childPacket, parentGroup->second).second) {
    state.valid = refuse(state, "adaptive_parent_or_child_untracked");
    return;
  }
  if (accepted) {
    state.adaptiveChildren++;
  } else {
    state.adaptiveDeferred++;
  }
}

bool takeCapture(Core *core,
                 std::vector<world_scene_oracle::Record> &records,
                 std::vector<HighProjectionSnapshot> &highProjections) {
  std::scoped_lock lock(s_mutex);
  records.clear();
  highProjections.clear();
  auto found = s_states.find(core);
  if (found == s_states.end() || !found->second.complete || !found->second.valid) {
    return false;
  }
  records = std::move(found->second.records);
  highProjections = std::move(found->second.highProjections);
  s_states.erase(found);
  return true;
}

std::array<uint32_t, 4> highVertexOffsets(uint32_t word) {
  return {(word >> 22) & 0x3fcu, (word >> 14) & 0x3fcu, (word >> 6) & 0x3fcu, (word << 2) & 0x3fcu};
}

const HighProjectionSnapshot *findProjection(std::span<const HighProjectionSnapshot> snapshots,
                                             uint32_t sector,
                                             uint32_t vertexOffset) {
  const uint32_t cacheAddress = 0x1f800000u + vertexOffset;
  const auto found = std::find_if(snapshots.begin(), snapshots.end(), [&](const auto &snapshot) {
    return snapshot.sector == sector && snapshot.cacheAddress == cacheAddress;
  });
  return found == snapshots.end() ? nullptr : &*found;
}

} // namespace

void begin(Core *core) {
  if (!cfg_on("PSXPORT_WORLD_SCENE_ORACLE")) {
    return;
  }
  std::scoped_lock lock(s_mutex);
  State &state = s_states[core];
  state = {};
  state.armed = true;
  state.poolBegin = kseg(core->mem_r32(kPoolPointer));
}

void noteHighFace(Core *core,
                  uint32_t source,
                  uint32_t v0,
                  uint32_t v1,
                  uint32_t v2,
                  uint32_t v3,
                  uint32_t status) {
  std::scoped_lock lock(s_mutex);
  auto found = s_states.find(core);
  if (found == s_states.end() || !found->second.armed) {
    return;
  }
  found->second.highFaceWords[kseg(source)] = {v0, v1, v2, v3, status};
}

void noteCoarseHighProjection(Core *core, uint32_t sector, uint32_t cacheAddress) {
  if (!cfg_on("PSXPORT_WORLD_SCENE_ORACLE")) {
    return;
  }
  HighProjectionSnapshot snapshot{};
  snapshot.sector = kseg(sector);
  snapshot.cacheAddress = cacheAddress;
  snapshot.packedVxy = gte_read_data(0);
  snapshot.packedVz = gte_read_data(1);
  const uint32_t c0 = gte_read_ctrl(0);
  const uint32_t c1 = gte_read_ctrl(1);
  const uint32_t c2 = gte_read_ctrl(2);
  const uint32_t c3 = gte_read_ctrl(3);
  const uint32_t c4 = gte_read_ctrl(4);
  snapshot.affine.m = {{{(int16_t)c0, (int16_t)(c0 >> 16), (int16_t)c1},
                        {(int16_t)(c1 >> 16), (int16_t)c2, (int16_t)(c2 >> 16)},
                        {(int16_t)c3, (int16_t)(c3 >> 16), (int16_t)c4}}};
  snapshot.affine.t = {
      {(int32_t)gte_read_ctrl(5), (int32_t)gte_read_ctrl(6), (int32_t)gte_read_ctrl(7)}};
  snapshot.projection = {.ofx = (int32_t)gte_read_ctrl(24),
                         .ofy = (int32_t)gte_read_ctrl(25),
                         .h = (uint16_t)gte_read_ctrl(26),
                         .dqa = (int16_t)gte_read_ctrl(27),
                         .dqb = (int32_t)gte_read_ctrl(28)};
  snapshot.projected = psxport::native_projection::project(snapshot.affine,
                                                           snapshot.projection,
                                                           {(int16_t)snapshot.packedVxy,
                                                            (int16_t)(snapshot.packedVxy >> 16),
                                                            (int16_t)snapshot.packedVz});

  std::scoped_lock lock(s_mutex);
  auto found = s_states.find(core);
  if (found == s_states.end() || !found->second.armed) {
    return;
  }
  found->second.highProjections.push_back(snapshot);
}

void noteLink(Core *core, uint32_t packet, uint32_t otSlot, Phase phase, uint32_t source) {
  std::scoped_lock lock(s_mutex);
  auto found = s_states.find(core);
  if (found == s_states.end() || !found->second.armed) {
    return;
  }
  State &state = found->second;
  const uint32_t otBase = core->mem_r32(kOtPointer);
  if (otSlot < otBase || otSlot >= otBase + kOtBins * 8u || ((otSlot - otBase) & 7u)) {
    state.valid = refuse(state, "invalid_link_slot");
    return;
  }
  state.phaseLinks[(uint8_t)phase]++;
  const uint32_t linkedPacket = kseg(packet);
  if (!state.linkedPackets.insert(linkedPacket).second ||
      !state.paintGroups.emplace(linkedPacket, state.nextPaintGroup++).second) {
    state.valid = refuse(state, "duplicate_link_hook");
  }
  if (source != 0) {
    state.sources.emplace(linkedPacket, kseg(source));
  }
}

void noteAdaptiveChild(Core *core, uint32_t packet, uint32_t original) {
  noteAdaptive(core, packet, original, true);
}

void noteAdaptiveDeferred(Core *core, uint32_t packet, uint32_t original) {
  noteAdaptive(core, packet, original, false);
}

void noteAdaptiveReplacement(Core *core, uint32_t original, uint32_t head, uint32_t otSlot) {
  std::scoped_lock lock(s_mutex);
  auto found = s_states.find(core);
  if (found == s_states.end() || !found->second.armed) {
    return;
  }
  State &state = found->second;
  const uint32_t otBase = core->mem_r32(kOtPointer);
  const uint32_t originalPacket = kseg(original);
  if ((!state.linkedPackets.contains(originalPacket) &&
       !state.adaptivePackets.contains(originalPacket)) ||
      !ramSpan(head, 4u) || otSlot < otBase || otSlot >= otBase + kOtBins * 8u ||
      ((otSlot - otBase) & 7u) || !state.paintGroups.contains(originalPacket)) {
    state.valid = refuse(state, "invalid_adaptive_replacement");
    return;
  }
  state.adaptiveReplacements++;
}

void finishGuest(Core *core, uint32_t poolEnd) {
  std::scoped_lock lock(s_mutex);
  auto found = s_states.find(core);
  if (found == s_states.end() || !found->second.armed) {
    return;
  }
  State &state = found->second;
  state.poolEnd = kseg(poolEnd);
  state.valid = state.valid && captureFinal(core, state);
  state.complete = true;
  state.armed = false;
  lucent::info("worldsceneoracle",
               "retail final stream: records={} valid={} links={}/{}/{}/{}/{}/{} "
               "adaptive children={} deferred={} replacements={} refusal={}",
               state.records.size(),
               state.valid ? "yes" : "NO",
               state.phaseLinks[0],
               state.phaseLinks[1],
               state.phaseLinks[2],
               state.phaseLinks[3],
               state.phaseLinks[4],
               state.phaseLinks[5],
               state.adaptiveChildren,
               state.adaptiveDeferred,
               state.adaptiveReplacements,
               state.refusal);
}

bool take(Core *core, std::vector<world_scene_oracle::Record> &records) {
  std::vector<HighProjectionSnapshot> highProjections;
  return takeCapture(core, records, highProjections);
}

Status verify(Core *core,
              std::span<const world_recipe::Face> semantic,
              std::span<const world_hq_recipe::AuditEntry> audit) {
  std::vector<world_scene_oracle::Record> retail;
  std::vector<HighProjectionSnapshot> highProjections;
  if (!takeCapture(core, retail, highProjections)) {
    lucent::error("worldsceneoracle", "retail semantic capture is incomplete or malformed");
    return Status::Refused;
  }
  std::vector<world_scene_oracle::Record> native;
  if (!world_scene_oracle::expected(semantic, native)) {
    lucent::error("worldsceneoracle", "semantic recipe has invalid or ambiguous paint identity");
    return Status::Refused;
  }
  const auto difference = world_scene_oracle::compare(retail, native);
  if (!difference.equal) {
    size_t mismatchRecord = difference.record;
    if (difference.field == std::string_view("record_count")) {
      const size_t shared = std::min(retail.size(), native.size());
      mismatchRecord = 0;
      while (mismatchRecord < shared &&
             world_scene_oracle::compare(
                 std::span<const world_scene_oracle::Record>(&retail[mismatchRecord], 1),
                 std::span<const world_scene_oracle::Record>(&native[mismatchRecord], 1))
                 .equal) {
        ++mismatchRecord;
      }
      const bool retailHasOneExtra =
          retail.size() == native.size() + 1 && mismatchRecord + 1 < retail.size() &&
          mismatchRecord < native.size() &&
          world_scene_oracle::compare(
              std::span<const world_scene_oracle::Record>(&retail[mismatchRecord + 1], 1),
              std::span<const world_scene_oracle::Record>(&native[mismatchRecord], 1))
              .equal;
      const bool semanticHasOneExtra =
          native.size() == retail.size() + 1 && mismatchRecord + 1 < native.size() &&
          mismatchRecord < retail.size() &&
          world_scene_oracle::compare(
              std::span<const world_scene_oracle::Record>(&retail[mismatchRecord], 1),
              std::span<const world_scene_oracle::Record>(&native[mismatchRecord + 1], 1))
              .equal;
      lucent::error("worldsceneoracle",
                    "record-count streams first diverge at {} retail_extra={} semantic_extra={}",
                    mismatchRecord,
                    retailHasOneExtra,
                    semanticHasOneExtra);
    }
    lucent::error("worldsceneoracle",
                  "DIFF retail={} semantic={} first_record={} field={}",
                  retail.size(),
                  native.size(),
                  mismatchRecord,
                  difference.field);
    if (mismatchRecord < retail.size() && mismatchRecord < native.size()) {
      const auto &left = retail[mismatchRecord].material;
      const auto &right = native[mismatchRecord].material;
      lucent::error("worldsceneoracle",
                    "material retail tex={} semi={} clut={:#06x} tpage={:#06x}; semantic "
                    "tex={} semi={} clut={:#06x} tpage={:#06x}",
                    left.textured,
                    left.semiTransparent,
                    left.clut,
                    left.tpage,
                    right.textured,
                    right.semiTransparent,
                    right.clut,
                    right.tpage);
      lucent::error("worldsceneoracle",
                    "retail packet={:#010x} source={:#010x} clip={:#010x}/{:#010x}/{:#010x}/"
                    "{:#010x} status={:#010x}",
                    retail[mismatchRecord].packet,
                    retail[mismatchRecord].source,
                    retail[mismatchRecord].clipWords[0],
                    retail[mismatchRecord].clipWords[1],
                    retail[mismatchRecord].clipWords[2],
                    retail[mismatchRecord].clipWords[3],
                    retail[mismatchRecord].clipStatus);
      for (const auto &entry : audit) {
        if (entry.source == retail[mismatchRecord].source) {
          lucent::error("worldsceneoracle",
                        "semantic source audit sector={:#010x} flags={:#010x} tags={} depth={} "
                        "chunk_common={:#04x} face_common={:#04x} decision={}",
                        entry.sector,
                        entry.flags,
                        entry.tags,
                        entry.depth,
                        entry.chunkCommonClip,
                        entry.commonClip,
                        (uint32_t)entry.decision);
          lucent::error("worldsceneoracle",
                        "projection ofx={:#010x} ofy={:#010x} h={} dqa={} dqb={} "
                        "matrix={},{},{};{},{},{};{},{},{} translation={},{},{}",
                        (uint32_t)entry.projection.ofx,
                        (uint32_t)entry.projection.ofy,
                        entry.projection.h,
                        entry.projection.dqa,
                        entry.projection.dqb,
                        entry.cameraMatrix.m[0][0],
                        entry.cameraMatrix.m[0][1],
                        entry.cameraMatrix.m[0][2],
                        entry.cameraMatrix.m[1][0],
                        entry.cameraMatrix.m[1][1],
                        entry.cameraMatrix.m[1][2],
                        entry.cameraMatrix.m[2][0],
                        entry.cameraMatrix.m[2][1],
                        entry.cameraMatrix.m[2][2],
                        entry.cameraMatrix.t[0],
                        entry.cameraMatrix.t[1],
                        entry.cameraMatrix.t[2]);
          for (uint32_t vertex = 0; vertex < entry.vertices.size(); ++vertex) {
            const auto &v = entry.vertices[vertex];
            lucent::error("worldsceneoracle",
                          "source vertex{} model={}/{}/{} semantic sxy={}/{} sz={}",
                          vertex,
                          v.modelX,
                          v.modelY,
                          v.modelZ,
                          v.sx,
                          v.sy,
                          v.sz);
          }
          if (ramSpan(entry.source, 4u)) {
            const auto offsets = highVertexOffsets(core->mem_r32(entry.source));
            for (uint32_t vertex = 0; vertex < offsets.size(); ++vertex) {
              const auto *snapshot = findProjection(highProjections, entry.sector, offsets[vertex]);
              if (!snapshot) {
                continue;
              }
              lucent::error(
                  "worldsceneoracle",
                  "guest RTPS input vertex{} cache={:#010x} model={}/{}/{} native={}/{} sz={} "
                  "flags={:#010x} projection={:#010x}/{:#010x}/{}/{}/{} "
                  "matrix={},{},{};{},{},{};{},{},{} translation={},{},{}",
                  vertex,
                  snapshot->cacheAddress,
                  (int16_t)snapshot->packedVxy,
                  (int16_t)(snapshot->packedVxy >> 16),
                  (int16_t)snapshot->packedVz,
                  snapshot->projected.sx,
                  snapshot->projected.sy,
                  snapshot->projected.sz,
                  snapshot->projected.flags,
                  (uint32_t)snapshot->projection.ofx,
                  (uint32_t)snapshot->projection.ofy,
                  snapshot->projection.h,
                  snapshot->projection.dqa,
                  snapshot->projection.dqb,
                  snapshot->affine.m[0][0],
                  snapshot->affine.m[0][1],
                  snapshot->affine.m[0][2],
                  snapshot->affine.m[1][0],
                  snapshot->affine.m[1][1],
                  snapshot->affine.m[1][2],
                  snapshot->affine.m[2][0],
                  snapshot->affine.m[2][1],
                  snapshot->affine.m[2][2],
                  snapshot->affine.t[0],
                  snapshot->affine.t[1],
                  snapshot->affine.t[2]);
            }
          }
        }
      }
      for (uint32_t vertex = 0; vertex < retail[mismatchRecord].vertexCount; ++vertex) {
        const auto &a = retail[mismatchRecord].vertices[vertex];
        const auto &b = native[mismatchRecord].vertices[vertex];
        lucent::error("worldsceneoracle",
                      "vertex{} retail xy={}/{} rgb={:#08x} uv={}/{}; semantic "
                      "xy={}/{} rgb={:#08x} uv={}/{}",
                      vertex,
                      a.sx,
                      a.sy,
                      a.rgb,
                      a.u,
                      a.v,
                      b.sx,
                      b.sy,
                      b.rgb,
                      b.u,
                      b.v);
      }
      std::vector<size_t> semanticOrder;
      if (world_recipe::paintOrder(semantic, semanticOrder) &&
          mismatchRecord < semanticOrder.size()) {
        const auto &face = semantic[semanticOrder[mismatchRecord]];
        lucent::error("worldsceneoracle",
                      "semantic face source={:#010x} sector={:#010x} origin={} bin={} "
                      "depth={}/{}/{}/{} uv0={}/{} uv1={}/{} texture_source={:#010x}",
                      face.source,
                      face.sector,
                      (uint32_t)face.origin,
                      face.otBin,
                      face.vertices[0].sz,
                      face.vertices[1].sz,
                      face.vertices[2].sz,
                      face.vertices[3].sz,
                      face.vertices[0].u,
                      face.vertices[0].v,
                      face.vertices[1].u,
                      face.vertices[1].v,
                      face.textureSource);
        if (face.textureSource && ramSpan(face.textureSource, 8u)) {
          const uint32_t rawFirst = core->mem_r32(face.textureSource);
          const uint32_t rawSecond = core->mem_r32(face.textureSource + 4u);
          const uint32_t attribute = rawSecond >> 25;
          const uint32_t adjustment = 0x8006d058u + attribute;
          const bool hasAdjustment = attribute && ramSpan(adjustment, 8u);
          lucent::error("worldsceneoracle",
                        "semantic texture raw={:#010x}/{:#010x} attribute={:#04x} "
                        "adjustment={:#010x} words={:#010x}/{:#010x}",
                        rawFirst,
                        rawSecond,
                        attribute,
                        adjustment,
                        hasAdjustment ? core->mem_r32(adjustment) : 0u,
                        hasAdjustment ? core->mem_r32(adjustment + 4u) : 0u);
        }
        for (const size_t index : semanticOrder) {
          const auto &sameSource = semantic[index];
          if (sameSource.source == retail[mismatchRecord].source) {
            lucent::error("worldsceneoracle",
                          "same-source semantic sector={:#010x} origin={} bin={} group={} "
                          "suborder={}",
                          sameSource.sector,
                          (uint32_t)sameSource.origin,
                          sameSource.otBin,
                          sameSource.paintGroup,
                          sameSource.paintSuborder);
          }
        }
      }
    }
    return Status::Mismatch;
  }
  lucent::info("worldsceneoracle", "PASS {} final semantic world record(s)", retail.size());
  return Status::Match;
}

} // namespace spyro::world_scene_capture
