#pragma once
#include "core/features/player/visual_settings.hpp"
#include "core/menu/menu_state.hpp"
#include "core/features/player/esp/elements.hpp"

namespace iris::ui::esp_preview {
enum class Element {Box,Name,Health,HealthNumber,Ammo,Armor,Weapon,Distance,Flags,Snapline,Skeleton,Hitboxes,Arrows,Sounds,Count};
struct State {Element selected{Element::Box};int target{};float yaw{},pitch{};bool rotating{},options_open{},element_options{};Bounds options_anchor;int dragging{-1};ImVec2 drag_offset{};};
// The texture must be owned by the menu upload cache and originate from copied
// RGBA scene pixels. Native entities and borrowed render targets never enter UI.
struct ModelFrame {
    std::uint64_t texture{};
    ImVec2 bounds_min{.35f,.12f},bounds_max{.65f,.84f};
    visuals::Player player;
    visuals::Matrix matrix{};
    visuals::Icon (*lookup)(void*,int){};
    void* icon_owner{};
};
// A drop returns one complete canonical layout for the caller's atomic queue.
// yaw/pitch are local view requests consumed by the game-thread scene owner.
bool draw(MenuRuntime& menu,const visuals::Settings& settings,State& state,visuals::EspLayout* updated_layout=nullptr,const ModelFrame* model=nullptr);
const char* label(Element element);
}
