// spyro_game.h — declarations for the Spyro game-side seam installers.
//
// The framework never includes this; it is purely how our own TUs (game_config.cpp, game_hooks.cpp,
// recomp_register.cpp, main.cpp) reach each other.
#pragma once

struct GameHooks;
class Game;

const GameHooks* spyro_game_hooks();     // game/core/game_hooks.cpp — the (mostly null) Phase-0 vtable
void spyro_install_game_config();        // game/core/game_config.cpp — installs GameConfig + hooks
void spyro_install_recomp();             // game/core/recomp_register.cpp — installs the generated substrate

// game/core/vsync.cpp — installs the vblank timebase (the libetc wait helper).
void spyro_register_vsync(Game* g);

// game/core/cd_queue.cpp — observes (and will own) the CD request queue service routine.
void spyro_register_cd_queue();
// TEMPORARY (docs/issues/0017): probes on the level-overlay load chain. Remove once the load fires.

// The first guest function this port OWNS outright (native_rand.cpp) — the recompiled body never
// runs once installed. Verified per call against that body under PSXPORT_NDIFF.
void spyro_register_native_rand();

// Hot leaf functions owned outright (native_leaf.cpp), chosen with tools/own_candidates.py.
void spyro_register_native_leaves();

// 3-word vector add/sub and the libgte angle-table interpolator (native_vec.cpp).
void spyro_register_native_vec();

// Geometry leaves that use the GTE (native_gte.cpp) — scalar logic native, COP2 via the platform model.
void spyro_register_native_gte();

// native_angle.cpp — the engine's 8-bit/12-bit angle helpers and its calibrated spin loop.
void spyro_register_native_angle();

// native_util.cpp — strlen, two set-and-return-previous globals, 2D distance, display-list link.
void spyro_register_native_util();
void spyro_register_native_terrain();
void spyro_register_native_render();

// native_angle.cpp — the engine's 8-bit/12-bit angle helpers and its calibrated spin loop.
void spyro_register_native_angle();
