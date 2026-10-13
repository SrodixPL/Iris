#pragma once
#include "core/features/inventory/inventory.hpp"
#include "core/features/player/models.hpp"
#include "core/menu/theme.hpp"
#include "core/menu/inventory.hpp"
#include <imgui.h>
#include <memory>
#include <optional>
#include <future>
#include <functional>
namespace iris::ui {
class SettingsIO;
class ConfigIO;
class ImageCache;
class ApplyQueue;
enum class Page { Legit, Rage, Player, World, Inventory, Misc, Config, Count, Aimbot=Legit, Visuals=World, Configs=Config };
using Tab = Page;
inline constexpr std::array<const char*,7> page_names{"Legit","Rage","Player","World","Inventory","Misc","Config"};
inline constexpr const char* app_name="IRIS", *app_version="0.1", *app_author="Srodix";
struct Bounds { ImVec2 min{},max{}; };
struct ColorPalette {
  std::vector<std::optional<ImVec4>> slots;
  std::string error;
  bool writable{true};
  void* owner{};
  bool (*save)(void*,const ColorPalette&){};
};
struct ColorSelectorState {
  char hex[7]{};
  ImVec4 original{};
  int selected_slot{};
  bool request_open{};
  std::string error;
  Bounds trigger{}, popup{}, hex_bounds{}, save{}, original_bounds{};
  std::vector<Bounds> slots;
  std::array<Bounds,4> presets{};
};
struct WorldState {
  std::string skybox_material;
  bool world_override{}, lighting_override{}, bloom_override{}, dof_override{}, wetness_override{}, smoke_override{};
  bool remove_fog{}, remove_shadows{}, remove_sky{}, remove_3d_sky{}, remove_flash{}, remove_smoke{}, remove_ragdolls{};
  float exposure{}, sky_brightness{1}, lighting_intensity{1}, bloom{.5f}, wetness{1}, fog_start{}, fog_falloff{1};
  float dof_near_blurry{}, dof_near_crisp{20}, dof_far_crisp{1500}, dof_far_blurry{3000};
  ImVec4 world_color{1,1,1,1}, lighting_color{1,1,1,1}, smoke_color{1,1,1,1};
  bool enabled{}, sky_override{}, fog_override{};
  float vibrance{1}, contrast{1}, gamma{1}, warmth{}, vignette{}, sharpness{}, haze{};
  float fog_density{.35f}, fog_distance{1800}, wind{};
  bool night_mode{};
  float night_intensity{.6f};
  int weather{}, skybox{};
  float weather_intensity{.5f};
  ImVec4 sky_color{.65f, .7f, .8f, 1}, fog_color{.6f, .6f, .6f, 1};
  float ambient{1}, brightness{1};
};
struct SkinEditor {
  int slot{};
  cosmetics::Selection value;
  int cached_counter{};
};
struct InventoryState {
  bool both_teams{};
  // Session editor caches; serialized configs contain only committed selections.
  std::array<std::vector<SkinEditor>, 2> editors;
  std::array<cosmetics::Selection, 2> gloves;
  InventoryState() {
    for (auto &glove : gloves) {
      glove.definition = 0;
      glove.enabled = false;
    }
  }
};
struct MenuState {
  cosmetics::Configuration cosmetics;
  WorldState world;
  InventoryState inventory;
  std::string hotkeys;
  std::string preview_tree;
};
struct ConfigEntry {
  std::string name, modified;
};
// Separate settings ownership: configs never overwrite these values.
struct MenuSettings {
  int scale{}, color_mode{}, safe_mode{};
  std::string error;
  int hotkey{0x2e};
  int close_key{0x1b};
  std::string autoload;
  std::string inventory_autoload;
  ImVec4 accent{theme::default_accent};
  std::vector<std::optional<ImVec4>> saved_colors;
  bool animations{true};
  bool autosave{true};
};
// Session-only navigation and roster snapshots; never written to configs.
enum class InventoryRosterSource { Unavailable, Preview, Game };
struct InventorySlot {
  int slot{}, definition{};
  bool operator==(const InventorySlot &) const = default;
};
struct InventoryNavigation {
  struct PreviewStage {
    int slot{}, definition{}, paint{};
  };
  std::optional<int> selected_slot;
  std::array<std::vector<InventorySlot>, 2> slots;
  std::array<InventoryRosterSource, 2> sources{};
  std::array<float, 2> overview_scroll{};
  std::array<std::vector<PreviewStage>, 2> preview_stages;
  float route_progress{1};
  bool route_from_picker{};
  std::uint64_t roster_revision{};
  double next_roster_poll{};
  bool initialized{}, restore_scroll{}, observed_runtime_available{};
};
struct MenuRuntime {
  models::Scan model_catalog;
  std::shared_future<models::Scan> model_scan;
  bool models_scanned{};
  char sidebar_search[128]{};
  InventoryNavigation inventory_navigation;
  std::array<int, 2> agent_drafts{};
  std::string username{"User"};
  MenuState values;
  MenuSettings settings;
  Tab selected{Tab::Player}, transition_tab{Tab::Player};
  float tab_progress{1}, nav_marker_y{-1};
  int transition_team{};
  float team_progress{1};
  bool rename_focus{};
  std::string inline_rename;
  std::array<char, 128> rename_buffer{};
  ImVec2 profile_anchor{}, profile_anchor_size{}, action_anchor{},
      action_anchor_size{};
  ImFont *body{};
  ImFont *bold{};
  ImFont *icons{}, *brand{};
  std::array<Bounds,static_cast<size_t>(Page::Count)> navigation{};
  Bounds selector{}, save{}, profile{}, search_bounds{}, clear_search{}, content{}, window{};
  ImVec2 window_offset{};
  bool dragging{};
  char search[128]{};
  bool focus_search{};
  std::string search_target;
  bool search_reveal_pending{};
  float highlight_y{-1};
  bool popup_was_open{}, request_profile{}, request_about{};
  Bounds profile_popup{};
  Bounds about_button{};
  Bounds about_popup{}, about_close{};
  std::array<Bounds,8> profile_controls{};
  std::array<Bounds,2> appearance_controls{};
  transitions::Motion appearance_motion;
  float appearance_from{}, appearance_position{};
  ColorPalette color_palette;
  ColorSelectorState accent_selector;
  ColorSelectorState sky_selector, fog_selector, world_selector, lighting_selector, smoke_selector;
  ColorSelectorState enemy_selector, ally_selector, hidden_selector;
  ColorSelectorState box_enemy_selector,box_ally_selector,box_hidden_selector,chams_selector,chams_hidden_selector;
  std::array<char,256> skybox_material_input{};
  bool skybox_material_editing{};
  std::string skybox_material_error;
  std::shared_ptr<ApplyQueue> apply_queue;
  // Enabled by game-host initialization; standalone fixtures keep local UI state.
  bool gameplay_wired{};
  void (*submit_adapter)(const cosmetics::Configuration&){};
  cosmetics::EquippedLoadout equipped_roster;
  std::shared_ptr<const cosmetics::PreviewImage> preview_image;
  bool apply_pending{}, persist_deferred{};
  std::function<void(bool)> finish_load;
  bool load_submission{}, load_accepted{};
  std::string apply_context;
  InventoryCanvas inventory;
  void (*trace)(const char*) noexcept{};
  bool inventory_trace_started{};
  transitions::PageMotion page_motion;
  bool request_world_preview{};
  float feature_column_width{};
  Bounds title_bounds{};
  bool focus_menu{true};
  bool dirty{};
  int menu_toggle_key{0x2e}; // Renderer compatibility mirror.
  int menu_close_key{0x1b};
  float scale{1};
  std::shared_ptr<SettingsIO> settings_io;
  std::shared_ptr<ConfigIO> config_io,inventory_config_io;
  std::vector<ConfigEntry> inventory_configs;
  std::string active_inventory_config{"default"},pending_inventory_load,inventory_load_after_save;
  std::array<char,65> inventory_config_name{};
  bool inventory_config_busy{},inventory_configs_started{},inventory_config_loaded{},inventory_persist_failed{};
  std::uint64_t inventory_saved_hash{};
  std::shared_ptr<ImageCache> images;
  int fixture_skin{};
  int inventory_team{}, inventory_slot{-1}, inventory_definition{59},
      rarity_filter{};
  float team_tint{};
  std::array<char, 128> skin_search{};
  std::vector<const cosmetics::Finish *> visible_skins;
  bool grid_dirty{true}, grid_scroll_reset{}, follow_team{true};
  std::vector<ConfigEntry> configs;
  std::vector<ConfigEntry> deleted_configs;
  int config_sort{};
  std::string active_config{"default"}, selected_config, pending_load;
  std::uint64_t saved_hash{};
  bool configs_ready{}, config_busy{}, autoload_checked{};
  bool persist_pending{}, persist_failed{}, startup_loading{};
  std::array<char, 128> global_search{};
  std::array<char, 128> config_search{}, quick_search{}, config_name{};
  std::string name_action, feedback;
  Bounds config_anchor;
  bool request_config_popup{};
  bool settings_ready{}, theme_pending{}, key_capture{};
  std::string error;
  cosmetics::Status runtime_status;
  double next_status_poll{};
};
bool submit_features(MenuRuntime &state);
std::uint64_t state_hash(const MenuState &state);
}
