#pragma once
#include "core/sdk/binding_values.hpp"
#include "core/sdk/offsets/build.hpp"
#include <cstdint>
#include <array>
namespace iris::sdk::world_bindings {
inline constexpr const char* scene_sha256=iris::sdk::reviewed_build::scenesystem_sha256;
inline constexpr const char* engine_sha256=iris::sdk::reviewed_build::engine2_sha256;
inline auto& shader_vector=iris::sdk::binding_values::world_shader_vector;
inline auto& shader_integer=iris::sdk::binding_values::world_shader_integer;
inline auto& clear_fog=iris::sdk::binding_values::world_clear_fog;
inline auto& render_smoke=iris::sdk::binding_values::world_render_smoke;
inline auto& post_vector=iris::sdk::binding_values::world_post_vector;
inline auto& aggregate=iris::sdk::binding_values::world_aggregate;
inline auto& light=iris::sdk::binding_values::world_light;
inline auto& aggregate_queue=iris::sdk::binding_values::world_aggregate_queue;
inline auto& draw_3d_sky=iris::sdk::binding_values::world_draw_3d_sky;
inline auto& ragdoll_limit=iris::sdk::binding_values::world_ragdoll_limit;
inline auto& shadows=iris::sdk::binding_values::world_shadows;
inline auto& light_color=iris::sdk::binding_values::world_light_color;
inline auto& integer_map=iris::sdk::binding_values::world_integer_map;
inline auto& aggregate_index=iris::sdk::binding_values::world_aggregate_index;
inline auto& record_stride=iris::sdk::binding_values::world_record_stride;
inline auto& smoke_prepare=iris::sdk::binding_values::world_smoke_prepare;
inline auto& smoke_color=iris::sdk::binding_values::world_smoke_color;
inline auto& smoke_state=iris::sdk::binding_values::world_smoke_state;
inline auto& smoke_state_color=iris::sdk::binding_values::world_smoke_state_color;
inline auto& read_shader_integer=iris::sdk::binding_values::world_read_shader_integer;
inline auto& permutation_hash=iris::sdk::binding_values::world_permutation_hash;
inline auto& particle_create=iris::sdk::binding_values::world_particle_create;
inline auto& particle_control=iris::sdk::binding_values::world_particle_control;
using ShaderValue=std::array<float,4>;
using SetShaderVector=std::uintptr_t(__fastcall*)(void*,std::uint32_t,const ShaderValue*);
using SetShaderInteger=std::uintptr_t(__fastcall*)(void*,std::uint32_t,int);
using GetShaderInteger=int(__fastcall*)(void*,std::uint32_t,int);
using ClearFog=std::uintptr_t(__fastcall*)(void*,void*);
using Aggregate=void(__fastcall*)(std::uintptr_t,std::uintptr_t,std::uintptr_t);
using Light=std::uintptr_t(__fastcall*)(std::uintptr_t,std::uintptr_t,std::uintptr_t);
using Smoke=void(__fastcall*)(std::uintptr_t,std::uintptr_t,int,int,std::uintptr_t,std::uintptr_t);
using SmokePrepare=void(__fastcall*)(std::uintptr_t);
}
