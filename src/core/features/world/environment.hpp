#pragma once
#include <cstdint>
namespace iris::cosmetics::detail {
struct Runtime;
struct EnvironmentEntity;
void update_skyboxes(Runtime& state,EnvironmentEntity& saved);
void update_postprocessing(Runtime& state,EnvironmentEntity& saved);
void update_smoke(Runtime& state,EnvironmentEntity& saved,std::uint32_t handle);
void update_flash(Runtime& state,EnvironmentEntity& saved);
void bind_flash(Runtime& state);
void update_wind(Runtime& state,EnvironmentEntity& saved);
}
