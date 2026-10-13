#pragma once
#include <imgui.h>
#include <cstddef>
namespace iris::ui::widgets {
void tooltip(const char* title,const char* detail=nullptr);
void tooltipf(const char* format,...);
float text_y(float top,float height);
void icon_text(ImDrawList* draw,ImVec2 center,ImU32 color,const char* glyph);
void row_text(const char* label,float height=0);
bool button(const char* label,ImVec2 size={},int flags=0);
bool input_text(const char* label,const char* hint,char* buffer,std::size_t capacity,ImGuiInputTextFlags flags=0,float height=0,float padding_x=-1);
bool input_scalar(const char* label,ImGuiDataType type,void* value,const char* format,float height=0,float padding_x=-1);
bool input_int(const char* label,int* value);
}
