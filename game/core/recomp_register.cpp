// recomp_register.cpp — fills psxport's framework↔generated-substrate seam (recomp_iface.h) from
// THIS game's generated symbols.
//
// The framework must not name generated/ symbols directly (that is what lets libpsxport.a build and
// link standalone). It reaches the recompiled game through psxport_recomp()->field instead, and this
// file is the ONE place those generated symbols are named — game code referring to generated code,
// which is fine; the coupling the seam breaks is framework→generated.
#include "core.h"
#include "recomp_iface.h"
#include "overlay_table.h"   // generated: main_dispatch, g_rec_overlays, g_rec_overlay_count
#include "spyro_game.h"

// The generated MAIN-module override setter, declared with the exact signature the recompiler emits.
extern void shard_set_override(uint32_t, void (*)(Core*));   // generated/shard_disp.c

static const RecompRegistry g_spyro_recomp = {
  /* main_dispatch        */ main_dispatch,
  /* rec_func_index       */ rec_func_index,
  // Spyro has NO code overlays — one executable, all data in WAD.WAD. emit.py still emits an (empty)
  // overlay table, so these stay wired to it and g_rec_overlay_count is 0; overlay_router simply
  // never matches an address.
  /* overlays             */ g_rec_overlays,
  /* overlay_count        */ g_rec_overlay_count,
  /* shard_set_override   */ shard_set_override,
  // Tomba!2-shaped members: setters for ITS overlay modules. Spyro emits no overlay modules, so there
  // is no such symbol to point at. (That these are named per-overlay in a game-agnostic struct is a
  // residual framework wart — see docs/codemap.md.)
  /* ov_a00_set_override  */ nullptr,
  /* ov_game_set_override */ nullptr,
  // The guest-memset gen body is a Tomba address (gen_func_8009A420) used as a fast path. Spyro's
  // equivalent has not been RE'd, so leave it null and let the guest's own memset run recompiled.
  /* guestMemset_gen      */ nullptr,
};

void spyro_install_recomp() { psxport_install_recomp(&g_spyro_recomp); }
