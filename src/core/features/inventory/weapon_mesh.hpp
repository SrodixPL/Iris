#pragma once
#include <cstdint>
namespace iris::cosmetics::detail {
struct Runtime;
struct ChangedWeapon;
std::uint64_t weapon_mesh_mask(const ChangedWeapon& weapon);
void synchronize_weapon_mesh(Runtime& state, ChangedWeapon& weapon);
bool synchronize_hud_weapon_meshes(Runtime& state);
void restore_hud_weapon_meshes(Runtime& state);
void refresh_weapon_materials(Runtime& state, bool refreshed, std::uintptr_t pawn);
}
