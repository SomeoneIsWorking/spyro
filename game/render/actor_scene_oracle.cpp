#include "actor_scene_oracle.h"

#include "cfg.h"
#include "core.h"
#include "game.h"
#include "gpu_packet_decode.h"
#include "guest_call.h"
#include "render_queue.h"

#include <algorithm>
#include <cstdint>
#include <lucent/log.h>
#include <span>
#include <utility>
#include <vector>

namespace spyro::actor_scene_oracle {
namespace {

constexpr uint32_t kOtPointer = 0x80075820u;
constexpr uint32_t kPoolCursor = 0x800757B0u;
constexpr uint32_t kPacketCount = 0x800758B0u;
constexpr uint32_t kOtBins = 0x800u;
constexpr uint32_t kMaxChain = 65536u;
// Moby::m_Class and the instance size, from external/spyro-1's byte-identical struct.
constexpr uint32_t kMobyClass = 54u;
constexpr uint32_t kMobyBytes = 0x58u;
constexpr uint32_t kMobyState = 72u;
constexpr uint32_t kLevelMobys = 0x80075828u;
constexpr uint32_t kMaxLevelMobys = 1024u;
constexpr uint32_t kCamera = 0x80076DD0u;

constexpr uint32_t kseg(uint32_t address) {
  return 0x80000000u | (address & 0x1fffffu);
}

bool ramSpan(uint32_t address, uint32_t bytes) {
  const uint32_t physical = address & 0x1fffffffu;
  return (address & 3u) == 0u && bytes != 0u && physical <= 0x200000u - bytes;
}

// The retail packets this producer linked, in the OT walk order the GPU would consume them.
struct Retail {
  uint16_t bin = 0;
  gpu_packet_decode::Packet packet{};
};

void logNative(Core *core, std::span<const uint32_t> painterKeys) {
  const RenderQueue &queue = core->game->rq;
  const int offX = core->game->gpu.s_off_x;
  const int offY = core->game->gpu.s_off_y;
  uint32_t emitted = 0;
  for (int i = 0; i < queue.n; ++i) {
    const RqItem &item = queue.items[i];
    if (std::find(painterKeys.begin(), painterKeys.end(), item.painter_object) ==
        painterKeys.end()) {
      continue;
    }
    // Screen positions are logged without the draw offset the native path already applied, because
    // a retail packet stores its vertices before the GPU adds it.
    lucent::debug("actororacle",
                  "native rec={} nv={} semi={} tex={} ord={:.6f} node=0x{:08X} v0={},{},{:06X} "
                  "v1={},{},{:06X} v2={},{},{:06X} "
                  "v3={},{},{:06X}",
                  emitted,
                  item.nv,
                  item.semi,
                  // Colour mode 3 is this queue's untextured sentinel; retail says the same thing
                  // with its 0x34/0x3C texture bit, so the two streams can be compared on whether a
                  // primitive carries a texture at all.
                  item.mode == 3 ? 0 : 1,
                  // The normalized depth this item will be drawn at. Retail says the same thing
                  // with the OT bin it linked the packet into, so a primitive that matches on
                  // pixels but disagrees here is a depth fault and nothing else.
                  item.depth[0],
                  // The instance this face belongs to. Two faces of one moby may legitimately
                  // disagree with retail's ordering, because retail leans on submission order
                  // inside a bin; a disagreement BETWEEN instances is what shows as pop-through.
                  item.dbg_node,
                  item.xs[0] - offX,
                  item.ys[0] - offY,
                  ((uint32_t)item.bs[0] << 16) | ((uint32_t)item.gs[0] << 8) | item.rs[0],
                  item.xs[1] - offX,
                  item.ys[1] - offY,
                  ((uint32_t)item.bs[1] << 16) | ((uint32_t)item.gs[1] << 8) | item.rs[1],
                  item.xs[2] - offX,
                  item.ys[2] - offY,
                  ((uint32_t)item.bs[2] << 16) | ((uint32_t)item.gs[2] << 8) | item.rs[2],
                  item.nv > 3 ? item.xs[3] - offX : 0,
                  item.nv > 3 ? item.ys[3] - offY : 0,
                  item.nv > 3
                      ? (((uint32_t)item.bs[3] << 16) | ((uint32_t)item.gs[3] << 8) | item.rs[3])
                      : 0u);
    ++emitted;
  }
  // The retail chain walker draws every moby chain, while the native side splits that work across
  // several producers. Without this histogram a count difference cannot be told apart from work
  // that simply landed under a different painter key.
  std::vector<std::pair<uint32_t, uint32_t>> byPainter;
  for (int i = 0; i < queue.n; ++i) {
    const uint32_t key = queue.items[i].painter_object;
    auto found = std::find_if(byPainter.begin(), byPainter.end(), [key](const auto &entry) {
      return entry.first == key;
    });
    if (found == byPainter.end()) {
      byPainter.emplace_back(key, 1u);
    } else {
      ++found->second;
    }
  }
  for (const auto &[key, count] : byPainter) {
    lucent::debug("actororacle", "native painter 0x{:08X}: items={}", key, count);
  }
  // One line per drawn Moby instance with the model it selected. Without it an instance that
  // disagrees with retail on depth is an anonymous address and cannot be named as a gem, an NPC or
  // scenery, which is the whole question a depth or colour report asks.
  std::vector<std::pair<uint32_t, uint32_t>> byInstance;
  for (int i = 0; i < queue.n; ++i) {
    const uint32_t node = queue.items[i].dbg_node;
    if (node == 0u) {
      continue;
    }
    auto found = std::find_if(byInstance.begin(), byInstance.end(), [node](const auto &entry) {
      return entry.first == node;
    });
    if (found == byInstance.end()) {
      byInstance.emplace_back(node, 1u);
    } else {
      ++found->second;
    }
  }
  for (const auto &[node, count] : byInstance) {
    if (!ramSpan(node, 0x58u)) {
      lucent::debug(
          "actororacle", "native instance 0x{:08X}: faces={} model=unreadable", node, count);
      continue;
    }
    // The scale byte at +0x57 multiplies the view-space translation, so it moves an instance's
    // depth as well as its size. A depth report that names an instance without it cannot tell a
    // projection fault from a correctly scaled model.
    lucent::debug("actororacle",
                  "native instance 0x{:08X}: faces={} class={} state=0x{:08X} scale={} "
                  "pos=({},{},{})",
                  node,
                  count,
                  core->mem_r16(node + 54u),
                  core->mem_r32(node + 72u),
                  core->mem_r8(node + 0x57u),
                  (int32_t)core->mem_r32(node + 12u),
                  (int32_t)core->mem_r32(node + 16u),
                  (int32_t)core->mem_r32(node + 20u));
  }
  // The camera position closes the loop on the per-instance lines above: without it an instance's
  // world position cannot be turned into a distance, so a depth disagreement cannot be checked
  // against the geometry that produced it and only retail's own answer is available to compare to.
  lucent::debug("actororacle",
                "native side: painters={} scanned_queue={} emitted={} distinct_painters={} "
                "camera=({},{},{})",
                painterKeys.size(),
                queue.n,
                emitted,
                byPainter.size(),
                (int32_t)core->mem_r32(kCamera + 0x28u),
                (int32_t)core->mem_r32(kCamera + 0x2Cu),
                (int32_t)core->mem_r32(kCamera + 0x30u));
}

bool walkOt(Core *core, uint32_t poolFrom, std::vector<Retail> &out, const char *&refusal) {
  const uint32_t otBase = core->mem_r32(kOtPointer);
  if (!ramSpan(otBase, kOtBins * 8u)) {
    refusal = "invalid_ot";
    return false;
  }
  uint16_t drawMode = 0;
  for (uint32_t reverse = kOtBins; reverse > 0; --reverse) {
    const uint32_t bin = reverse - 1u;
    // Each bucket stores {last, first}; the producer's own chain is traversable from `first`.
    uint32_t packet = core->mem_r32(kseg(otBase + bin * 8u + 4u));
    uint32_t count = 0;
    while (packet != 0 && count < kMaxChain) {
      packet = kseg(packet);
      if (!ramSpan(packet, 8u)) {
        refusal = "invalid_ot_chain";
        return false;
      }
      const uint32_t tag = core->mem_r32(packet);
      const uint8_t wordCount = (uint8_t)(tag >> 24);
      const uint32_t command = core->mem_r32(kseg(packet + 4u));
      if (wordCount != 0 && (command >> 24) == 0xe1u) {
        drawMode = (uint16_t)command;
      } else if (wordCount != 0 && packet >= kseg(poolFrom)) {
        Retail record{.bin = (uint16_t)bin};
        if (gpu_packet_decode::decode(core, packet, drawMode, wordCount, record.packet, refusal)) {
          out.push_back(record);
        }
        // A packet this producer did not build is not a failure of the capture; it is skipped and
        // shows up in the scanned/decoded denominators below.
      }
      const uint32_t next = tag & 0x00ffffffu;
      packet = next == 0 ? 0 : kseg(next);
      ++count;
    }
    if (packet != 0) {
      refusal = "ot_chain_limit";
      return false;
    }
  }
  return true;
}

void logDrawnClasses(Core *core, int wantedClass) {
  const RenderQueue &queue = core->game->rq;
  std::vector<uint32_t> classes;
  for (int i = 0; i < queue.n; ++i) {
    const uint32_t node = queue.items[i].dbg_node;
    if (node == 0u || !ramSpan(node, kMobyBytes)) {
      continue;
    }
    const uint32_t drawn = core->mem_r16(node + kMobyClass);
    if (std::find(classes.begin(), classes.end(), drawn) == classes.end()) {
      classes.push_back(drawn);
    }
  }
  std::sort(classes.begin(), classes.end());
  lucent::Line line;
  line.add("drawn classes ({}):", classes.size());
  for (const uint32_t drawn : classes) {
    line.add(" {}", drawn);
  }
  line.flush_debug("actororacle");

  // The level's own Moby array, independently of what was drawn. "No gem appeared" has two very
  // different causes — no gem is in the level, or a gem is in the level and the port did not draw
  // it — and only the second is a port fault. Reporting the drawn set alone cannot tell them apart.
  const uint32_t first = core->mem_r32(kLevelMobys);
  if (!ramSpan(first, kMobyBytes)) {
    lucent::debug("actororacle", "level moby array unreadable at 0x{:08X}", first);
    return;
  }
  std::vector<uint32_t> present;
  uint32_t scanned = 0;
  for (uint32_t i = 0, moby = first; i < kMaxLevelMobys; ++i, moby += kMobyBytes) {
    if (!ramSpan(moby, kMobyBytes)) {
      break;
    }
    const uint32_t state = core->mem_r32(moby + kMobyState);
    if ((int8_t)state < 0) {
      if ((state & 0xffu) == 0xffu) {
        break;
      }
      continue;
    }
    ++scanned;
    const uint32_t held = core->mem_r16(moby + kMobyClass);
    if (std::find(present.begin(), present.end(), held) == present.end()) {
      present.push_back(held);
    }
  }
  std::sort(present.begin(), present.end());
  lucent::Line level;
  level.add("level moby classes (live={} distinct={}):", scanned, present.size());
  for (const uint32_t held : present) {
    level.add(
        " {}{}", held, std::find(classes.begin(), classes.end(), held) == classes.end() ? "" : "*");
  }
  level.add("   (* = drawn this frame)");
  level.flush_debug("actororacle");

  if (wantedClass < 0) {
    return;
  }
  // Where the instances under investigation actually are, relative to the camera, so a run that
  // never draws one says whether it was culled at a plausible distance or lost.
  const int32_t cameraX = (int32_t)core->mem_r32(kCamera + 40u);
  const int32_t cameraY = (int32_t)core->mem_r32(kCamera + 44u);
  const int32_t cameraZ = (int32_t)core->mem_r32(kCamera + 48u);
  for (uint32_t i = 0, moby = first; i < kMaxLevelMobys; ++i, moby += kMobyBytes) {
    if (!ramSpan(moby, kMobyBytes)) {
      break;
    }
    const uint32_t state = core->mem_r32(moby + kMobyState);
    if ((int8_t)state < 0) {
      if ((state & 0xffu) == 0xffu) {
        break;
      }
      continue;
    }
    if ((int)core->mem_r16(moby + kMobyClass) != wantedClass) {
      continue;
    }
    lucent::debug("actororacle",
                  "class {} instance 0x{:08X} pos=({},{},{}) from_camera=({},{},{})",
                  wantedClass,
                  moby,
                  (int32_t)core->mem_r32(moby + 12u),
                  (int32_t)core->mem_r32(moby + 16u),
                  (int32_t)core->mem_r32(moby + 20u),
                  (int32_t)core->mem_r32(moby + 12u) - cameraX,
                  (int32_t)core->mem_r32(moby + 16u) - cameraY,
                  (int32_t)core->mem_r32(moby + 20u) - cameraZ);
  }
}

// Is a Moby of the requested class among the instances the native producers just drew?
//
// The oracle otherwise dumps whichever frame the driver happens to settle on, and a fault in an
// object that is not on screen cannot appear in it — the first Artisans capture contained no gem at
// all, so it could not have shown a gem fault whether or not one exists. Gating on the class turns
// "walk around and hope" into "dump the frame that contains the thing under investigation".
bool drawsClass(Core *core, int wantedClass) {
  const RenderQueue &queue = core->game->rq;
  for (int i = 0; i < queue.n; ++i) {
    const uint32_t node = queue.items[i].dbg_node;
    if (node != 0u && ramSpan(node, kMobyBytes) &&
        (int)core->mem_r16(node + kMobyClass) == wantedClass) {
      return true;
    }
  }
  return false;
}

} // namespace

void compare(Core *core,
             uint32_t retailBody,
             std::span<const uint32_t> nativePainterKeys,
             const char *site) {
  if (core == nullptr || !cfg_on("PSXPORT_ACTOR_SCENE_ORACLE")) {
    return;
  }
  // PSXPORT_ACTOR_SCENE_ORACLE_CLASS holds the dump until a Moby of that class is on screen; the
  // Spyro 1 gems are classes 83, 84, 85, 86 and 87. Unset means dump every frame.
  const int wantedClass = cfg_int("PSXPORT_ACTOR_SCENE_ORACLE_CLASS", -1);
  // Printed on every armed frame, before the gate, so a run that never dumps still says WHICH
  // classes it did draw. Without it a zero-dump run is indistinguishable from a broken filter.
  logDrawnClasses(core, wantedClass);
  if (wantedClass >= 0 && !drawsClass(core, wantedClass)) {
    return;
  }
  logNative(core, nativePainterKeys);

  const uint32_t otBase = core->mem_r32(kOtPointer);
  const uint32_t savedCursor = core->mem_r32(kPoolCursor);
  const uint32_t savedCount = core->mem_r32(kPacketCount);
  if (!ramSpan(otBase, kOtBins * 8u)) {
    lucent::debug("actororacle", "retail side REFUSED at {}: invalid_ot", site);
    return;
  }
  std::vector<uint32_t> savedOt(kOtBins * 2u);
  for (uint32_t word = 0; word < savedOt.size(); ++word) {
    savedOt[word] = core->mem_r32(kseg(otBase + word * 4u));
  }

  psx::cpu::dispatchGuestToReturn0(
      *core, retailBody, psx::cpu::ExecutionBudget::currentTurn(*core), site);

  std::vector<Retail> retail;
  const char *refusal = "none";
  const bool walked = walkOt(core, savedCursor, retail, refusal);

  for (uint32_t word = 0; word < savedOt.size(); ++word) {
    core->mem_w32(kseg(otBase + word * 4u), savedOt[word]);
  }
  core->mem_w32(kPoolCursor, savedCursor);
  core->mem_w32(kPacketCount, savedCount);

  for (uint32_t index = 0; index < retail.size(); ++index) {
    const gpu_packet_decode::Packet &p = retail[index].packet;
    lucent::debug("actororacle",
                  "retail rec={} bin={} code={:02X} nv={} semi={} v0={},{},{:06X} "
                  "v1={},{},{:06X} v2={},{},{:06X} v3={},{},{:06X}",
                  index,
                  retail[index].bin,
                  p.code,
                  p.vertexCount,
                  p.semiTransparent ? 1 : 0,
                  p.vertices[0].sx,
                  p.vertices[0].sy,
                  p.vertices[0].rgb,
                  p.vertices[1].sx,
                  p.vertices[1].sy,
                  p.vertices[1].rgb,
                  p.vertices[2].sx,
                  p.vertices[2].sy,
                  p.vertices[2].rgb,
                  p.vertices[3].sx,
                  p.vertices[3].sy,
                  p.vertices[3].rgb);
  }
  lucent::debug("actororacle",
                "retail side: body=0x{:08X} walked={} refusal={} decoded={} pool_from=0x{:08X}",
                retailBody,
                walked,
                refusal,
                retail.size(),
                savedCursor);
}

} // namespace spyro::actor_scene_oracle
