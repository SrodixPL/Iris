#pragma once
#include <imgui.h>
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
namespace iris::ui {
struct Bounds;
namespace backdrop {
void initialize(ID3D11Device* device,ID3D11DeviceContext* context,bool blur=true,void (*log)(const char*)=nullptr);
void shutdown();
void begin_frame();
void window(ImDrawList* draw,Bounds bounds,float sidebar,float scale);
void surface(ImDrawList* draw,Bounds bounds,float radius,ImVec4 tint,ImDrawFlags corners=ImDrawFlags_RoundCornersAll);
void surface(ImDrawList* draw,ImVec2 min,ImVec2 max,float radius,ImVec4 tint);
bool render(ID3D11RenderTargetView* target,ImDrawData* data);
bool available();
}
}
