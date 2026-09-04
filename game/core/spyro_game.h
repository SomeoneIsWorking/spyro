#pragma once

class Core;

// Image-scoped native owners preserved from earlier verified work. Each installer
// binds the verified handwritten body to the image that currently owns its guest address.
void spyro_register_native_rand(Core &core);
void spyro_register_native_leaves(Core &core);
void spyro_register_native_vec(Core &core);
void spyro_register_native_gte(Core &core);
void spyro_register_native_angle(Core &core);
void spyro_register_native_util(Core &core);
void spyro_register_native_gameplay(Core &core);
