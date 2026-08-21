// Native ownership of PsyQ WriteSpuRamPio (0x8005BE88).
//
// Ground truth is SCUS_942.28 0x8005BE88..0x8005C053 (115 instructions), independently named
// and byte-matched by both vendored Spyro references. Its two direct children, calibrated spin
// 0x8005C720 and printf 0x8006279C, are already owned and remain separate dispatch boundaries.
// The generated parent remains compiled as the per-call differential oracle.
#include "core.h"
#include "native_diff.h"
#include "rec_decls.h"
#include "recomp_iface.h"
#include "spu_pio_upload.h"
#include "spyro_game.h"

namespace {

constexpr uint32_t kSpuBasePtr = 0x80073554u;
constexpr uint32_t kTransferAddressShadow = 0x8007356Cu;
constexpr uint32_t kTimeoutFormat = 0x80011534u;
constexpr uint32_t kWriteReadyTimeout = 0x80011554u;
constexpr uint32_t kDmaClearTimeout = 0x80011568u;

constexpr uint32_t kTransferAddress = 0x1A6u;
constexpr uint32_t kTransferData = 0x1A8u;
constexpr uint32_t kControl = 0x1AAu;
constexpr uint32_t kStatus = 0x1AEu;
constexpr uint32_t kControlModeMask = 0xFFCFu;
constexpr uint32_t kPioWriteMode = 0x10u;
constexpr uint32_t kWriteFifoBusy = 0x400u;
constexpr uint32_t kStatusMask = 0x7FFu;
constexpr uint32_t kTimeoutIterations = 0xF00u;

void spin(Core *c, uint32_t returnPc) {
  c->r[31] = returnPc;
  func_8005C720(c);
}

void reportTimeout(Core *c, uint32_t message, uint32_t returnPc) {
  c->r[4] = kTimeoutFormat;
  c->r[5] = message;
  c->r[31] = returnPc;
  func_8006279C(c);
}

void writeSpuRamPioNative(Core *c) {
  c->r[2] = c->mem_r32(kSpuBasePtr);
  c->r[3] = c->mem_r16(kTransferAddressShadow);
  c->r[29] -= 48;
  c->mem_w32(c->r[29] + 28, c->r[17]);
  c->r[17] = c->r[5];
  c->mem_w32(c->r[29] + 40, c->r[31]);
  c->mem_w32(c->r[29] + 36, c->r[19]);
  c->mem_w32(c->r[29] + 32, c->r[18]);
  c->mem_w32(c->r[29] + 24, c->r[16]);
  c->r[5] = c->mem_r16(c->r[2] + kStatus);
  c->r[18] = c->r[4];
  c->mem_w16(c->r[2] + kTransferAddress, static_cast<uint16_t>(c->r[3]));
  c->r[19] = c->r[5] & kStatusMask;
  spin(c, 0x8005BEC8u);

  c->r[2] = static_cast<uint32_t>(c->r[17] < spyro::kSpuPioBatchBytes + 1u);
  while (c->r[17] != 0) {
    if (c->pending_work) {
      rec_irq_poll(c);
    }

    c->r[16] = spyro::spuPioBatchBytes(c->r[17]);
    c->r[3] = 0;
    c->r[4] = c->mem_r32(kSpuBasePtr);
    for (uint32_t word = 0; word < spyro::spuPioHalfwordCount(c->r[16]); ++word) {
      if (c->pending_work) {
        rec_irq_poll(c);
      }
      c->r[2] = c->mem_r16(c->r[18]);
      c->r[18] += 2;
      c->r[3] += 2;
      c->mem_w16(c->r[4] + kTransferData, static_cast<uint16_t>(c->r[2]));
      c->r[2] =
          static_cast<uint32_t>(static_cast<int32_t>(c->r[3]) < static_cast<int32_t>(c->r[16]));
    }

    c->r[3] = c->mem_r32(kSpuBasePtr);
    c->r[4] = c->mem_r16(c->r[3] + kControl);
    c->r[2] = c->r[4] & kControlModeMask;
    c->r[4] = c->r[2] | kPioWriteMode;
    c->mem_w16(c->r[3] + kControl, static_cast<uint16_t>(c->r[4]));
    spin(c, 0x8005BF30u);

    c->r[2] = c->mem_r32(kSpuBasePtr);
    c->r[2] = c->mem_r16(c->r[2] + kStatus);
    c->r[2] &= kWriteFifoBusy;
    c->r[3] = 0;
    if (c->r[2] != 0) {
      ++c->r[3];
      while (true) {
        if (c->pending_work) {
          rec_irq_poll(c);
        }
        c->r[2] = static_cast<uint32_t>(c->r[3] < kTimeoutIterations + 1u);
        if (c->r[2] == 0) {
          reportTimeout(c, kWriteReadyTimeout, 0x8005BF78u);
          break;
        }
        c->r[2] = c->mem_r32(kSpuBasePtr);
        c->r[2] = c->mem_r16(c->r[2] + kStatus);
        c->r[2] &= kWriteFifoBusy;
        ++c->r[3];
        if (c->r[2] == 0) {
          break;
        }
      }
    }

    c->r[17] -= c->r[16];
    spin(c, 0x8005BFA8u);
    spin(c, 0x8005BFB0u);
    c->r[2] = static_cast<uint32_t>(c->r[17] < spyro::kSpuPioBatchBytes + 1u);
  }

  c->r[2] = c->mem_r32(kSpuBasePtr);
  c->r[4] = c->mem_r16(c->r[2] + kControl);
  c->r[3] = 0;
  c->r[4] &= kControlModeMask;
  c->mem_w16(c->r[2] + kControl, static_cast<uint16_t>(c->r[4]));
  c->r[2] = c->mem_r16(c->r[2] + kStatus);
  c->r[5] = c->r[19] & 0xFFFFu;
  c->r[2] &= kStatusMask;
  ++c->r[3];
  while (c->r[2] != c->r[5]) {
    if (c->pending_work) {
      rec_irq_poll(c);
    }
    c->r[2] = static_cast<uint32_t>(c->r[3] < kTimeoutIterations + 1u);
    if (c->r[2] == 0) {
      reportTimeout(c, kDmaClearTimeout, 0x8005C00Cu);
      break;
    }
    c->r[2] = c->mem_r32(kSpuBasePtr);
    c->r[2] = c->mem_r16(c->r[2] + kStatus);
    c->r[2] &= kStatusMask;
    ++c->r[3];
  }

  c->r[31] = c->mem_r32(c->r[29] + 40);
  c->r[19] = c->mem_r32(c->r[29] + 36);
  c->r[18] = c->mem_r32(c->r[29] + 32);
  c->r[17] = c->mem_r32(c->r[29] + 28);
  c->r[16] = c->mem_r32(c->r[29] + 24);
  c->r[29] += 48;
}

void writeSpuRamPioOwned(Core *c) {
  ndiff_run(c, "spu-pio@0x8005BE88", writeSpuRamPioNative, gen_func_8005BE88);
}

} // namespace

void spyro_register_native_spu_pio_upload() {
  psxport_recomp()->shard_set_override(0x8005BE88u, writeSpuRamPioOwned);
}
