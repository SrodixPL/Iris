#pragma once
#include "core/menu/menu_state.hpp"
#include "core/menu/text.hpp"
#include <span>
#include <string>
namespace iris::ui::widgets {
void reset();
void configure(bool animations);
bool key_selector(const char* id, ImGuiKey& key, bool disabled=false, bool listen=false, float height=28, float width=0);
bool keybind_owns_key(ImGuiKey key);
bool capturing_hotkey();
bool navigation(const char* id, const char* label, bool selected, float width,
                MenuRuntime& state, Bounds& bounds);
void profile(MenuRuntime& state, float width);
void section(const char* id, const char* label, MenuRuntime& state);
// Uppercased, letter-spaced caption drawn at an absolute screen position.
void caption(ImVec2 at, const char* text, float scale);
void config_row(MenuRuntime& state, float width);
struct Option { const char* label; bool disabled{}; };
struct PopupContents {
  const char* title;
  void (*draw)(void*);
  void* data{};
  float height{260};
  bool request_open{};
};
bool toggle(const char* id, const char* label, bool& value, bool disabled=false,
            const PopupContents* settings=nullptr, bool hotkeys=true, bool compact=false,ImGuiID binding=0);
bool toggle_more(const char* id, const char* label, bool& value,
                 const PopupContents& settings, bool disabled=false, bool hotkeys=true);
struct RowResult { bool clicked{}; Bounds bounds{}; };
// Full-width row that reads as a row label with a trailing chevron and opens a submenu.
RowResult panel_row(const char* id, const char* label, bool disabled=false);
// Row carrying a key-capture button sized and aligned like every other control.
bool key_row(const char* id, const char* label, ImGuiKey& key, bool disabled=false);
struct SliderEdit { bool active{}, finished{}; Bounds track{}, value{}; };
bool slider(const char* id, const char* label, float& value, float min, float max,
            float step=0, const char* format="%.1f", bool disabled=false, bool hotkeys=true, bool compact=false,bool stacked=false,SliderEdit* edit=nullptr,ImGuiID binding=0);
bool percent(const char* id,const char* label,float& value,bool compact=false,ImGuiID binding=0);
bool slider(const char* id, const char* label, int& value, int min, int max,
            int step=1, const char* format="%d", bool disabled=false, bool hotkeys=true, bool compact=false,ImGuiID binding=0);
bool combo(const char* id, const char* label, int& selected,
           std::span<const Option> options, bool disabled=false,
           const PopupContents* settings=nullptr, bool hotkeys=true, bool compact=false,ImGuiID binding=0);
bool color_selector(const char* id,const char* label,ImVec4& color,ColorSelectorState& state,
                    ColorPalette& palette,bool disabled=false,bool compact=false);
bool save_color_palette(ColorPalette& palette);
bool combo_more(const char* id, const char* label, int& selected,
                std::span<const Option> options, const PopupContents& settings,
                bool disabled=false, bool hotkeys=true);
using SelectionSummary=std::string (*)(std::span<const bool>,std::span<const Option>);
bool multi_combo(const char* id, const char* label, std::span<bool> selected,
                 std::span<const Option> options, const char* title=nullptr,
                 bool disabled=false, SelectionSummary summary=nullptr, bool hotkeys=true,ImGuiID binding=0);
bool begin_popup(const char* id, Bounds anchor, float width, float height,
                 const char* title=nullptr, bool beside=false, bool viewport_only=false, bool compact=false,bool fit_content=true,bool modal=false,bool preserve_focus=false,ImVec2 padding={-1,-1});
void chevron(ImVec2 center,float rotation=0);
bool popup_item(const char* label,bool selected=false,bool keep_open=false,const char* description=nullptr);
float dropdown_width(Bounds field);
bool begin_dropdown(const char* id,const char* preview,int count,float width=0);
void end_dropdown();
void end_popup();
bool popup_closing();
bool begin_group(const char* id, const char* title=nullptr);
void end_group();
}
