#pragma once
namespace iris::ui::feature_tree {
enum class Kind { Toggle,Number,Choice,Multi,Color,Key,Panel,TogglePanel };
struct Node { const char* path; const char* label; int parent; Kind kind; const char* options; float low,high,initial; const char* format; };
#include "core/menu/feature_tree_data.inl"
}
