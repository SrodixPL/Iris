#pragma once
#include "core/features/player/visuals.hpp"
#include <imgui.h>
#include <limits>

namespace iris::ui::player_model {
struct Projected {visuals::Vector position;unsigned color;};
struct Face {unsigned triangle;float depth;};
struct Cache {float yaw{std::numeric_limits<float>::quiet_NaN()},pitch{};std::vector<Face> faces;std::vector<Projected> vertices;};
// Local asset rendering only. No game entities, settings, or native bindings.
visuals::Matrix draw(ImDrawList* draw,ImVec2 min,ImVec2 max,float yaw,float pitch,visuals::Player& player,Cache& cache);
}
