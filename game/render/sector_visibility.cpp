#include "sector_visibility.h"

#include "core.h"

namespace spyro::sector_visibility {

Table readGuest(Core &core) {
  Table table{};
  for (uint32_t i = 0; i < kSectors; ++i) {
    table[i] = core.mem_r8(kGuestTable + i);
  }
  return table;
}

void publishGuest(Core &core, const Table &table) {
  for (uint32_t i = 0; i < kSectors; ++i) {
    core.mem_w8(kGuestTable + i, table[i]);
  }
}

} // namespace spyro::sector_visibility
