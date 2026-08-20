// native_printf.cpp — own PsyQ printf's non-leaf argument-homing wrapper.
//
// This is the first non-leaf ownership step. The wrapper itself is game code at 0x8006279C;
// FormatAndWrite at 0x800627D8 remains independently dispatchable and recompiled. Keeping that
// call boundary is deliberate: non-leaf ownership grows bottom-up without deleting the generated
// oracle or pretending the formatter has already been reimplemented.
//
// Ground truth is the SCUS_942.28 image, bytes 0x52F9C..0x52FD7 (15 instructions), independently
// named `printf` by the vendored disassembly. It homes a1/a2/a3 above its 24-byte frame, then calls
// FormatAndWrite(1, format, &first_vararg). The precise stack writes, jal return address, and final
// scratch-register state are ABI, so PSXPORT_NDIFF compares this body with the retained generated
// body on real calls.
#include "core.h"
#include "native_diff.h"
#include "rec_decls.h"
#include "recomp_iface.h"
#include "spyro_game.h"

namespace {

void write_printf_native(Core *c) {
  c->r[29] -= 24; // addiu sp, sp, -0x18

  c->r[2] = c->r[4]; // v0 = format
  c->r[4] = 1;       // a0 = stdout file descriptor
  c->mem_w32(c->r[29] + 28, c->r[5]);
  c->r[5] = c->r[2]; // a1 = format
  c->mem_w32(c->r[29] + 32, c->r[6]);
  c->r[6] = c->r[29] + 28; // a2 = first homed vararg
  c->mem_w32(c->r[29] + 16, c->r[31]);
  c->mem_w32(c->r[29] + 24, c->r[2]);
  c->r[31] = 0x800627C8u;
  c->mem_w32(c->r[29] + 36, c->r[7]); // jal delay slot
  func_800627D8(c);

  c->r[31] = c->mem_r32(c->r[29] + 16);
  c->r[29] += 24;
}

void write_printf_owned(Core *c) {
  ndiff_run(c, "printf@0x8006279C", write_printf_native, gen_func_8006279C);
}

} // namespace

void spyro_register_native_printf() {
  psxport_recomp()->shard_set_override(0x8006279Cu, write_printf_owned);
}
