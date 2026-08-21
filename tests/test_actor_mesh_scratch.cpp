#include "actor_mesh_scratch.h"

#include <cstdio>
#include <cstdlib>

namespace {

void require(bool condition, const char *what) {
  if (!condition) {
    std::fprintf(stderr, "actor_mesh_scratch: %s\n", what);
    std::abort();
  }
}

} // namespace

int main() {
  constexpr uint32_t workTop = 0x80200000u;
  constexpr spyro::ActorMeshScratchLayout whole = spyro::actorMeshScratchLayout(workTop, false);
  require(whole.emitList == 0x801FE000u && whole.otBase == 0x801FA000u &&
              whole.primTop == 0x801F9FF8u,
          "fixed header carving differs from 0x8005B6F8");
  require(whole.primBase1 == 0x801DDFF8u && whole.regionBase == 0x801C1FF8u,
          "whole-region branch did not apply two 0x1C000 strides");

  constexpr spyro::ActorMeshScratchLayout split = spyro::actorMeshScratchLayout(workTop, true);
  require(split.emitList == whole.emitList && split.otBase == whole.otBase &&
              split.primTop == whole.primTop,
          "split mode changed the fixed scratch headers");
  require(split.primBase1 == 0x801E6FF8u && split.regionBase == 0x801D3FF8u,
          "split-region branch did not apply two 0x13000 strides");
  require(split.primBase1 != whole.primBase1 && split.regionBase != whole.regionBase,
          "branch discriminator produced identical layouts");
  return 0;
}
