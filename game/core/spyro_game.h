#pragma once

#include <array>
#include <cstdint>

class Core;

// Image-scoped native owners preserved from earlier verified work. Each installer
// binds the verified handwritten body to the image that currently owns its guest address.
void spyro_register_native_rand(Core &core);
void spyro_register_native_leaves(Core &core);
void spyro_register_native_vec(Core &core);
void spyro_register_native_gte(Core &core);
void spyro_register_native_angle(Core &core);
void spyro_register_native_util(Core &core);
void spyro_register_cd_queue(Core &core);
bool spyro_terrain_submit(Core *c, int32_t selector, uint32_t matrix1, uint32_t matrix2);
// The same renderer for a caller that BUILT its matrices rather than finding them in guest RAM.
// func_8001A050 does exactly that while a level entrance sweep is still winding down.
bool spyro_terrain_submit_matrices(Core *c,
                                   int32_t selector,
                                   const std::array<uint32_t, 5> &view,
                                   const std::array<uint32_t, 5> &projection);
