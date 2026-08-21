// Native ownership of PsyQ InitSpuHardware (0x8005BBF4).
//
// Ground truth is SCUS_942.28 0x8005BBF4..0x8005BE87 (165 instructions). Ghidra and both
// byte-matching CC0 Spyro references identify the same two-mode hardware reset. Its only direct
// children -- calibrated spin 0x8005C720, printf 0x8006279C, and WriteSpuRamPio 0x8005BE88 -- are
// already owned and remain separate dispatch boundaries. The generated parent stays compiled as
// the per-call differential oracle.
#include "core.h"
#include "native_diff.h"
#include "rec_decls.h"
#include "recomp_iface.h"
#include "spu_hardware_init.h"
#include "spyro_game.h"

namespace {

constexpr uint32_t kSpuBasePtr = 0x80073554u;
constexpr uint32_t kDmaControlPtr = 0x80073564u;
constexpr uint32_t kTransferAddressShadow = 0x8007356Cu;
constexpr uint32_t kTransferState0 = 0x80073570u;
constexpr uint32_t kTransferState1 = 0x80073574u;
constexpr uint32_t kAddressAlignEnable = 0x80073578u;
constexpr uint32_t kAddressShift = 0x8007357Cu;
constexpr uint32_t kAddressAlignUnit = 0x80073580u;
constexpr uint32_t kAddressAlignMask = 0x80073584u;
constexpr uint32_t kTransferDone = 0x80073588u;
constexpr uint32_t kTransferState2 = 0x8007358Cu;
constexpr uint32_t kTransferState3 = 0x80073590u;
constexpr uint32_t kSilentSample = 0x80073594u;
constexpr uint32_t kPendingPitchTable = 0x800777A8u;
constexpr uint32_t kTimeoutFormat = 0x80011534u;
constexpr uint32_t kResetTimeoutMessage = 0x80011544u;

constexpr uint32_t kMainVolumeLeft = 0x180u;
constexpr uint32_t kMainVolumeRight = 0x182u;
constexpr uint32_t kKeyOnLow = 0x188u;
constexpr uint32_t kKeyOnHigh = 0x18Au;
constexpr uint32_t kKeyOffLow = 0x18Cu;
constexpr uint32_t kKeyOffHigh = 0x18Eu;
constexpr uint32_t kPitchModLow = 0x190u;
constexpr uint32_t kPitchModHigh = 0x192u;
constexpr uint32_t kNoiseLow = 0x194u;
constexpr uint32_t kNoiseHigh = 0x196u;
constexpr uint32_t kReverbLow = 0x198u;
constexpr uint32_t kReverbHigh = 0x19Au;
constexpr uint32_t kControl = 0x1AAu;
constexpr uint32_t kTransferMode = 0x1ACu;
constexpr uint32_t kStatus = 0x1AEu;
constexpr uint32_t kCdVolumeLeft = 0x1B0u;
constexpr uint32_t kCdVolumeRight = 0x1B2u;
constexpr uint32_t kExternalVolumeLeft = 0x1B4u;
constexpr uint32_t kExternalVolumeRight = 0x1B6u;

constexpr uint32_t kDmaInterruptMask = 0xB0000u;
constexpr uint32_t kStatusMask = 0x7FFu;
constexpr uint32_t kTimeoutIterations = 0xF00u;
constexpr uint32_t kVoiceStride = 16u;

void delaySpuRegisterWrite(Core *c, uint32_t returnPc) {
  c->r[31] = returnPc;
  func_8005C720(c);
}

void reportResetTimeout(Core *c) {
  c->r[4] = kTimeoutFormat;
  c->r[5] = kResetTimeoutMessage;
  c->r[31] = 0x8005BCA4u;
  func_8006279C(c);
}

void initSpuHardwareNative(Core *c) {
  c->r[29] -= 32u;
  c->mem_w32(c->r[29] + 16u, c->r[16]);
  c->r[16] = c->r[4];
  c->r[4] = c->mem_r32(kDmaControlPtr);
  c->mem_w32(c->r[29] + 24u, c->r[31]);
  c->mem_w32(c->r[29] + 20u, c->r[17]);

  c->r[2] = c->mem_r32(c->r[4]);
  c->r[3] = kDmaInterruptMask;
  c->r[2] |= c->r[3];
  c->mem_w32(c->r[4], c->r[2]);
  c->r[2] = c->mem_r32(kSpuBasePtr);
  c->r[1] = 0x80070000u;
  c->mem_w32(kTransferState0, 0u);
  c->r[1] = 0x80070000u;
  c->mem_w32(kTransferState1, 0u);
  c->r[1] = 0x80070000u;
  c->mem_w16(kTransferAddressShadow, 0u);
  c->mem_w16(c->r[2] + kMainVolumeLeft, 0u);
  c->mem_w16(c->r[2] + kMainVolumeRight, 0u);
  c->mem_w16(c->r[2] + kControl, 0u);
  delaySpuRegisterWrite(c, 0x8005BC54u);

  c->r[2] = c->mem_r32(kSpuBasePtr);
  c->mem_w16(c->r[2] + kMainVolumeLeft, 0u);
  c->mem_w16(c->r[2] + kMainVolumeRight, 0u);
  c->r[2] = c->mem_r16(c->r[2] + kStatus) & kStatusMask;
  c->r[3] = 0u;
  if (c->r[2] != 0u) {
    ++c->r[3];
    while (true) {
      if (c->pending_work) {
        rec_irq_poll(c);
      }
      c->r[2] = static_cast<uint32_t>(c->r[3] < kTimeoutIterations + 1u);
      if (c->r[2] == 0u) {
        reportResetTimeout(c);
        break;
      }
      c->r[2] = c->mem_r32(kSpuBasePtr);
      c->r[2] = c->mem_r16(c->r[2] + kStatus) & kStatusMask;
      ++c->r[3];
      if (c->r[2] == 0u) {
        break;
      }
    }
  }

  c->r[4] = 0u;
  c->r[5] = kPendingPitchTable;
  c->r[2] = 2u;
  c->r[1] = 0x80070000u;
  c->mem_w32(kAddressAlignEnable, c->r[2]);
  c->r[2] = 3u;
  c->r[1] = 0x80070000u;
  c->mem_w32(kAddressShift, c->r[2]);
  c->r[2] = 8u;
  c->r[1] = 0x80070000u;
  c->mem_w32(kAddressAlignUnit, c->r[2]);
  c->r[2] = 7u;
  c->r[1] = 0x80070000u;
  c->mem_w32(kAddressAlignMask, c->r[2]);
  c->r[2] = c->mem_r32(kSpuBasePtr);
  c->r[3] = 4u;
  c->mem_w16(c->r[2] + kTransferMode, static_cast<uint16_t>(c->r[3]));
  c->r[3] = 0xFFFFu;
  c->mem_w16(c->r[2] + kMainVolumeLeft + 4u, 0u);
  c->mem_w16(c->r[2] + kMainVolumeRight + 4u, 0u);
  c->mem_w16(c->r[2] + kKeyOffLow, static_cast<uint16_t>(c->r[3]));
  c->mem_w16(c->r[2] + kKeyOffHigh, static_cast<uint16_t>(c->r[3]));
  c->mem_w16(c->r[2] + kReverbLow, 0u);
  c->mem_w16(c->r[2] + kReverbHigh, 0u);

  while (true) {
    if (c->pending_work) {
      rec_irq_poll(c);
    }
    c->mem_w16(c->r[5], 0u);
    ++c->r[4];
    c->r[2] = static_cast<uint32_t>(static_cast<int32_t>(c->r[4]) <
                                    static_cast<int32_t>(spyro::kSpuPendingPitchCount));
    c->r[5] += 2u;
    if (c->r[2] == 0u) {
      break;
    }
  }

  c->r[2] = 0u;
  if (spyro::spuHardwareNeedsFullReset(c->r[16])) {
    c->r[4] = kSilentSample;
    c->r[2] = c->mem_r32(kSpuBasePtr);
    c->r[3] = 0x200u;
    c->r[1] = 0x80070000u;
    c->mem_w16(kTransferAddressShadow, static_cast<uint16_t>(c->r[3]));
    c->mem_w16(c->r[2] + kPitchModLow, 0u);
    c->mem_w16(c->r[2] + kPitchModHigh, 0u);
    c->mem_w16(c->r[2] + kNoiseLow, 0u);
    c->mem_w16(c->r[2] + kNoiseHigh, 0u);
    c->mem_w16(c->r[2] + kCdVolumeLeft, 0u);
    c->mem_w16(c->r[2] + kCdVolumeRight, 0u);
    c->mem_w16(c->r[2] + kExternalVolumeLeft, 0u);
    c->mem_w16(c->r[2] + kExternalVolumeRight, 0u);
    c->r[31] = 0x8005BD94u;
    c->r[5] = 0x10u;
    func_8005BE88(c);

    constexpr spyro::SpuVoiceReset reset = spyro::spuVoiceReset();
    c->r[4] = 0u;
    c->r[6] = reset.pitch;
    c->r[5] = reset.startAddress;
    c->r[3] = c->mem_r32(kSpuBasePtr);
    while (true) {
      if (c->pending_work) {
        rec_irq_poll(c);
      }
      c->mem_w16(c->r[3] + 0u, reset.volumeLeft);
      c->mem_w16(c->r[3] + 2u, reset.volumeRight);
      c->mem_w16(c->r[3] + 4u, static_cast<uint16_t>(c->r[6]));
      c->mem_w16(c->r[3] + 6u, static_cast<uint16_t>(c->r[5]));
      c->mem_w16(c->r[3] + 8u, reset.adsr1);
      c->mem_w16(c->r[3] + 10u, reset.adsr2);
      ++c->r[4];
      c->r[2] = static_cast<uint32_t>(static_cast<int32_t>(c->r[4]) <
                                      static_cast<int32_t>(spyro::kSpuVoiceCount));
      c->r[3] += kVoiceStride;
      if (c->r[2] == 0u) {
        break;
      }
    }

    c->r[17] = 0xFFFFu;
    c->r[2] = c->mem_r32(kSpuBasePtr);
    c->r[16] = 0xFFu;
    c->mem_w16(c->r[2] + kKeyOnLow, static_cast<uint16_t>(c->r[17]));
    c->mem_w16(c->r[2] + kKeyOnHigh, static_cast<uint16_t>(c->r[16]));
    delaySpuRegisterWrite(c, 0x8005BDF4u);
    delaySpuRegisterWrite(c, 0x8005BDFCu);
    delaySpuRegisterWrite(c, 0x8005BE04u);
    delaySpuRegisterWrite(c, 0x8005BE0Cu);
    c->r[2] = c->mem_r32(kSpuBasePtr);
    c->mem_w16(c->r[2] + kKeyOffLow, static_cast<uint16_t>(c->r[17]));
    c->mem_w16(c->r[2] + kKeyOffHigh, static_cast<uint16_t>(c->r[16]));
    delaySpuRegisterWrite(c, 0x8005BE28u);
    delaySpuRegisterWrite(c, 0x8005BE30u);
    delaySpuRegisterWrite(c, 0x8005BE38u);
    delaySpuRegisterWrite(c, 0x8005BE40u);
    c->r[2] = 0u;
  }

  c->r[4] = c->mem_r32(kSpuBasePtr);
  c->r[3] = 1u;
  c->r[1] = 0x80070000u;
  c->mem_w32(kTransferDone, c->r[3]);
  c->r[3] = 0xC000u;
  c->mem_w16(c->r[4] + kControl, static_cast<uint16_t>(c->r[3]));
  c->r[1] = 0x80070000u;
  c->mem_w32(kTransferState2, 0u);
  c->r[1] = 0x80070000u;
  c->mem_w32(kTransferState3, 0u);

  c->r[31] = c->mem_r32(c->r[29] + 24u);
  c->r[17] = c->mem_r32(c->r[29] + 20u);
  c->r[16] = c->mem_r32(c->r[29] + 16u);
  c->r[29] += 32u;
}

void initSpuHardwareOwned(Core *c) {
  ndiff_run(c, "spu-init@0x8005BBF4", initSpuHardwareNative, gen_func_8005BBF4);
}

} // namespace

void spyro_register_native_spu_hardware_init() {
  psxport_recomp()->shard_set_override(0x8005BBF4u, initSpuHardwareOwned);
}
