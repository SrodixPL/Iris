#pragma once
#include <imgui.h>
#include <cmath>
namespace iris::ui {
// Supplied SVG geometry preserved by Iris Loader/src/main.cpp::DrawIrisLogo.
inline void draw_brand_logo(ImDrawList* draw,ImVec2 pos,float size) {
  const float scale=size/256.f;
  constexpr float pi=3.14159265f;
  const ImVec2 center{pos.x+size*.5f,pos.y+size*.5f};
  const auto plum=ImGui::GetColorU32(ImVec4{.075f,.063f,.086f,1});
  const auto rose=ImGui::GetColorU32(ImVec4{.91f,.63f,.74f,1});
  draw->AddRectFilled({pos.x+4*scale,pos.y+4*scale},{pos.x+252*scale,pos.y+252*scale},plum,56*scale);
  draw->AddCircle(center,98*.9f*scale,rose,64,5*.9f*scale);
  for(int blade=0;blade<6;++blade) {
    const float angle=blade*pi/3;
    const auto point=[&](float x,float y) {
      return ImVec2{center.x+(x*std::cos(angle)-y*std::sin(angle))*.9f*scale,
        center.y+(x*std::sin(angle)+y*std::cos(angle))*.9f*scale};
    };
    ImVec2 points[20];int count=0;points[count++]=point(0,-26);
    for(int segment=0;segment<=16;++segment) {
      const float arc=(-18.03f+60.f*segment/16)*pi/180;
      points[count++]=point(84*std::cos(arc),84*std::sin(arc));
    }
    points[count++]=point(22.52f,-13);
    draw->AddConcavePolyFilled(points,count,rose);
    draw->AddPolyline(points,count,plum,ImDrawFlags_Closed,3.2f*.9f*scale);
  }
}
}
