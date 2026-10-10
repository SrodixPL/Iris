#pragma once
#include "core/menu/menu_state.hpp"
namespace iris::menu {
using State = ui::MenuRuntime;
void initialize(State& state);
void render(State& state);
}
