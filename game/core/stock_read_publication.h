#pragma once

#include "cd_stock_read_completion.h"

class Core;

namespace spyro {

// What a finished synchronous libcd `CdRead` means to Spyro 2 and Spyro 3: the guest streams its
// code modules and level data through the framework's stock read, and calls into whatever landed,
// so every landing is published as a resident image identified by the SHA-256 of the bytes now in
// RAM. Without this the dispatcher finds no image at the callee and refuses it as an ambiguous
// identity.
//
// One publication per whole read, made after the bytes are written. A module replaced by a later
// read at the same address is a new generation and the old one stops resolving; a read that landed
// nothing publishes nothing. A landing that cannot be published (outside main RAM, or no digest
// provider) requests a runtime fault rather than leaving guest code to run under no identity.
void publishStockReadLanding(Core &core, const psx::cd::StockReadLanding &landing);

} // namespace spyro
