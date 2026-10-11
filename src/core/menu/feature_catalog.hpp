#pragma once
namespace iris::ui::feature_tree {
enum class Kind { Toggle,Number,Choice,Multi,Color,Key,Panel,TogglePanel,ToggleColor,MultiColor };
// Paths retain profile/hotkey identity even when display ownership changes.
struct Node { const char* path; const char* label; int parent; Kind kind; const char* options; float low,high,initial; const char* format; const char* gate{}; const char* color_path{}; bool planned{}; };
#include "core/menu/feature_tree_data.inl"
}
