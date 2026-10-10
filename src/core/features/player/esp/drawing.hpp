#pragma once
#include "core/features/player/visuals.hpp"
#include <imgui.h>
namespace iris::visuals::esp {
ImVec2 point(Vector v);
void text(ImDrawList* d,ImVec2 at,ImU32 color,const char* value,bool center=false);
void bar(ImDrawList* d,ImVec2 min,ImVec2 max,float ratio,ImU32 color,bool vertical);
void flags(ImDrawList* d,const Player& p,const Settings& c,ImVec2 min,ImVec2 max,ImU32 color,float font);
void bones(ImDrawList* d,const Player& p,const Settings& c,const Matrix& matrix,ImVec2 size,ImU32 color);
void hitboxes(ImDrawList* d,const Player& p,const Settings& c,const Matrix& matrix,ImVec2 size,ImU32 color);
void box(ImDrawList* d,const Player& p,const Settings& c,ImVec2 min,ImVec2 max);
void sound(ImDrawList* d,const Player& p,const Settings& c,const Snapshot& frame,std::uint64_t now,ImVec2 size,const std::array<float,4>& rgba);
}
