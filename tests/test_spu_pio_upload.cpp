#include "spu_pio_upload.h"

#include <cstdio>
#include <cstdlib>

namespace {

void require(bool condition, const char *what) {
  if (!condition) {
    std::fprintf(stderr, "spu_pio_upload: %s\n", what);
    std::abort();
  }
}

} // namespace

int main() {
  require(spyro::spuPioBatchBytes(0) == 0 && spyro::spuPioBatchBytes(1) == 1,
          "zero/single-byte batches changed");
  require(spyro::spuPioBatchBytes(63) == 63 && spyro::spuPioBatchBytes(64) == 64,
          "short/full batch boundary changed");
  require(spyro::spuPioBatchBytes(65) == 64 && spyro::spuPioBatchBytes(0x1000) == 64,
          "oversize input no longer clamps to the binary's 0x40-byte batch");
  require(spyro::spuPioHalfwordCount(0) == 0 && spyro::spuPioHalfwordCount(1) == 1 &&
              spyro::spuPioHalfwordCount(63) == 32 && spyro::spuPioHalfwordCount(64) == 32,
          "halfword stream count no longer matches the binary's two-byte loop step");
  return 0;
}
