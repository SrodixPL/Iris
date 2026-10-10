#pragma once
#include "core/menu/menu_state.hpp"
#include <filesystem>
#include <map>
#include <vector>
#include <string>
#include <span>
namespace iris::ui::hotkeys {
enum class Kind { Boolean, Number, Selection, Multi };
enum class Mode { Toggle, Hold, HoldOff };
struct Hotkey {
  ImGuiKey key{ImGuiKey_None};
  Mode mode{Mode::Toggle};
  bool value{true}, visible{true}, active{}, toggled_value{};
  double number{};
  std::vector<bool> selection;
};
struct Option {
  std::vector<Hotkey> entries;
  Kind kind{Kind::Boolean};
  std::string label, path;
  std::vector<std::string> choices;
  std::vector<bool> choice_disabled;
  std::vector<bool> baseline_selection;
  double minimum{}, maximum{100}, step{1}, baseline_number{};
  Bounds anchor{};
  Bounds detail_anchor{};
  size_t editing{};
  bool baseline{}, last_value{}, was_active{};
  bool request_detail{}, listen{}, draft{};
  bool detail_was_open{}, detail_pinned{}, hover_armed{true};
  double hover_until{};
  Hotkey last_detail{};
  bool has_last_detail{};
  int frame{-1};
};
void reset();
void icons(ImFont* font);
Option& option(ImGuiID id);
ImGuiID binding(const std::string& path);
const std::map<ImGuiID,Option>& options();
bool evaluate(ImGuiID id, bool& value, bool disabled=false);
bool evaluate(ImGuiID id, double& value, bool disabled=false);
bool evaluate(ImGuiID id, std::span<bool> value, bool disabled=false);
void popup(ImGuiID id, Option& option, Bounds row, Bounds button, bool disabled=false, bool boolean=true);
bool owns_key(ImGuiKey key);
std::string snapshot();
std::string normalize(const std::string& snapshot);
bool take_dirty();
bool restore(const std::string& snapshot);
const char* error();
}
