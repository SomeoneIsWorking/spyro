#pragma once

class Core;

// Diagnostic-only retained shadow capture. Arm it with PSXPORT_SHADOW_ORACLE=1. It runs the two
// source-owned shadow consumers against the current native FIELD state, records their packet
// geometry, depth buckets, and linked replay chains, and restores guest state before the shipping
// renderer continues. No native draw is produced.
void spyro_shadow_oracle_capture(Core *core);
