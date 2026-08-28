#include "spyro_gate_debug.h"

#include "core.h"
#include <cstdio>
#include <cstring>
#include <lucent/log.h>

namespace spyro::gate_debug {
namespace {

constexpr uint32_t kRamBegin = 0x80000000u;
constexpr uint32_t kRamEnd = 0x801fffffu;
constexpr uint32_t kGamestate = 0x800757d8u;
constexpr uint32_t kLoadStage = 0x80075864u;
constexpr uint32_t kLevelId = 0x8007596cu;
constexpr uint32_t kNextLevelId = 0x800758b4u;
constexpr uint32_t kPortalCount = 0x800758bcu;
constexpr uint32_t kPortals = 0x80078640u;
constexpr uint32_t kLevelMobys = 0x80075828u;
constexpr uint32_t kSpyro = 0x80078a58u;

constexpr uint32_t kPortalPathMoby = 0x18u;
constexpr uint32_t kPortalLevelId = 0x1cu;
constexpr uint32_t kPortalCenter = 0x20u;
constexpr uint32_t kMobyStride = 0x58u;
constexpr uint32_t kMobyProps = 0x00u;
constexpr uint32_t kPathData = 0x00u;
constexpr uint32_t kPathNodeCount = 0x00u;
constexpr uint32_t kPathNodes = 0x08u;
constexpr uint32_t kPathNodeStride = 0x10u;
constexpr uint32_t kSpyroPosition = 0x00u;
constexpr uint32_t kSpyroPreviousPosition = 0x8cu;

bool validAddress(uint32_t address, uint32_t bytes = 4u) {
  return address >= kRamBegin && address <= kRamEnd && bytes <= kRamEnd - address + 1u;
}

bool validPointer(uint32_t address, uint32_t bytes = 4u) {
  return address != 0u && validAddress(address, bytes);
}

bool readPointer(Core &core, uint32_t address, uint32_t &out) {
  if (!validAddress(address)) {
    return false;
  }
  out = core.mem_r32(address);
  return validPointer(out);
}

Position readPosition(Core &core, uint32_t address) {
  return {static_cast<int32_t>(core.mem_r32(address)),
          static_cast<int32_t>(core.mem_r32(address + 4u)),
          static_cast<int32_t>(core.mem_r32(address + 8u))};
}

bool readGatePath(Core &core, uint32_t portal, GateInfo &out) {
  if (!validPointer(portal) || !validAddress(portal + kPortalCenter, 12u)) {
    return false;
  }
  const int32_t pathMoby = static_cast<int32_t>(core.mem_r32(portal + kPortalPathMoby));
  const uint32_t levelMobys = core.mem_r32(kLevelMobys);
  if (pathMoby < 0 || !validPointer(levelMobys)) {
    return false;
  }
  const uint64_t mobyOffset = static_cast<uint64_t>(static_cast<uint32_t>(pathMoby)) * kMobyStride;
  if (mobyOffset > 0xffffffffu) {
    return false;
  }
  const uint32_t moby = levelMobys + static_cast<uint32_t>(mobyOffset);
  if (!validAddress(moby, 4u)) {
    return false;
  }
  const uint32_t props = core.mem_r32(moby + kMobyProps);
  uint32_t path = 0;
  if (!validPointer(props) || !readPointer(core, props + kPathData, path)) {
    return false;
  }
  const uint32_t nodeCount = core.mem_r8(path + kPathNodeCount);
  if (nodeCount == 0u || nodeCount > 16u ||
      !validAddress(path + kPathNodes, nodeCount * kPathNodeStride)) {
    return false;
  }

  out.valid = true;
  out.portalAddress = portal;
  out.pathMoby = pathMoby;
  out.levelId = static_cast<int32_t>(core.mem_r32(portal + kPortalLevelId));
  out.center = readPosition(core, portal + kPortalCenter);
  out.nodeCount = nodeCount;
  for (uint32_t i = 0; i < nodeCount; ++i) {
    out.nodes[i] = readPosition(core, path + kPathNodes + i * kPathNodeStride);
  }
  return true;
}

bool parseIndex(const char *line, uint32_t &index, uint32_t &node) {
  unsigned parsedIndex = 0;
  unsigned parsedNode = 0;
  if (std::sscanf(line, "%*s %u %u", &parsedIndex, &parsedNode) != 2) {
    return false;
  }
  index = parsedIndex;
  node = parsedNode;
  return true;
}

} // namespace

bool inspectGate(Core &core, uint32_t index, GateInfo &out) {
  out = {};
  const uint32_t count = core.mem_r32(kPortalCount);
  if (index >= count || index >= 16u || !validAddress(kPortals + index * 4u)) {
    return false;
  }
  uint32_t portal = 0;
  if (!readPointer(core, kPortals + index * 4u, portal)) {
    return false;
  }
  return readGatePath(core, portal, out);
}

bool teleportToGate(Core &core, uint32_t index, uint32_t node) {
  if (core.mem_r32(kGamestate) != 0u || core.mem_r32(kLoadStage) != 0xffffffffu) {
    lucent::info("spyro-gate", "gate-teleport refused: gameplay is not in an active field");
    return false;
  }
  GateInfo gate;
  if (!inspectGate(core, index, gate)) {
    lucent::info(
        "spyro-gate", "gate-teleport refused: gate {} is not a valid loaded portal", index);
    return false;
  }
  if (node >= gate.nodeCount || !validAddress(kSpyro + kSpyroPreviousPosition, 12u)) {
    lucent::info("spyro-gate", "gate-teleport refused: node {} is outside gate {}", node, index);
    return false;
  }
  const Position destination = gate.nodes[node];
  core.mem_w32(kSpyro + kSpyroPreviousPosition, static_cast<uint32_t>(destination.x));
  core.mem_w32(kSpyro + kSpyroPreviousPosition + 4u, static_cast<uint32_t>(destination.y));
  core.mem_w32(kSpyro + kSpyroPreviousPosition + 8u, static_cast<uint32_t>(destination.z));
  core.mem_w32(kSpyro + kSpyroPosition, static_cast<uint32_t>(destination.x));
  core.mem_w32(kSpyro + kSpyroPosition + 4u, static_cast<uint32_t>(destination.y));
  core.mem_w32(kSpyro + kSpyroPosition + 8u, static_cast<uint32_t>(destination.z));
  lucent::info("spyro-gate",
               "gate-teleport gate={} node={} targetLevel={} position=({}, {}, {})",
               index,
               node,
               gate.levelId,
               destination.x,
               destination.y,
               destination.z);
  return true;
}

bool replCommand(Core *core, const char *command, const char *line) {
  if (std::strcmp(command, "gates") != 0 && std::strcmp(command, "gate-teleport") != 0) {
    return false;
  }
  if (std::strcmp(command, "gate-teleport") == 0) {
    uint32_t index = 0;
    uint32_t node = 0;
    if (!parseIndex(line, index, node)) {
      lucent::info("spyro-gate", "usage: gate-teleport <portal-index> <path-node>");
      return true;
    }
    (void)teleportToGate(*core, index, node);
    return true;
  }

  const uint32_t count = core->mem_r32(kPortalCount);
  lucent::info("spyro-gate",
               "level={} nextLevel={} gamestate={} loadStage={} portals={}",
               static_cast<int32_t>(core->mem_r32(kLevelId)),
               static_cast<int32_t>(core->mem_r32(kNextLevelId)),
               core->mem_r32(kGamestate),
               core->mem_r32(kLoadStage),
               count);
  for (uint32_t i = 0; i < count && i < 16u; ++i) {
    GateInfo gate;
    if (!inspectGate(*core, i, gate)) {
      lucent::info("spyro-gate", "gate={} INVALID or unloaded", i);
      continue;
    }
    lucent::info("spyro-gate",
                 "gate={} targetLevel={} pathMoby={} center=({}, {}, {}) nodes={}",
                 i,
                 gate.levelId,
                 gate.pathMoby,
                 gate.center.x,
                 gate.center.y,
                 gate.center.z,
                 gate.nodeCount);
    for (uint32_t node = 0; node < gate.nodeCount; ++node) {
      lucent::info("spyro-gate",
                   "  node={} position=({}, {}, {})",
                   node,
                   gate.nodes[node].x,
                   gate.nodes[node].y,
                   gate.nodes[node].z);
    }
  }
  return true;
}

} // namespace spyro::gate_debug
