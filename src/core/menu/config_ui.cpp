#include "core/menu/config_io.hpp"
#include "core/menu/settings_io.hpp"
#include "core/menu/theme.hpp"
#include "core/menu/widgets.hpp"
#include "core/menu/hotkeys.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
namespace iris::ui {
bool text_matches(const char *text, const char *query) {
  if (!*query)
    return true;
  for (; *text; ++text) {
    const char *a = text;
    const char *b = query;
    while (*a && *b &&
           std::tolower(static_cast<unsigned char>(*a)) ==
               std::tolower(static_cast<unsigned char>(*b))) {
      ++a;
      ++b;
    }
    if (!*b)
      return true;
  }
  return false;
}
void persist_features(MenuRuntime &state) {
  if(state.startup_loading)return;
  if(!state.settings.autosave){state.dirty=true;return;}
  if(state.apply_pending){state.persist_deferred=true;return;}
  state.persist_pending=true;state.persist_failed=false;
  if(state.config_io && state.autoload_checked && !state.config_busy)state.config_io->request(ConfigAction::Autosave,state.active_config,state.values);
}
void save_config(MenuRuntime &state) {
  if (state.config_io && !state.config_busy && !state.apply_pending) {
    state.config_busy = true;
    state.config_io->request(ConfigAction::Save, state.active_config,
                             state.values);
  }
}
void request_load(MenuRuntime &state, const std::string &name) {
  if (state.config_busy || state.persist_pending || state.apply_pending)
    return;
  if (state.dirty)
    state.pending_load = name;
  else {
    state.config_busy = true;
    state.config_io->request(ConfigAction::Load, name);
  }
}
void tick_configs(MenuRuntime &state) {
  if (!state.config_io)
    return;
  while (auto result = state.config_io->poll()) {
    if(result->action!=ConfigAction::Autosave)state.config_busy = false;
    if (!result->error.empty()) {
      state.error = result->error;
      if(result->action==ConfigAction::Save || result->action==ConfigAction::Autosave){state.persist_failed=true;state.persist_pending=false;}
      if(state.startup_loading && result->action==ConfigAction::Load){
        state.startup_loading=false;state.persist_pending=false;
        if(result->name!="default"){state.startup_loading=true;state.config_busy=true;state.config_io->request(ConfigAction::Load,"default");}
      }
      state.configs_ready = true;
      continue;
    }
    if(result->action==ConfigAction::ListDeleted){state.deleted_configs=std::move(result->entries);continue;}
    state.configs = std::move(result->entries);
    state.configs_ready = true;
    if(result->action==ConfigAction::Autosave){
      if(result->name==state.active_config){state.saved_hash=result->hash;state.persist_pending=state_hash(state.values)!=result->hash;}
      continue;
    }
    state.feedback.clear();
    if (result->action == ConfigAction::Load && result->state) {
      state.startup_loading=true;
      auto previous = std::move(state.values);
      state.values = std::move(*result->state);
      state.values.cosmetics.revision = previous.cosmetics.revision;
      hotkeys::restore(state.values.hotkeys);
      hotkeys::take_dirty();
      const auto name=result->name;
      const auto loaded_hotkeys=state.values.hotkeys;
      const auto loaded_hash=state_hash(state.values);
      const auto finish=[&state,name,previous,loaded_hash,loaded_hotkeys](bool success) {
        if(success) {
          state.active_config=name;state.selected_config=name;state.grid_dirty=true;state.grid_scroll_reset=true;
          state.saved_hash=loaded_hash;state.feedback="Loaded "+name;state.values.inventory.editors={};
          state.agent_drafts=state.values.cosmetics.agents;state.settings.autoload=name;save_settings(state);
        } else {
          if(!state.apply_queue)state.values=previous;
          if(state.values.hotkeys==loaded_hotkeys){state.values.hotkeys=previous.hotkeys;hotkeys::restore(previous.hotkeys);hotkeys::take_dirty();}
        }
        state.startup_loading=false;state.config_busy=false;state.persist_pending=false;
        if(state_hash(state.values)!=state.saved_hash)persist_features(state);
      };
      state.load_submission=true;state.load_accepted=false;
      const bool queued=submit_features(state);state.load_submission=false;
      if(queued) {
        if(state.apply_queue){state.config_busy=true;state.finish_load=finish;}
        else finish(true);
      } else {state.values=previous;finish(false);}
    } else if (result->action == ConfigAction::Save ||
               result->action == ConfigAction::Create) {
      state.active_config = result->name;
      state.selected_config = result->name;
      state.saved_hash = result->hash;
      state.feedback = "Saved " + result->name;
      state.settings.autoload = result->name;
      save_settings(state);
    } else if (result->action == ConfigAction::Export) {
      ImGui::SetClipboardText(result->clipboard.c_str());
      state.feedback = "Exported " + result->name + " to clipboard";
    } else if (result->action == ConfigAction::Rename) {
      const std::string old = state.selected_config;
      if (state.active_config == old)
        state.active_config = result->name;
      if (state.settings.autoload == old) {
        state.settings.autoload = result->name;
        save_settings(state);
      }
      state.selected_config = result->name;
    } else if (result->action == ConfigAction::Delete) {
      if (state.active_config == result->name) {
        state.active_config = "default";
        state.saved_hash = 0;
        state.startup_loading=true;state.config_busy=true;state.config_io->request(ConfigAction::Load,"default");
      }
      if (state.settings.autoload == result->name) {
        state.settings.autoload.clear();
        save_settings(state);
      }
      state.selected_config.clear();
      state.feedback = "Deleted " + result->name;
    } else if (result->action == ConfigAction::Duplicate ||
               result->action == ConfigAction::Import)
      state.selected_config = result->name;
  }
  state.dirty = state_hash(state.values) != state.saved_hash;
  if (state.settings_ready && state.configs_ready && !state.autoload_checked) {
    state.autoload_checked = true;
    state.startup_loading=true;state.config_busy=true;
    state.config_io->request(ConfigAction::Load,state.settings.autoload.empty()?"default":state.settings.autoload);
  }
  if(state.settings.autosave && !state.apply_pending && state.persist_pending && !state.persist_failed && state.autoload_checked && !state.config_busy)state.config_io->request(ConfigAction::Autosave,state.active_config,state.values);
}
void config_overlays(MenuRuntime& state) {
  if(!state.name_action.empty()) {
    if(state.request_config_popup){ImGui::OpenPopup("profile_name");state.request_config_popup=false;}
    if(!ImGui::IsPopupOpen("profile_name")){state.name_action.clear();return;}
    const bool deleting=state.name_action=="Delete";
    const char* title=deleting ? "Delete profile" : state.name_action=="Rename" ? "Rename profile" : state.name_action=="Duplicate" ? "Duplicate profile" : state.name_action=="Import" ? "Import profile" : "New profile";
    if(widgets::begin_popup("profile_name",state.config_anchor,theme::config_name_width,theme::config_name_height,title)) {
      if(deleting)ImGui::TextWrapped("Delete %s?",state.selected_config.c_str());
      else {
        if(ImGui::IsWindowAppearing())ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-1);
        widgets::input_text("##name","Profile name",state.config_name.data(),state.config_name.size());
      }
      if(widgets::button(deleting ? "Delete profile" : "Confirm")) {
        const std::string name=state.config_name.data();
        if(!deleting && !valid_config_name(name))state.error="Use 1-64 letters, digits, spaces, hyphens or underscores for the name.";
        else if(state.config_io && !state.config_busy) {
          const auto action=deleting ? ConfigAction::Delete : state.name_action=="Rename" ? ConfigAction::Rename : state.name_action=="Duplicate" ? ConfigAction::Duplicate : state.name_action=="Import" ? ConfigAction::Import : ConfigAction::Create;
          state.config_busy=true;
          const char* clipboard=action==ConfigAction::Import ? ImGui::GetClipboardText() : nullptr;
          state.config_io->request(action,(action==ConfigAction::Rename || action==ConfigAction::Duplicate || deleting) ? state.selected_config : name,state.values,action==ConfigAction::Import ? (clipboard ? clipboard : "") : name);
          state.name_action.clear();state.error.clear();ImGui::CloseCurrentPopup();
        }
      }
      ImGui::SameLine();
      if(widgets::button("Cancel")){state.name_action.clear();ImGui::CloseCurrentPopup();}
      if(!state.error.empty())ImGui::TextWrapped("%s",state.error.c_str());
      widgets::end_popup();
    }
  }
  if(state.pending_load.empty()) return;
  ImGui::OpenPopup("Unsaved changes");
  if(widgets::begin_popup("Unsaved changes",state.content,320,150,"Unsaved changes",false,false,false,true,true)) {
    ImGui::TextWrapped("Discard unsaved changes and load %s?",state.pending_load.c_str());
    if(widgets::button("Cancel")) { state.pending_load.clear(); ImGui::CloseCurrentPopup(); }
    ImGui::SameLine();
    if(widgets::button("Discard and load")) {
      state.config_busy=true; state.config_io->request(ConfigAction::Load,state.pending_load);
      state.pending_load.clear(); ImGui::CloseCurrentPopup();
    }
    widgets::end_popup();
  }
}
}
