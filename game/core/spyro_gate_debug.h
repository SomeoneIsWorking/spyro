#pragma once

#include <cstdint>

class Core;

namespace spyro::gate_debug {

struct Position {
  int32_t x = 0;
  int32_t y = 0;
  int32_t z = 0;
};

struct GateInfo {
  bool valid = false;
  uint32_t portalAddress = 0;
  int32_t pathMoby = -1;
  int32_t levelId = -1;
  Position firstPoint;
  uint32_t nodeCount = 0;
  Position nodes[16]{};
};

bool inspectGate(Core &core, uint32_t index, GateInfo &out);
bool teleportToGate(Core &core, uint32_t index, uint32_t node);
bool replCommand(Core *core, const char *command, const char *line);

} // namespace spyro::gate_debug
