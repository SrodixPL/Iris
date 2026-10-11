#pragma once
#include "core/features/player/esp/drawing.hpp"

namespace iris::visuals::esp {
struct ElementMeasure {ImVec2 size{};bool enabled{},bar{};};
struct ElementRect {ImVec2 min{},max{};bool drawn{};};
using ElementRects=std::array<ElementRect,esp_element_count>;
ElementRects layout_elements(const EspLayout& layout,const std::array<ElementMeasure,esp_element_count>& measures,ImVec2 min,ImVec2 max,float gap=4);
// A null draw list measures without drawing or mutating the layout.
// Both the live overlay and copied-data preview call this exact measurement,
// layout and drawing path. Geometry overlays remain attached to the model.
ElementRects draw_elements(ImDrawList* draw,const Player& player,const Settings& settings,ImVec2 min,ImVec2 max,Icon (*lookup)(void*,int)=nullptr,void* owner=nullptr);
}
