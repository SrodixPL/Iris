#pragma once
#include <imgui.h>
#include <algorithm>
namespace iris::ui::transitions {
inline float ease(float value) { value=std::clamp(value,0.f,1.f); return 1-(1-value)*(1-value)*(1-value); }
struct Motion {
  float progress{1};
  void start(bool animated) { progress=animated ? 0.f : 1.f; }
  float step(bool animated,float seconds) {
    progress=animated ? std::min(1.f,progress+ImGui::GetIO().DeltaTime/seconds) : 1.f;
    return ease(progress);
  }
};
struct PageMotion { int page{-1}; Motion motion; };
}
