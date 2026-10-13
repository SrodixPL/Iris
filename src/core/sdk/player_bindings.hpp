#pragma once
#include "core/sdk/binding_values.hpp"
#include <cstdint>
namespace iris::sdk::player_bindings {
inline auto& preview_player_vtable=iris::sdk::binding_values::player_preview_player_vtable;
inline auto& preview_player_alias_vtable=iris::sdk::binding_values::player_preview_player_alias_vtable;
inline auto& camera_calculate_view=iris::sdk::binding_values::player_camera_calculate_view;
inline auto& camera_services_vtable=iris::sdk::binding_values::player_camera_services_vtable;
}
